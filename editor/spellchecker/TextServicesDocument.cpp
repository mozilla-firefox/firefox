/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "TextServicesDocument.h"

#include "EditorBase.h"               // for EditorBase
#include "EditorUtils.h"              // for AutoTransactionBatchExternal
#include "FilteredContentIterator.h"  // for FilteredContentIterator
#include "HTMLEditHelpers.h"          // for BlockInlineCheck
#include "HTMLEditUtils.h"            // for HTMLEditUtils

#include "mozilla/Assertions.h"    // for MOZ_ASSERT, etc
#include "mozilla/IntegerRange.h"  // for IntegerRange
#include "mozilla/mozalloc.h"      // for operator new, etc
#include "mozilla/OwningNonNull.h"
#include "mozilla/RangeBoundary.h"
#include "mozilla/UniquePtr.h"          // for UniquePtr
#include "mozilla/dom/AbstractRange.h"  // for AbstractRange
#include "mozilla/dom/Element.h"
#include "mozilla/dom/Range.h"  // for Range
#include "mozilla/dom/Selection.h"
#include "mozilla/dom/StaticRange.h"  // for StaticRange
#include "mozilla/dom/Text.h"
#include "mozilla/intl/WordBreaker.h"  // for WordRange, WordBreaker

#include "nsAString.h"       // for nsAString::Length, etc
#include "nsContentUtils.h"  // for nsContentUtils
#include "nsComposeTxtSrvFilter.h"
#include "nsDebug.h"                 // for NS_ENSURE_TRUE, etc
#include "nsDependentSubstring.h"    // for Substring
#include "nsError.h"                 // for NS_OK, NS_ERROR_FAILURE, etc
#include "nsGenericHTMLElement.h"    // for nsGenericHTMLElement
#include "nsIContent.h"              // for nsIContent, etc
#include "nsID.h"                    // for NS_GET_IID
#include "nsIEditor.h"               // for nsIEditor, etc
#include "nsIEditorSpellCheck.h"     // for nsIEditorSpellCheck, etc
#include "nsINode.h"                 // for nsINode
#include "nsISelectionController.h"  // for nsISelectionController, etc
#include "nsISupports.h"             // for nsISupports
#include "nsISupportsUtils.h"        // for NS_IF_ADDREF, NS_ADDREF, etc
#include "nsString.h"                // for nsString, nsAutoString
#include "nscore.h"                  // for nsresult, NS_IMETHODIMP, etc

namespace mozilla {

using namespace dom;

/**
 * OffsetEntry manages a range in a text node.  It stores 2 offset values,
 * one is offset in the text node, the other is offset in all text in
 * the ancestor block of the text node.  And the length is managing length
 * in the text node, starting from the offset in text node.
 * In other words, a text node may be managed by multiple instances of this
 * class.
 */
class OffsetEntry final {
 public:
  OffsetEntry() = delete;

  /**
   * @param aTextNode   The text node which will be manged by the instance.
   * @param aOffsetInTextInBlock
   *                    Start offset in the text node which will be managed by
   *                    the instance.
   * @param aLength     Length in the text node which will be managed by the
   *                    instance.
   */
  OffsetEntry(Text& aTextNode, uint32_t aOffsetInTextInBlock, uint32_t aLength)
      : mTextNode(aTextNode),
        mOffsetInTextNode(0),
        mOffsetInTextInBlock(aOffsetInTextInBlock),
        mLength(aLength),
        mIsInsertedText(false),
        mIsValid(true) {}

  /**
   * EndOffsetInTextNode() returns end offset in the text node, which is
   * managed by the instance.
   */
  uint32_t EndOffsetInTextNode() const { return mOffsetInTextNode + mLength; }

  /**
   * OffsetInTextNodeIsInRangeOrEndOffset() checks whether the offset in
   * the text node is managed by the instance or not.
   */
  bool OffsetInTextNodeIsInRangeOrEndOffset(uint32_t aOffsetInTextNode) const {
    return aOffsetInTextNode >= mOffsetInTextNode &&
           aOffsetInTextNode <= EndOffsetInTextNode();
  }

  /**
   * EndOffsetInTextInBlock() returns end offset in the all text in ancestor
   * block of the text node, which is managed by the instance.
   */
  uint32_t EndOffsetInTextInBlock() const {
    return mOffsetInTextInBlock + mLength;
  }

  /**
   * OffsetInTextNodeIsInRangeOrEndOffset() checks whether the offset in
   * the all text in ancestor block of the text node is managed by the instance
   * or not.
   */
  bool OffsetInTextInBlockIsInRangeOrEndOffset(
      uint32_t aOffsetInTextInBlock) const {
    return aOffsetInTextInBlock >= mOffsetInTextInBlock &&
           aOffsetInTextInBlock <= EndOffsetInTextInBlock();
  }

  RawRangeBoundary StartRef() const {
    return RawRangeBoundary(mTextNode, mOffsetInTextNode,
                            RangeBoundarySetBy::Offset, TreeKind::DOM);
  }
  RawRangeBoundary EndRef() const {
    return RawRangeBoundary(mTextNode, EndOffsetInTextNode(),
                            RangeBoundarySetBy::Offset, TreeKind::DOM);
  }

  OwningNonNull<Text> mTextNode;
  uint32_t mOffsetInTextNode;
  // Offset in all text in the closest ancestor block of mTextNode.
  uint32_t mOffsetInTextInBlock;
  uint32_t mLength;
  bool mIsInsertedText;
  bool mIsValid;
};

template <typename ElementType>
struct MOZ_STACK_CLASS ArrayLengthMutationGuard final {
  ArrayLengthMutationGuard() = delete;
  explicit ArrayLengthMutationGuard(const nsTArray<ElementType>& aArray)
      : mArray(aArray), mOldLength(aArray.Length()) {}
  ~ArrayLengthMutationGuard() {
    if (mArray.Length() != mOldLength) {
      MOZ_CRASH("The array length was changed unexpectedly");
    }
  }

 private:
  const nsTArray<ElementType>& mArray;
  size_t mOldLength;
};

#define LockOffsetEntryArrayLengthInDebugBuild(aName, aArray)               \
  DebugOnly<ArrayLengthMutationGuard<UniquePtr<OffsetEntry>>> const aName = \
      ArrayLengthMutationGuard<UniquePtr<OffsetEntry>>(aArray);

TextServicesDocument::TextServicesDocument()
    : mTxtSvcFilterType(0), mIteratorStatus(IteratorStatus::eDone) {}

NS_IMPL_CYCLE_COLLECTING_ADDREF(TextServicesDocument)
NS_IMPL_CYCLE_COLLECTING_RELEASE(TextServicesDocument)

NS_INTERFACE_MAP_BEGIN(TextServicesDocument)
  NS_INTERFACE_MAP_ENTRY(nsIEditActionListener)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsIEditActionListener)
  NS_INTERFACE_MAP_ENTRIES_CYCLE_COLLECTION(TextServicesDocument)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTION(TextServicesDocument, mDocument, mSelCon, mEditorBase,
                         mFilteredIter, mPrevTextBlock, mNextTextBlock, mExtent)

nsresult TextServicesDocument::InitWithEditor(nsIEditor* aEditor) {
  nsCOMPtr<nsISelectionController> selCon;

  NS_ENSURE_TRUE(aEditor, NS_ERROR_NULL_POINTER);

  // Check to see if we already have an mSelCon. If we do, it
  // better be the same one the editor uses!

  nsresult rv = aEditor->GetSelectionController(getter_AddRefs(selCon));

  if (NS_FAILED(rv)) {
    return rv;
  }

  if (!selCon || (mSelCon && selCon != mSelCon)) {
    return NS_ERROR_FAILURE;
  }

  if (!mSelCon) {
    mSelCon = std::move(selCon);
  }

  // Check to see if we already have an mDocument. If we do, it
  // better be the same one the editor uses!

  RefPtr<Document> doc = aEditor->AsEditorBase()->GetDocument();
  if (!doc || (mDocument && doc != mDocument)) {
    return NS_ERROR_FAILURE;
  }

  if (!mDocument) {
    mDocument = doc;

    rv = CreateDocumentContentIterator(getter_AddRefs(mFilteredIter));

    if (NS_FAILED(rv)) {
      return rv;
    }

    mIteratorStatus = IteratorStatus::eDone;

    rv = FirstBlock();

    if (NS_FAILED(rv)) {
      return rv;
    }
  }

  mEditorBase = aEditor->AsEditorBase();

  rv = aEditor->AddEditActionListener(this);

  return rv;
}

nsresult TextServicesDocument::SetExtent(const AbstractRange* aAbstractRange) {
  MOZ_ASSERT(aAbstractRange);

  if (NS_WARN_IF(!mDocument)) {
    return NS_ERROR_FAILURE;
  }

  // We need to store a copy of aAbstractRange since we don't know where it
  // came from.
  mExtent = dom::Range::Create(aAbstractRange, IgnoreErrors());
  if (NS_WARN_IF(!mExtent)) {
    return NS_ERROR_FAILURE;
  }

  // Create a new iterator based on our new extent range.
  nsresult rv =
      CreateFilteredContentIterator(mExtent, getter_AddRefs(mFilteredIter));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  // Now position the iterator at the start of the first block
  // in the range.
  mIteratorStatus = IteratorStatus::eDone;

  rv = FirstBlock();
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv), "FirstBlock() failed");
  return rv;
}

nsresult TextServicesDocument::ExpandRangeToWordBoundaries(
    StaticRange* aStaticRange) {
  MOZ_ASSERT(aStaticRange);

  // Get the end points of the range.

  nsCOMPtr<nsINode> rngStartNode, rngEndNode;
  uint32_t rngStartOffset, rngEndOffset;

  nsresult rv = GetRangeEndPoints(aStaticRange, getter_AddRefs(rngStartNode),
                                  &rngStartOffset, getter_AddRefs(rngEndNode),
                                  &rngEndOffset);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  // Create a content iterator based on the range.
  RefPtr<FilteredContentIterator> filteredIter;
  rv =
      CreateFilteredContentIterator(aStaticRange, getter_AddRefs(filteredIter));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  // Find the first text node in the range.
  IteratorStatus iterStatus = IteratorStatus::eDone;
  rv = FirstTextNode(filteredIter, &iterStatus);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  if (iterStatus == IteratorStatus::eDone) {
    // No text was found so there's no adjustment necessary!
    return NS_OK;
  }

  nsINode* firstText = filteredIter->GetCurrentNode();
  if (NS_WARN_IF(!firstText)) {
    return NS_ERROR_FAILURE;
  }

  // Find the last text node in the range.

  rv = LastTextNode(filteredIter, &iterStatus);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  if (iterStatus == IteratorStatus::eDone) {
    // We should never get here because a first text block
    // was found above.
    NS_ASSERTION(false, "Found a first without a last!");
    return NS_ERROR_FAILURE;
  }

  nsINode* lastText = filteredIter->GetCurrentNode();
  if (NS_WARN_IF(!lastText)) {
    return NS_ERROR_FAILURE;
  }

  // Now make sure our end points are in terms of text nodes in the range!

  if (rngStartNode != firstText) {
    // The range includes the start of the first text node!
    rngStartNode = firstText;
    rngStartOffset = 0;
  }

  if (rngEndNode != lastText) {
    // The range includes the end of the last text node!
    rngEndNode = lastText;
    rngEndOffset = lastText->Length();
  }

  // Create a doc iterator so that we can scan beyond
  // the bounds of the extent range.

  RefPtr<FilteredContentIterator> docFilteredIter;
  rv = CreateDocumentContentIterator(getter_AddRefs(docFilteredIter));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  // Grab all the text in the block containing our
  // first text node.
  rv = docFilteredIter->PositionAt(firstText);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  iterStatus = IteratorStatus::eValid;

  OffsetEntryArray offsetTable;
  nsAutoString blockStr;
  Result<IteratorStatus, nsresult> result = offsetTable.Init(
      *docFilteredIter, IteratorStatus::eValid, nullptr, &blockStr);
  if (result.isErr()) {
    return result.unwrapErr();
  }

  Result<EditorDOMRangeInTexts, nsresult> maybeWordRange =
      offsetTable.FindWordRange(
          blockStr, EditorRawDOMPoint(rngStartNode, rngStartOffset));
  offsetTable.Clear();
  if (maybeWordRange.isErr()) {
    NS_WARNING(
        "TextServicesDocument::OffsetEntryArray::FindWordRange() failed");
    return maybeWordRange.unwrapErr();
  }
  rngStartNode = maybeWordRange.inspect().StartRef().GetContainerAs<Text>();
  rngStartOffset = maybeWordRange.inspect().StartRef().Offset();

  // Grab all the text in the block containing our
  // last text node.

  rv = docFilteredIter->PositionAt(lastText);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  result = offsetTable.Init(*docFilteredIter, IteratorStatus::eValid, nullptr,
                            &blockStr);
  if (result.isErr()) {
    return result.unwrapErr();
  }

  maybeWordRange = offsetTable.FindWordRange(
      blockStr, EditorRawDOMPoint(rngEndNode, rngEndOffset));
  offsetTable.Clear();
  if (maybeWordRange.isErr()) {
    NS_WARNING(
        "TextServicesDocument::OffsetEntryArray::FindWordRange() failed");
    return maybeWordRange.unwrapErr();
  }

  // To prevent expanding the range too much, we only change
  // rngEndNode and rngEndOffset if it isn't already at the start of the
  // word and isn't equivalent to rngStartNode and rngStartOffset.

  if (rngEndNode !=
          maybeWordRange.inspect().StartRef().GetContainerAs<Text>() ||
      rngEndOffset != maybeWordRange.inspect().StartRef().Offset() ||
      (rngEndNode == rngStartNode && rngEndOffset == rngStartOffset)) {
    rngEndNode = maybeWordRange.inspect().EndRef().GetContainerAs<Text>();
    rngEndOffset = maybeWordRange.inspect().EndRef().Offset();
  }

  // Now adjust the range so that it uses our new end points.
  rv = aStaticRange->SetStartAndEnd(rngStartNode, rngStartOffset, rngEndNode,
                                    rngEndOffset);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv), "Failed to update the given range");
  return rv;
}

nsresult TextServicesDocument::SetFilterType(uint32_t aFilterType) {
  mTxtSvcFilterType = aFilterType;

  return NS_OK;
}

nsresult TextServicesDocument::GetCurrentTextBlock(nsAString& aStr) {
  aStr.Truncate();

  NS_ENSURE_TRUE(mFilteredIter, NS_ERROR_FAILURE);

  Result<IteratorStatus, nsresult> result =
      mOffsetTable.Init(*mFilteredIter, mIteratorStatus, mExtent, &aStr);
  if (result.isErr()) {
    NS_WARNING("OffsetEntryArray::Init() failed");
    return result.unwrapErr();
  }
  mIteratorStatus = result.unwrap();
  return NS_OK;
}

nsresult TextServicesDocument::FirstBlock() {
  NS_ENSURE_TRUE(mFilteredIter, NS_ERROR_FAILURE);

  nsresult rv = FirstTextNode(mFilteredIter, &mIteratorStatus);

  if (NS_FAILED(rv)) {
    return rv;
  }

  // Keep track of prev and next blocks, just in case
  // the text service blows away the current block.

  if (mIteratorStatus == IteratorStatus::eValid) {
    mPrevTextBlock = nullptr;
    rv = GetFirstTextNodeInNextBlock(getter_AddRefs(mNextTextBlock));
  } else {
    // There's no text block in the document!

    mPrevTextBlock = nullptr;
    mNextTextBlock = nullptr;
  }

  // XXX Result of FirstTextNode() or GetFirstTextNodeInNextBlock().
  return rv;
}

nsresult TextServicesDocument::LastSelectedBlock(
    BlockSelectionStatus* aSelStatus, uint32_t* aSelOffset,
    uint32_t* aSelLength) {
  NS_ENSURE_TRUE(aSelStatus && aSelOffset && aSelLength, NS_ERROR_NULL_POINTER);

  mIteratorStatus = IteratorStatus::eDone;

  *aSelStatus = BlockSelectionStatus::eBlockNotFound;
  *aSelOffset = *aSelLength = UINT32_MAX;

  if (!mSelCon || !mFilteredIter) {
    return NS_ERROR_FAILURE;
  }

  RefPtr<Selection> selection =
      mSelCon->GetSelection(nsISelectionController::SELECTION_NORMAL);
  if (NS_WARN_IF(!selection)) {
    return NS_ERROR_FAILURE;
  }

  RefPtr<const dom::Range> range;
  nsCOMPtr<nsINode> parent;

  if (selection->IsCollapsed()) {
    // We have a caret. Check if the caret is in a text node.
    // If it is, make the text node's block the current block.
    // If the caret isn't in a text node, search forwards in
    // the document, till we find a text node.

    range = selection->GetRangeAt(0);
    if (!range) {
      return NS_ERROR_FAILURE;
    }

    parent = range->GetStartContainer();
    if (!parent) {
      return NS_ERROR_FAILURE;
    }

    nsresult rv;
    if (parent->IsText()) {
      // The caret is in a text node. Find the beginning
      // of the text block containing this text node and
      // return.

      rv = mFilteredIter->PositionAt(parent->AsText());
      if (NS_FAILED(rv)) {
        return rv;
      }

      rv = FirstTextNodeInCurrentBlock(mFilteredIter);
      if (NS_FAILED(rv)) {
        return rv;
      }

      Result<IteratorStatus, nsresult> result =
          mOffsetTable.Init(*mFilteredIter, IteratorStatus::eValid, mExtent);
      if (result.isErr()) {
        NS_WARNING("OffsetEntryArray::Init() failed");
        mIteratorStatus = IteratorStatus::eValid;  // XXX
        return result.unwrapErr();
      }
      mIteratorStatus = result.unwrap();

      rv = GetSelection(aSelStatus, aSelOffset, aSelLength);
      if (NS_FAILED(rv)) {
        return rv;
      }

      if (*aSelStatus == BlockSelectionStatus::eBlockContains) {
        rv = SetSelectionInternal(*aSelOffset, *aSelLength, false);
      }
    } else {
      // The caret isn't in a text node. Create an iterator
      // based on a range that extends from the current caret
      // position to the end of the document, then walk forwards
      // till you find a text node, then find the beginning of it's block.

      range = CreateDocumentContentRootToNodeOffsetRange(
          parent, range->StartOffset(), false);
      if (NS_WARN_IF(!range)) {
        return NS_ERROR_FAILURE;
      }

      if (range->Collapsed()) {
        // If we get here, the range is collapsed because there is nothing after
        // the caret! Just return NS_OK;
        return NS_OK;
      }

      RefPtr<FilteredContentIterator> filteredIter;
      rv = CreateFilteredContentIterator(range, getter_AddRefs(filteredIter));
      if (NS_FAILED(rv)) {
        return rv;
      }

      filteredIter->First();

      Text* textNode = nullptr;
      for (; !filteredIter->IsDone(); filteredIter->Next()) {
        nsINode* currentNode = filteredIter->GetCurrentNode();
        if (currentNode->IsText()) {
          textNode = currentNode->AsText();
          break;
        }
      }

      if (!textNode) {
        return NS_OK;
      }

      rv = mFilteredIter->PositionAt(textNode);
      if (NS_FAILED(rv)) {
        return rv;
      }

      rv = FirstTextNodeInCurrentBlock(mFilteredIter);
      if (NS_FAILED(rv)) {
        return rv;
      }

      Result<IteratorStatus, nsresult> result = mOffsetTable.Init(
          *mFilteredIter, IteratorStatus::eValid, mExtent, nullptr);
      if (result.isErr()) {
        NS_WARNING("OffsetEntryArray::Init() failed");
        mIteratorStatus = IteratorStatus::eValid;  // XXX
        return result.unwrapErr();
      }
      mIteratorStatus = result.inspect();

      rv = GetSelection(aSelStatus, aSelOffset, aSelLength);
      if (NS_FAILED(rv)) {
        return rv;
      }
    }

    // Result of SetSelectionInternal() in the |if| block or NS_OK.
    return rv;
  }

  // If we get here, we have an uncollapsed selection!
  // Look backwards through each range in the selection till you
  // find the first text node. If you find one, find the
  // beginning of its text block, and make it the current
  // block.

  const uint32_t rangeCount = selection->RangeCount();
  MOZ_ASSERT(
      rangeCount,
      "Selection is not collapsed, so, the range count should be 1 or larger");

  // XXX: We may need to add some code here to make sure
  //      the ranges are sorted in document appearance order!

  for (const uint32_t i : Reversed(IntegerRange(rangeCount))) {
    MOZ_ASSERT(selection->RangeCount() == rangeCount);
    range = selection->GetRangeAt(i);
    if (MOZ_UNLIKELY(!range)) {
      return NS_OK;  // XXX Really?
    }

    // Create an iterator for the range.

    RefPtr<FilteredContentIterator> filteredIter;
    nsresult rv =
        CreateFilteredContentIterator(range, getter_AddRefs(filteredIter));
    if (NS_FAILED(rv)) {
      return rv;
    }

    filteredIter->Last();

    // Now walk through the range till we find a text node.

    for (; !filteredIter->IsDone(); filteredIter->Prev()) {
      if (filteredIter->GetCurrentNode()->NodeType() == nsINode::TEXT_NODE) {
        // We found a text node, so position the document's
        // iterator at the beginning of the block, then get
        // the selection in terms of the string offset.

        nsresult rv = mFilteredIter->PositionAt(filteredIter->GetCurrentNode());
        if (NS_FAILED(rv)) {
          return rv;
        }

        rv = FirstTextNodeInCurrentBlock(mFilteredIter);
        if (NS_FAILED(rv)) {
          return rv;
        }

        mIteratorStatus = IteratorStatus::eValid;

        Result<IteratorStatus, nsresult> result =
            mOffsetTable.Init(*mFilteredIter, IteratorStatus::eValid, mExtent);
        if (result.isErr()) {
          NS_WARNING("OffsetEntryArray::Init() failed");
          mIteratorStatus = IteratorStatus::eValid;  // XXX
          return result.unwrapErr();
        }
        mIteratorStatus = result.unwrap();

        return GetSelection(aSelStatus, aSelOffset, aSelLength);
      }
    }
  }

  // If we get here, we didn't find any text node in the selection!
  // Create a range that extends from the end of the selection,
  // to the end of the document, then iterate forwards through
  // it till you find a text node!
  range = rangeCount > 0 ? selection->GetRangeAt(rangeCount - 1) : nullptr;
  if (!range) {
    return NS_ERROR_FAILURE;
  }

  parent = range->GetEndContainer();
  if (!parent) {
    return NS_ERROR_FAILURE;
  }

  range = CreateDocumentContentRootToNodeOffsetRange(parent, range->EndOffset(),
                                                     false);
  if (NS_WARN_IF(!range)) {
    return NS_ERROR_FAILURE;
  }

  if (range->Collapsed()) {
    // If we get here, the range is collapsed because there is nothing after
    // the current selection! Just return NS_OK;
    return NS_OK;
  }

  RefPtr<FilteredContentIterator> filteredIter;
  nsresult rv =
      CreateFilteredContentIterator(range, getter_AddRefs(filteredIter));
  if (NS_FAILED(rv)) {
    return rv;
  }

  filteredIter->First();

  for (; !filteredIter->IsDone(); filteredIter->Next()) {
    if (filteredIter->GetCurrentNode()->NodeType() == nsINode::TEXT_NODE) {
      // We found a text node! Adjust the document's iterator to point
      // to the beginning of its text block, then get the current selection.
      nsresult rv = mFilteredIter->PositionAt(filteredIter->GetCurrentNode());
      if (NS_FAILED(rv)) {
        return rv;
      }

      rv = FirstTextNodeInCurrentBlock(mFilteredIter);
      if (NS_FAILED(rv)) {
        return rv;
      }

      Result<IteratorStatus, nsresult> result =
          mOffsetTable.Init(*mFilteredIter, IteratorStatus::eValid, mExtent);
      if (result.isErr()) {
        NS_WARNING("OffsetEntryArray::Init() failed");
        mIteratorStatus = IteratorStatus::eValid;  // XXX
        return result.unwrapErr();
      }
      mIteratorStatus = result.unwrap();

      rv = GetSelection(aSelStatus, aSelOffset, aSelLength);
      NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                           "TextServicesDocument::GetSelection() failed");
      return rv;
    }
  }

  // If we get here, we didn't find any block before or inside
  // the selection! Just return OK.
  return NS_OK;
}

nsresult TextServicesDocument::PrevBlock() {
  NS_ENSURE_TRUE(mFilteredIter, NS_ERROR_FAILURE);

  if (mIteratorStatus == IteratorStatus::eDone) {
    return NS_OK;
  }

  switch (mIteratorStatus) {
    case IteratorStatus::eValid:
    case IteratorStatus::eNext: {
      nsresult rv = FirstTextNodeInPrevBlock(mFilteredIter);

      if (NS_FAILED(rv)) {
        mIteratorStatus = IteratorStatus::eDone;
        return rv;
      }

      if (mFilteredIter->IsDone()) {
        mIteratorStatus = IteratorStatus::eDone;
        return NS_OK;
      }

      mIteratorStatus = IteratorStatus::eValid;
      break;
    }
    case IteratorStatus::ePrev:

      // The iterator already points to the previous
      // block, so don't do anything.

      mIteratorStatus = IteratorStatus::eValid;
      break;

    default:

      mIteratorStatus = IteratorStatus::eDone;
      break;
  }

  // Keep track of prev and next blocks, just in case
  // the text service blows away the current block.
  nsresult rv = NS_OK;
  if (mIteratorStatus == IteratorStatus::eValid) {
    GetFirstTextNodeInPrevBlock(getter_AddRefs(mPrevTextBlock));
    rv = GetFirstTextNodeInNextBlock(getter_AddRefs(mNextTextBlock));
  } else {
    // We must be done!
    mPrevTextBlock = nullptr;
    mNextTextBlock = nullptr;
  }

  // XXX The result of GetFirstTextNodeInNextBlock() or NS_OK.
  return rv;
}

nsresult TextServicesDocument::NextBlock() {
  NS_ENSURE_TRUE(mFilteredIter, NS_ERROR_FAILURE);

  if (mIteratorStatus == IteratorStatus::eDone) {
    return NS_OK;
  }

  switch (mIteratorStatus) {
    case IteratorStatus::eValid: {
      // Advance the iterator to the next text block.

      nsresult rv = FirstTextNodeInNextBlock(mFilteredIter);

      if (NS_FAILED(rv)) {
        mIteratorStatus = IteratorStatus::eDone;
        return rv;
      }

      if (mFilteredIter->IsDone()) {
        mIteratorStatus = IteratorStatus::eDone;
        return NS_OK;
      }

      mIteratorStatus = IteratorStatus::eValid;
      break;
    }
    case IteratorStatus::eNext:

      // The iterator already points to the next block,
      // so don't do anything to it!

      mIteratorStatus = IteratorStatus::eValid;
      break;

    case IteratorStatus::ePrev:

      // If the iterator is pointing to the previous block,
      // we know that there is no next text block! Just
      // fall through to the default case!

    default:

      mIteratorStatus = IteratorStatus::eDone;
      break;
  }

  // Keep track of prev and next blocks, just in case
  // the text service blows away the current block.
  nsresult rv = NS_OK;
  if (mIteratorStatus == IteratorStatus::eValid) {
    GetFirstTextNodeInPrevBlock(getter_AddRefs(mPrevTextBlock));
    rv = GetFirstTextNodeInNextBlock(getter_AddRefs(mNextTextBlock));
  } else {
    // We must be done.
    mPrevTextBlock = nullptr;
    mNextTextBlock = nullptr;
  }

  // The result of GetFirstTextNodeInNextBlock() or NS_OK.
  return rv;
}

nsresult TextServicesDocument::IsDone(bool* aIsDone) {
  NS_ENSURE_TRUE(aIsDone, NS_ERROR_NULL_POINTER);

  *aIsDone = false;

  NS_ENSURE_TRUE(mFilteredIter, NS_ERROR_FAILURE);

  *aIsDone = mIteratorStatus == IteratorStatus::eDone;

  return NS_OK;
}

nsresult TextServicesDocument::SetSelection(uint32_t aOffset,
                                            uint32_t aLength) {
  NS_ENSURE_TRUE(mSelCon, NS_ERROR_FAILURE);

  return SetSelectionInternal(aOffset, aLength, true);
}

nsresult TextServicesDocument::ScrollSelectionIntoView() {
  NS_ENSURE_TRUE(mSelCon, NS_ERROR_FAILURE);

  // After ScrollSelectionIntoView(), the pending notifications might be flushed
  // and PresShell/PresContext/Frames may be dead. See bug 418470.
  const nsCOMPtr selCon = mSelCon;
  return selCon->ScrollSelectionIntoView(
      SelectionType::eNormal, nsISelectionController::SELECTION_FOCUS_REGION,
      AxisScrollParams(), AxisScrollParams(), ScrollFlags::None,
      SelectionScrollMode::SyncFlush);
}

nsresult TextServicesDocument::OffsetEntryArray::WillDeleteSelection() {
  MOZ_ASSERT(mSelection.IsSet());
  MOZ_ASSERT(!mSelection.IsCollapsed());

  for (size_t i = mSelection.StartIndex(); i <= mSelection.EndIndex(); i++) {
    OffsetEntry* entry = ElementAt(i).get();
    if (i == mSelection.StartIndex()) {
      // Calculate the length of the selection. Note that the
      // selection length can be zero if the start of the selection
      // is at the very end of a text node entry.
      uint32_t selLength;
      if (entry->mIsInsertedText) {
        // Inserted text offset entries have no width when
        // talking in terms of string offsets! If the beginning
        // of the selection is in an inserted text offset entry,
        // the caret is always at the end of the entry!
        selLength = 0;
      } else {
        selLength = entry->EndOffsetInTextInBlock() -
                    mSelection.StartOffsetInTextInBlock();
      }

      if (selLength > 0) {
        if (mSelection.StartOffsetInTextInBlock() >
            entry->mOffsetInTextInBlock) {
          // Selection doesn't start at the beginning of the
          // text node entry. We need to split this entry into
          // two pieces, the piece before the selection, and
          // the piece inside the selection.
          nsresult rv = SplitElementAt(i, selLength);
          if (NS_FAILED(rv)) {
            NS_WARNING("selLength was invalid for the OffsetEntry");
            return rv;
          }

          // Adjust selection indexes to account for new entry:
          MOZ_DIAGNOSTIC_ASSERT(mSelection.StartIndex() + 1 < Length());
          MOZ_DIAGNOSTIC_ASSERT(mSelection.EndIndex() + 1 < Length());
          mSelection.SetIndexes(mSelection.StartIndex() + 1,
                                mSelection.EndIndex() + 1);
          entry = ElementAt(++i).get();
        }

        if (mSelection.StartIndex() < mSelection.EndIndex()) {
          // The entire entry is contained in the selection. Mark the
          // entry invalid.
          entry->mIsValid = false;
        }
      }
    }

    if (i == mSelection.EndIndex()) {
      if (entry->mIsInsertedText) {
        // Inserted text offset entries have no width when
        // talking in terms of string offsets! If the end
        // of the selection is in an inserted text offset entry,
        // the selection includes the entire entry!
        entry->mIsValid = false;
      } else {
        // Calculate the length of the selection. Note that the
        // selection length can be zero if the end of the selection
        // is at the very beginning of a text node entry.

        const uint32_t selLength =
            mSelection.EndOffsetInTextInBlock() - entry->mOffsetInTextInBlock;
        if (selLength) {
          if (mSelection.EndOffsetInTextInBlock() <
              entry->EndOffsetInTextInBlock()) {
            // mOffsetInTextInBlock is guaranteed to be inside the selection,
            // even when mSelection.IsInSameElement() is true.
            nsresult rv = SplitElementAt(i, entry->mLength - selLength);
            if (NS_FAILED(rv)) {
              NS_WARNING(
                  "entry->mLength - selLength was invalid for the OffsetEntry");
              return rv;
            }

            // Update the entry fields:
            ElementAt(i + 1)->mOffsetInTextNode = entry->mOffsetInTextNode;
          }

          if (mSelection.EndOffsetInTextInBlock() ==
              entry->EndOffsetInTextInBlock()) {
            // The entire entry is contained in the selection. Mark the
            // entry invalid.
            entry->mIsValid = false;
          }
        }
      }
    }

    if (i != mSelection.StartIndex() && i != mSelection.EndIndex()) {
      // The entire entry is contained in the selection. Mark the
      // entry invalid.
      entry->mIsValid = false;
    }
  }

  return NS_OK;
}

nsresult TextServicesDocument::DeleteSelection() {
  if (NS_WARN_IF(!mEditorBase) ||
      NS_WARN_IF(!mOffsetTable.mSelection.IsSet())) {
    return NS_ERROR_FAILURE;
  }

  if (mOffsetTable.mSelection.IsCollapsed()) {
    return NS_OK;
  }

  // If we have an mExtent, save off its current set of
  // end points so we can compare them against mExtent's
  // set after the deletion of the content.

  nsCOMPtr<nsINode> origStartNode, origEndNode;
  uint32_t origStartOffset = 0, origEndOffset = 0;

  if (mExtent) {
    nsresult rv = GetRangeEndPoints(
        mExtent, getter_AddRefs(origStartNode), &origStartOffset,
        getter_AddRefs(origEndNode), &origEndOffset);

    if (NS_FAILED(rv)) {
      return rv;
    }
  }

  if (NS_FAILED(mOffsetTable.WillDeleteSelection())) {
    NS_WARNING(
        "TextServicesDocument::OffsetEntryTable::WillDeleteSelection() failed");
    return NS_ERROR_FAILURE;
  }

  // Make sure mFilteredIter always points to something valid!
  AdjustContentIterator();

  // Now delete the actual content!
  OwningNonNull<EditorBase> editorBase = *mEditorBase;
  nsresult rv = editorBase->DeleteSelectionAsAction(nsIEditor::ePrevious,
                                                    nsIEditor::eStrip);
  if (NS_FAILED(rv)) {
    return rv;
  }

  // Now that we've actually deleted the selected content,
  // check to see if our mExtent has changed, if so, then
  // we have to create a new content iterator!

  if (origStartNode && origEndNode) {
    nsCOMPtr<nsINode> curStartNode, curEndNode;
    uint32_t curStartOffset = 0, curEndOffset = 0;

    rv = GetRangeEndPoints(mExtent, getter_AddRefs(curStartNode),
                           &curStartOffset, getter_AddRefs(curEndNode),
                           &curEndOffset);

    if (NS_FAILED(rv)) {
      return rv;
    }

    if (origStartNode != curStartNode || origEndNode != curEndNode) {
      // The range has changed, so we need to create a new content
      // iterator based on the new range.
      nsCOMPtr<nsIContent> curContent;
      if (mIteratorStatus != IteratorStatus::eDone) {
        // The old iterator is still pointing to something valid,
        // so get its current node so we can restore it after we
        // create the new iterator!
        curContent = mFilteredIter->GetCurrentNode()
                         ? mFilteredIter->GetCurrentNode()->AsContent()
                         : nullptr;
      }

      // Create the new iterator.
      rv =
          CreateFilteredContentIterator(mExtent, getter_AddRefs(mFilteredIter));
      if (NS_FAILED(rv)) {
        return rv;
      }

      // Now make the new iterator point to the content node
      // the old one was pointing at.
      if (curContent) {
        rv = mFilteredIter->PositionAt(curContent);
        if (NS_FAILED(rv)) {
          mIteratorStatus = IteratorStatus::eDone;
        } else {
          mIteratorStatus = IteratorStatus::eValid;
        }
      }
    }
  }

  OffsetEntry* entry = mOffsetTable.DidDeleteSelection();
  if (entry) {
    SetSelection(mOffsetTable.mSelection.StartOffsetInTextInBlock(), 0);
  }

  // Now remove any invalid entries from the offset table.
  mOffsetTable.RemoveInvalidElements();
  return NS_OK;
}

OffsetEntry* TextServicesDocument::OffsetEntryArray::DidDeleteSelection() {
  MOZ_ASSERT(mSelection.IsSet());

  // Move the caret to the end of the first valid entry.
  // Start with SelectionStartIndex() since it may still be valid.
  OffsetEntry* entry = nullptr;
  for (size_t i = mSelection.StartIndex() + 1; !entry && i > 0; i--) {
    entry = ElementAt(i - 1).get();
    if (!entry->mIsValid) {
      entry = nullptr;
    } else {
      MOZ_DIAGNOSTIC_ASSERT(i - 1 < Length());
      mSelection.Set(i - 1, entry->EndOffsetInTextInBlock());
    }
  }

  // If we still don't have a valid entry, move the caret
  // to the next valid entry after the selection:
  for (size_t i = mSelection.EndIndex(); !entry && i < Length(); i++) {
    entry = ElementAt(i).get();
    if (!entry->mIsValid) {
      entry = nullptr;
    } else {
      MOZ_DIAGNOSTIC_ASSERT(i < Length());
      mSelection.Set(i, entry->mOffsetInTextInBlock);
    }
  }

  if (!entry) {
    // Uuughh we have no valid offset entry to place our
    // caret ... just mark the selection invalid.
    mSelection.Reset();
  }

  return entry;
}

nsresult TextServicesDocument::InsertText(const nsAString& aText) {
  if (NS_WARN_IF(!mEditorBase) ||
      NS_WARN_IF(!mOffsetTable.mSelection.IsSet())) {
    return NS_ERROR_FAILURE;
  }

  // If the selection is not collapsed, we need to save
  // off the selection offsets so we can restore the
  // selection and delete the selected content after we've
  // inserted the new text. This is necessary to try and
  // retain as much of the original style of the content
  // being deleted.

  const bool wasSelectionCollapsed = mOffsetTable.mSelection.IsCollapsed();
  const uint32_t savedSelOffset =
      mOffsetTable.mSelection.StartOffsetInTextInBlock();
  const uint32_t savedSelLength = mOffsetTable.mSelection.LengthInTextInBlock();

  if (!wasSelectionCollapsed) {
    // Collapse to the start of the current selection
    // for the insert!
    nsresult rv =
        SetSelection(mOffsetTable.mSelection.StartOffsetInTextInBlock(), 0);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  // AutoTransactionBatchExternal grabs mEditorBase, so, we don't need to grab
  // the instance with local variable here.
  OwningNonNull<EditorBase> editorBase = *mEditorBase;
  AutoTransactionBatchExternal treatAsOneTransaction(editorBase);

  nsresult rv = editorBase->InsertTextAsAction(aText);
  if (NS_FAILED(rv)) {
    NS_WARNING("InsertTextAsAction() failed");
    return rv;
  }

  RefPtr<Selection> selection =
      mSelCon->GetSelection(nsISelectionController::SELECTION_NORMAL);
  rv = mOffsetTable.DidInsertText(selection, aText);
  if (NS_FAILED(rv)) {
    NS_WARNING("TextServicesDocument::OffsetEntry::DidInsertText() failed");
    return rv;
  }

  if (!wasSelectionCollapsed) {
    nsresult rv = SetSelection(savedSelOffset, savedSelLength);
    if (NS_FAILED(rv)) {
      return rv;
    }

    rv = DeleteSelection();
    if (NS_FAILED(rv)) {
      return rv;
    }
  }

  return NS_OK;
}

nsresult TextServicesDocument::OffsetEntryArray::DidInsertText(
    dom::Selection* aSelection, const nsAString& aInsertedString) {
  MOZ_ASSERT(mSelection.IsSet());

  // When you touch this method, please make sure that the entry instance
  // won't be deleted.  If you know it'll be deleted, you should set it to
  // `nullptr`.
  OffsetEntry* entry = ElementAt(mSelection.StartIndex()).get();
  OwningNonNull<Text> const textNodeAtStartEntry = entry->mTextNode;

  NS_ASSERTION((entry->mIsValid), "Invalid insertion point!");

  if (entry->mOffsetInTextInBlock == mSelection.StartOffsetInTextInBlock()) {
    if (entry->mIsInsertedText) {
      // If the caret is in an inserted text offset entry,
      // we simply insert the text at the end of the entry.
      entry->mLength += aInsertedString.Length();
    } else {
      // Insert an inserted text offset entry before the current
      // entry!
      auto newInsertedTextEntry = std::make_unique<OffsetEntry>(
          entry->mTextNode, entry->mOffsetInTextInBlock,
          aInsertedString.Length());
      newInsertedTextEntry->mIsInsertedText = true;
      newInsertedTextEntry->mOffsetInTextNode = entry->mOffsetInTextNode;
      // XXX(Bug 1631371) Check if this should use a fallible operation as it
      // pretended earlier.
      InsertElementAt(mSelection.StartIndex(), std::move(newInsertedTextEntry));
    }
  } else if (entry->EndOffsetInTextInBlock() ==
             mSelection.EndOffsetInTextInBlock()) {
    // We are inserting text at the end of the current offset entry.
    // Look at the next valid entry in the table. If it's an inserted
    // text entry, add to its length and adjust its node offset. If
    // it isn't, add a new inserted text entry.
    uint32_t nextIndex = mSelection.StartIndex() + 1;
    OffsetEntry* insertedTextEntry = nullptr;
    if (Length() > nextIndex) {
      insertedTextEntry = ElementAt(nextIndex).get();
      if (!insertedTextEntry) {
        return NS_ERROR_FAILURE;
      }

      // Check if the entry is a match. If it isn't, set
      // iEntry to zero.
      if (!insertedTextEntry->mIsInsertedText ||
          insertedTextEntry->mOffsetInTextInBlock !=
              mSelection.StartOffsetInTextInBlock()) {
        insertedTextEntry = nullptr;
      }
    }

    if (!insertedTextEntry) {
      // We didn't find an inserted text offset entry, so
      // create one.
      auto newInsertedTextEntry = std::make_unique<OffsetEntry>(
          entry->mTextNode, mSelection.StartOffsetInTextInBlock(), 0);
      newInsertedTextEntry->mOffsetInTextNode = entry->EndOffsetInTextNode();
      newInsertedTextEntry->mIsInsertedText = true;
      // XXX(Bug 1631371) Check if this should use a fallible operation as it
      // pretended earlier.
      insertedTextEntry =
          InsertElementAt(nextIndex, std::move(newInsertedTextEntry))->get();
    }

    // We have a valid inserted text offset entry. Update its
    // length, adjust the selection indexes, and make sure the
    // caret is properly placed!

    insertedTextEntry->mLength += aInsertedString.Length();

    MOZ_DIAGNOSTIC_ASSERT(nextIndex < Length());
    mSelection.SetIndex(nextIndex);

    if (!aSelection) {
      return NS_OK;
    }

    OwningNonNull<Text> textNode = insertedTextEntry->mTextNode;
    nsresult rv = aSelection->CollapseInLimiter(
        textNode, insertedTextEntry->EndOffsetInTextNode());
    if (NS_FAILED(rv)) {
      NS_WARNING("Selection::CollapseInLimiter() failed");
      return rv;
    }
  } else if (entry->EndOffsetInTextInBlock() >
             mSelection.StartOffsetInTextInBlock()) {
    // We are inserting text into the middle of the current offset entry.
    // split the current entry into two parts, then insert an inserted text
    // entry between them!
    nsresult rv = SplitElementAt(mSelection.StartIndex(),
                                 entry->EndOffsetInTextInBlock() -
                                     mSelection.StartOffsetInTextInBlock());
    if (NS_FAILED(rv)) {
      NS_WARNING(
          "entry->EndOffsetInTextInBlock() - "
          "mSelection.StartOffsetInTextInBlock() was invalid for the "
          "OffsetEntry");
      return rv;
    }

    // XXX(Bug 1631371) Check if this should use a fallible operation as it
    // pretended earlier.
    UniquePtr<OffsetEntry>& insertedTextEntry = *InsertElementAt(
        mSelection.StartIndex() + 1,
        std::make_unique<OffsetEntry>(entry->mTextNode,
                                      mSelection.StartOffsetInTextInBlock(),
                                      aInsertedString.Length()));
    LockOffsetEntryArrayLengthInDebugBuild(observer, *this);
    insertedTextEntry->mIsInsertedText = true;
    insertedTextEntry->mOffsetInTextNode = entry->EndOffsetInTextNode();
    MOZ_DIAGNOSTIC_ASSERT(mSelection.StartIndex() + 1 < Length());
    mSelection.SetIndex(mSelection.StartIndex() + 1);
  }

  // We've just finished inserting an inserted text offset entry.
  // update all entries with the same mTextNode pointer that follow
  // it in the table!

  for (size_t i = mSelection.StartIndex() + 1; i < Length(); i++) {
    const UniquePtr<OffsetEntry>& entry = ElementAt(i);
    LockOffsetEntryArrayLengthInDebugBuild(observer, *this);
    if (entry->mTextNode != textNodeAtStartEntry) {
      break;
    }
    if (entry->mIsValid) {
      entry->mOffsetInTextNode += aInsertedString.Length();
    }
  }

  return NS_OK;
}

void TextServicesDocument::DidDeleteContent(const nsIContent& aChildContent) {
  if (NS_WARN_IF(!mFilteredIter) || !aChildContent.IsText()) {
    return;
  }

  Maybe<size_t> maybeNodeIndex =
      mOffsetTable.FirstIndexOf(*aChildContent.AsText());
  if (maybeNodeIndex.isNothing()) {
    // It's okay if the node isn't in the offset table, the
    // editor could be cleaning house.
    return;
  }

  nsINode* node = mFilteredIter->GetCurrentNode();
  if (node && node == &aChildContent &&
      mIteratorStatus != IteratorStatus::eDone) {
    // XXX: This should never really happen because
    // AdjustContentIterator() should have been called prior
    // to the delete to try and position the iterator on the
    // next valid text node in the offset table, and if there
    // wasn't a next, it would've set mIteratorStatus to eIsDone.

    NS_ERROR("DeleteNode called for current iterator node.");
  }

  for (size_t nodeIndex = *maybeNodeIndex; nodeIndex < mOffsetTable.Length();
       nodeIndex++) {
    const UniquePtr<OffsetEntry>& entry = mOffsetTable[nodeIndex];
    LockOffsetEntryArrayLengthInDebugBuild(observer, mOffsetTable);
    if (!entry) {
      return;
    }

    if (entry->mTextNode == &aChildContent) {
      entry->mIsValid = false;
    }
  }
}

void TextServicesDocument::DidJoinContents(
    const EditorRawDOMPoint& aJoinedPoint, const nsIContent& aRemovedContent) {
  // Make sure that both nodes are text nodes -- otherwise we don't care.
  if (!aJoinedPoint.IsInTextNode() || !aRemovedContent.IsText()) {
    return;
  }

  // Note: The editor merges the contents of the left node into the
  //       contents of the right.

  Maybe<size_t> maybeRemovedIndex =
      mOffsetTable.FirstIndexOf(*aRemovedContent.AsText());
  if (maybeRemovedIndex.isNothing()) {
    // It's okay if the node isn't in the offset table, the
    // editor could be cleaning house.
    return;
  }

  Maybe<size_t> maybeJoinedIndex =
      mOffsetTable.FirstIndexOf(*aJoinedPoint.ContainerAs<Text>());
  if (maybeJoinedIndex.isNothing()) {
    // It's okay if the node isn't in the offset table, the
    // editor could be cleaning house.
    return;
  }

  const size_t removedIndex = *maybeRemovedIndex;
  const size_t joinedIndex = *maybeJoinedIndex;

  if (MOZ_UNLIKELY(joinedIndex > removedIndex)) {
    NS_ASSERTION(joinedIndex < removedIndex, "Indexes out of order.");
    return;
  }
  NS_ASSERTION(mOffsetTable[removedIndex]->mOffsetInTextNode == 0,
               "Unexpected offset value for rightIndex.");

  // Run through the table and change all entries referring to
  // the removed node so that they now refer to the joined node,
  // and adjust offsets if necessary.
  const uint32_t movedTextDataLength =
      aJoinedPoint.ContainerAs<Text>()->TextDataLength() -
      aJoinedPoint.Offset();
  for (uint32_t i = removedIndex; i < mOffsetTable.Length(); i++) {
    const UniquePtr<OffsetEntry>& entry = mOffsetTable[i];
    LockOffsetEntryArrayLengthInDebugBuild(observer, mOffsetTable);
    if (entry->mTextNode != aRemovedContent.AsText()) {
      break;
    }
    if (entry->mIsValid) {
      entry->mTextNode = aJoinedPoint.ContainerAs<Text>();
      // The text was moved from aRemovedContent to end of the container of
      // aJoinedPoint.
      entry->mOffsetInTextNode += movedTextDataLength;
    }
  }

  // Now check to see if the iterator is pointing to the
  // left node. If it is, make it point to the joined node!
  if (mFilteredIter->GetCurrentNode() == aRemovedContent.AsText()) {
    mFilteredIter->PositionAt(aJoinedPoint.ContainerAs<Text>());
  }
}

nsresult TextServicesDocument::CreateFilteredContentIterator(
    const AbstractRange* aAbstractRange,
    FilteredContentIterator** aFilteredIter) {
  if (NS_WARN_IF(!aAbstractRange) || NS_WARN_IF(!aFilteredIter)) {
    return NS_ERROR_INVALID_ARG;
  }

  *aFilteredIter = nullptr;

  UniquePtr<nsComposeTxtSrvFilter> composeFilter;
  switch (mTxtSvcFilterType) {
    case nsIEditorSpellCheck::FILTERTYPE_NORMAL:
      composeFilter = nsComposeTxtSrvFilter::CreateNormalFilter();
      break;
    case nsIEditorSpellCheck::FILTERTYPE_MAIL:
      composeFilter = nsComposeTxtSrvFilter::CreateMailFilter();
      break;
  }

  // Create a FilteredContentIterator
  // This class wraps the ContentIterator in order to give itself a chance
  // to filter out certain content nodes
  RefPtr filter = MakeRefPtr<FilteredContentIterator>(std::move(composeFilter));
  nsresult rv = filter->Init(aAbstractRange);
  if (NS_FAILED(rv)) {
    return rv;
  }

  filter.forget(aFilteredIter);
  return NS_OK;
}

Element* TextServicesDocument::GetDocumentContentRootNode() const {
  if (NS_WARN_IF(!mDocument)) {
    return nullptr;
  }

  if (mDocument->IsHTMLOrXHTML()) {
    Element* rootElement = mDocument->GetRootElement();
    if (rootElement && rootElement->IsXULElement()) {
      // HTML documents with root XUL elements should eventually be transitioned
      // to a regular document structure, but for now the content root node will
      // be the document element.
      return mDocument->GetDocumentElement();
    }
    // For HTML documents, the content root node is the body.
    return mDocument->GetBody();
  }

  // For non-HTML documents, the content root node will be the document element.
  return mDocument->GetDocumentElement();
}

already_AddRefed<dom::Range>
TextServicesDocument::CreateDocumentContentRange() {
  nsCOMPtr<nsINode> node = GetDocumentContentRootNode();
  if (NS_WARN_IF(!node)) {
    return nullptr;
  }

  RefPtr<dom::Range> range = dom::Range::Create(node);
  IgnoredErrorResult ignoredError;
  range->SelectNodeContents(*node, ignoredError);
  NS_WARNING_ASSERTION(!ignoredError.Failed(), "SelectNodeContents() failed");
  return range.forget();
}

already_AddRefed<dom::Range>
TextServicesDocument::CreateDocumentContentRootToNodeOffsetRange(
    nsINode* aParent, uint32_t aOffset, bool aToStart) {
  if (NS_WARN_IF(!aParent)) {
    return nullptr;
  }

  nsCOMPtr<nsINode> bodyNode = GetDocumentContentRootNode();
  if (NS_WARN_IF(!bodyNode)) {
    return nullptr;
  }

  nsCOMPtr<nsINode> startNode;
  nsCOMPtr<nsINode> endNode;
  uint32_t startOffset, endOffset;

  if (aToStart) {
    // The range should begin at the start of the document
    // and extend up until (aParent, aOffset).
    startNode = bodyNode;
    startOffset = 0;
    endNode = aParent;
    endOffset = aOffset;
  } else {
    // The range should begin at (aParent, aOffset) and
    // extend to the end of the document.
    startNode = aParent;
    startOffset = aOffset;
    endNode = bodyNode;
    endOffset = endNode ? endNode->GetChildCount() : 0;
  }

  RefPtr<dom::Range> range = dom::Range::Create(startNode, startOffset, endNode,
                                                endOffset, IgnoreErrors());
  NS_WARNING_ASSERTION(range,
                       "Range::Create() failed to create new valid range");
  return range.forget();
}

nsresult TextServicesDocument::CreateDocumentContentIterator(
    FilteredContentIterator** aFilteredIter) {
  NS_ENSURE_TRUE(aFilteredIter, NS_ERROR_NULL_POINTER);

  RefPtr<dom::Range> range = CreateDocumentContentRange();
  if (NS_WARN_IF(!range)) {
    *aFilteredIter = nullptr;
    return NS_ERROR_FAILURE;
  }

  return CreateFilteredContentIterator(range, aFilteredIter);
}

nsresult TextServicesDocument::AdjustContentIterator() {
  NS_ENSURE_TRUE(mFilteredIter, NS_ERROR_FAILURE);

  nsCOMPtr<nsINode> node = mFilteredIter->GetCurrentNode();
  NS_ENSURE_TRUE(node, NS_ERROR_FAILURE);

  Text* prevValidTextNode = nullptr;
  Text* nextValidTextNode = nullptr;
  bool foundEntry = false;

  const size_t tableLength = mOffsetTable.Length();
  for (size_t i = 0; i < tableLength && !nextValidTextNode; i++) {
    UniquePtr<OffsetEntry>& entry = mOffsetTable[i];
    LockOffsetEntryArrayLengthInDebugBuild(observer, mOffsetTable);
    if (entry->mTextNode == node) {
      if (entry->mIsValid) {
        // The iterator is still pointing to something valid!
        // Do nothing!
        return NS_OK;
      }
      // We found an invalid entry that points to
      // the current iterator node. Stop looking for
      // a previous valid node!
      foundEntry = true;
    }

    if (entry->mIsValid) {
      if (!foundEntry) {
        prevValidTextNode = entry->mTextNode;
      } else {
        nextValidTextNode = entry->mTextNode;
      }
    }
  }

  Text* validTextNode = nullptr;
  if (prevValidTextNode) {
    validTextNode = prevValidTextNode;
  } else if (nextValidTextNode) {
    validTextNode = nextValidTextNode;
  }

  if (validTextNode) {
    nsresult rv = mFilteredIter->PositionAt(validTextNode);
    if (NS_FAILED(rv)) {
      mIteratorStatus = IteratorStatus::eDone;
    } else {
      mIteratorStatus = IteratorStatus::eValid;
    }
    return rv;
  }

  // If we get here, there aren't any valid entries
  // in the offset table! Try to position the iterator
  // on the next text block first, then previous if
  // one doesn't exist!

  if (mNextTextBlock) {
    nsresult rv = mFilteredIter->PositionAt(mNextTextBlock);
    if (NS_FAILED(rv)) {
      mIteratorStatus = IteratorStatus::eDone;
      return rv;
    }

    mIteratorStatus = IteratorStatus::eNext;
  } else if (mPrevTextBlock) {
    nsresult rv = mFilteredIter->PositionAt(mPrevTextBlock);
    if (NS_FAILED(rv)) {
      mIteratorStatus = IteratorStatus::eDone;
      return rv;
    }

    mIteratorStatus = IteratorStatus::ePrev;
  } else {
    mIteratorStatus = IteratorStatus::eDone;
  }
  return NS_OK;
}

// static
bool TextServicesDocument::DidSkip(FilteredContentIterator* aFilteredIter) {
  return aFilteredIter && aFilteredIter->DidSkip();
}

// static
void TextServicesDocument::ClearDidSkip(
    FilteredContentIterator* aFilteredIter) {
  // Clear filter's skip flag
  if (aFilteredIter) {
    aFilteredIter->ClearDidSkip();
  }
}

// static
bool TextServicesDocument::HasSameBlockNodeParent(Text& aTextNode1,
                                                  Text& aTextNode2) {
  // XXX How about the case that both text nodes are orphan nodes?
  if (aTextNode1.GetParent() == aTextNode2.GetParent()) {
    return true;
  }

  // I think that spellcheck should be available only in editable nodes.
  // So, we also need to check whether they are in same editing host.
  const Element* editableBlockElementOrInlineEditingHost1 =
      HTMLEditUtils::GetAncestorElement(
          aTextNode1,
          HTMLEditUtils::ClosestEditableBlockElementOrInlineEditingHost,
          BlockInlineCheck::UseHTMLDefaultStyle);
  const Element* editableBlockElementOrInlineEditingHost2 =
      HTMLEditUtils::GetAncestorElement(
          aTextNode2,
          HTMLEditUtils::ClosestEditableBlockElementOrInlineEditingHost,
          BlockInlineCheck::UseHTMLDefaultStyle);
  return editableBlockElementOrInlineEditingHost1 &&
         editableBlockElementOrInlineEditingHost1 ==
             editableBlockElementOrInlineEditingHost2;
}

Result<EditorRawDOMRangeInTexts, nsresult>
TextServicesDocument::OffsetEntryArray::WillSetSelection(
    uint32_t aOffsetInTextInBlock, uint32_t aLength) {
  // Find start of selection in node offset terms:
  EditorRawDOMPointInText newStart;
  for (size_t i = 0; !newStart.IsSet() && i < Length(); i++) {
    const UniquePtr<OffsetEntry>& entry = ElementAt(i);
    LockOffsetEntryArrayLengthInDebugBuild(observer, *this);
    if (entry->mIsValid) {
      if (entry->mIsInsertedText) {
        // Caret can only be placed at the end of an
        // inserted text offset entry, if the offsets
        // match exactly!
        if (entry->mOffsetInTextInBlock == aOffsetInTextInBlock) {
          newStart.Set(entry->mTextNode, entry->EndOffsetInTextNode());
        }
      } else if (aOffsetInTextInBlock >= entry->mOffsetInTextInBlock) {
        bool foundEntry = false;
        if (aOffsetInTextInBlock < entry->EndOffsetInTextInBlock()) {
          foundEntry = true;
        } else if (aOffsetInTextInBlock == entry->EndOffsetInTextInBlock()) {
          // Peek after this entry to see if we have any
          // inserted text entries belonging to the same
          // entry->mTextNode. If so, we have to place the selection
          // after it!
          if (i + 1 < Length()) {
            const UniquePtr<OffsetEntry>& nextEntry = ElementAt(i + 1);
            LockOffsetEntryArrayLengthInDebugBuild(observer, *this);
            if (!nextEntry->mIsValid ||
                nextEntry->mOffsetInTextInBlock != aOffsetInTextInBlock) {
              // Next offset entry isn't an exact match, so we'll
              // just use the current entry.
              foundEntry = true;
            }
          }
        }

        if (foundEntry) {
          newStart.Set(entry->mTextNode, entry->mOffsetInTextNode +
                                             aOffsetInTextInBlock -
                                             entry->mOffsetInTextInBlock);
        }
      }

      if (newStart.IsSet()) {
        MOZ_DIAGNOSTIC_ASSERT(i < Length());
        mSelection.Set(i, aOffsetInTextInBlock);
      }
    }
  }

  if (NS_WARN_IF(!newStart.IsSet())) {
    return Err(NS_ERROR_FAILURE);
  }

  if (!aLength) {
    mSelection.CollapseToStart();
    return EditorRawDOMRangeInTexts(newStart);
  }

  // Find the end of the selection in node offset terms:
  EditorRawDOMPointInText newEnd;
  const uint32_t endOffset = aOffsetInTextInBlock + aLength;
  for (uint32_t i = Length(); !newEnd.IsSet() && i > 0; i--) {
    const UniquePtr<OffsetEntry>& entry = ElementAt(i - 1);
    LockOffsetEntryArrayLengthInDebugBuild(observer, *this);
    if (entry->mIsValid) {
      if (entry->mIsInsertedText) {
        if (entry->mOffsetInTextInBlock ==
            (newEnd.IsSet() ? newEnd.Offset() : 0)) {
          // If the selection ends on an inserted text offset entry,
          // the selection includes the entire entry!
          newEnd.Set(entry->mTextNode, entry->EndOffsetInTextNode());
        }
      } else if (entry->OffsetInTextInBlockIsInRangeOrEndOffset(endOffset)) {
        newEnd.Set(entry->mTextNode, entry->mOffsetInTextNode + endOffset -
                                         entry->mOffsetInTextInBlock);
      }

      if (newEnd.IsSet()) {
        MOZ_DIAGNOSTIC_ASSERT(mSelection.StartIndex() < Length());
        MOZ_DIAGNOSTIC_ASSERT(i - 1 < Length());
        mSelection.Set(mSelection.StartIndex(), i - 1,
                       mSelection.StartOffsetInTextInBlock(), endOffset);
      }
    }
  }

  return newEnd.IsSet() ? EditorRawDOMRangeInTexts(newStart, newEnd)
                        : EditorRawDOMRangeInTexts(newStart);
}

nsresult TextServicesDocument::SetSelectionInternal(
    uint32_t aOffsetInTextInBlock, uint32_t aLength, bool aDoUpdate) {
  if (NS_WARN_IF(!mSelCon)) {
    return NS_ERROR_INVALID_ARG;
  }

  Result<EditorRawDOMRangeInTexts, nsresult> newSelectionRange =
      mOffsetTable.WillSetSelection(aOffsetInTextInBlock, aLength);
  if (newSelectionRange.isErr()) {
    NS_WARNING(
        "TextServicesDocument::OffsetEntryArray::WillSetSelection() failed");
    return newSelectionRange.unwrapErr();
  }

  if (!aDoUpdate) {
    return NS_OK;
  }

  // XXX: If we ever get a SetSelection() method in nsIEditor, we should
  //      use it.
  RefPtr<Selection> selection =
      mSelCon->GetSelection(nsISelectionController::SELECTION_NORMAL);
  if (NS_WARN_IF(!selection)) {
    return NS_ERROR_FAILURE;
  }

  if (newSelectionRange.inspect().Collapsed()) {
    nsresult rv =
        selection->CollapseInLimiter(newSelectionRange.inspect().StartRef());
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                         "Selection::CollapseInLimiter() failed");
    return rv;
  }

  ErrorResult error;
  selection->SetStartAndEndInLimiter(newSelectionRange.inspect().StartRef(),
                                     newSelectionRange.inspect().EndRef(),
                                     error);
  NS_WARNING_ASSERTION(!error.Failed(),
                       "Selection::SetStartAndEndInLimiter() failed");
  return error.StealNSResult();
}

nsresult TextServicesDocument::GetSelection(BlockSelectionStatus* aSelStatus,
                                            uint32_t* aSelOffset,
                                            uint32_t* aSelLength) {
  NS_ENSURE_TRUE(aSelStatus && aSelOffset && aSelLength, NS_ERROR_NULL_POINTER);

  *aSelStatus = BlockSelectionStatus::eBlockNotFound;
  *aSelOffset = UINT32_MAX;
  *aSelLength = UINT32_MAX;

  NS_ENSURE_TRUE(mDocument && mSelCon, NS_ERROR_FAILURE);

  if (mIteratorStatus == IteratorStatus::eDone) {
    return NS_OK;
  }

  RefPtr<Selection> selection =
      mSelCon->GetSelection(nsISelectionController::SELECTION_NORMAL);
  NS_ENSURE_TRUE(selection, NS_ERROR_FAILURE);

  if (selection->IsCollapsed()) {
    return GetCollapsedSelection(aSelStatus, aSelOffset, aSelLength);
  }

  return GetUncollapsedSelection(aSelStatus, aSelOffset, aSelLength);
}

nsresult TextServicesDocument::GetCollapsedSelection(
    BlockSelectionStatus* aSelStatus, uint32_t* aSelOffset,
    uint32_t* aSelLength) {
  RefPtr<Selection> selection =
      mSelCon->GetSelection(nsISelectionController::SELECTION_NORMAL);
  NS_ENSURE_TRUE(selection, NS_ERROR_FAILURE);

  // The calling function should have done the GetIsCollapsed()
  // check already. Just assume it's collapsed!
  *aSelStatus = BlockSelectionStatus::eBlockOutside;
  *aSelOffset = *aSelLength = UINT32_MAX;

  const uint32_t tableCount = mOffsetTable.Length();
  if (!tableCount) {
    return NS_OK;
  }

  // Get pointers to the first and last offset entries
  // in the table.

  UniquePtr<OffsetEntry>& eStart = mOffsetTable[0];
  UniquePtr<OffsetEntry>& eEnd =
      tableCount > 1 ? mOffsetTable[tableCount - 1] : eStart;
  LockOffsetEntryArrayLengthInDebugBuild(observer, mOffsetTable);

  const dom::Range* const selectionRange = selection->GetRangeAt(0);
  NS_ENSURE_STATE(selectionRange);

  const Maybe<int32_t> e1s1 =
      nsContentUtils::ComparePoints<TreeKind::ShadowIncludingDOM>(
          eStart->StartRef(), selectionRange->StartRef());
  const Maybe<int32_t> e2s1 =
      nsContentUtils::ComparePoints<TreeKind::ShadowIncludingDOM>(
          eEnd->EndRef(), selectionRange->StartRef());

  if (MOZ_UNLIKELY(NS_WARN_IF(!e1s1) || NS_WARN_IF(!e2s1))) {
    return NS_ERROR_FAILURE;
  }

  if (*e1s1 > 0 || *e2s1 < 0) {
    // We're done if the caret is outside the current text block.
    return NS_OK;
  }

  nsINode* const parent = selectionRange->GetStartContainer();
  MOZ_ASSERT(parent);
  uint32_t offset = selectionRange->StartOffset();
  if (parent->IsText()) {
    // Good news, the caret is in a text node. Look
    // through the offset table for the entry that
    // matches its parent and offset.

    for (uint32_t i = 0; i < tableCount; i++) {
      const UniquePtr<OffsetEntry>& entry = mOffsetTable[i];
      LockOffsetEntryArrayLengthInDebugBuild(observer, mOffsetTable);
      if (entry->mTextNode == parent->AsText() &&
          entry->OffsetInTextNodeIsInRangeOrEndOffset(offset)) {
        *aSelStatus = BlockSelectionStatus::eBlockContains;
        *aSelOffset =
            entry->mOffsetInTextInBlock + (offset - entry->mOffsetInTextNode);
        *aSelLength = 0;
        return NS_OK;
      }
    }

    // If we get here, we didn't find a text node entry
    // in our offset table that matched.
    return NS_ERROR_FAILURE;
  }

  // The caret is in our text block, but it's positioned in some
  // non-text node (ex. <b>). Create a range based on the start
  // and end of the text block, then create an iterator based on
  // this range, with its initial position set to the closest
  // child of this non-text node. Then look for the closest text
  // node.

  const RefPtr<const dom::Range> range =
      dom::Range::Create(eStart->StartRef(), eEnd->EndRef(), IgnoreErrors());
  if (NS_WARN_IF(!range)) {
    return NS_ERROR_FAILURE;
  }

  RefPtr<FilteredContentIterator> filteredIter;
  nsresult rv =
      CreateFilteredContentIterator(range, getter_AddRefs(filteredIter));
  NS_ENSURE_SUCCESS(rv, rv);

  nsIContent* saveNode;
  if (parent->HasChildren()) {
    // XXX: We need to make sure that all of parent's
    //      children are in the text block.

    // If the parent has children, position the iterator
    // on the child that is to the left of the offset.

    nsIContent* content = range->GetChildAtStartOffset();
    if (content && parent->GetFirstChild() != content) {
      content = content->GetPreviousSibling();
    }
    NS_ENSURE_TRUE(content, NS_ERROR_FAILURE);

    nsresult rv = filteredIter->PositionAt(content);
    NS_ENSURE_SUCCESS(rv, rv);

    saveNode = content;
  } else {
    // The parent has no children, so position the iterator
    // on the parent.
    NS_ENSURE_TRUE(parent->IsContent(), NS_ERROR_FAILURE);
    nsIContent* const content = parent->AsContent();

    nsresult rv = filteredIter->PositionAt(content);
    NS_ENSURE_SUCCESS(rv, rv);

    saveNode = content;
  }

  // Now iterate to the left, towards the beginning of
  // the text block, to find the first text node you
  // come across.

  Text* textNode = nullptr;
  for (; !filteredIter->IsDone(); filteredIter->Prev()) {
    nsINode* current = filteredIter->GetCurrentNode();
    if (current->IsText()) {
      textNode = current->AsText();
      break;
    }
  }

  if (textNode) {
    // We found a node, now set the offset to the end
    // of the text node.
    offset = textNode->TextLength();
  } else {
    // We should never really get here, but I'm paranoid.

    // We didn't find a text node above, so iterate to
    // the right, towards the end of the text block, looking
    // for a text node.

    nsresult rv = filteredIter->PositionAt(saveNode);
    NS_ENSURE_SUCCESS(rv, rv);

    textNode = nullptr;
    for (; !filteredIter->IsDone(); filteredIter->Next()) {
      nsINode* current = filteredIter->GetCurrentNode();
      if (current->IsText()) {
        textNode = current->AsText();
        break;
      }
    }
    NS_ENSURE_TRUE(textNode, NS_ERROR_FAILURE);

    // We found a text node, so set the offset to
    // the beginning of the node.
    offset = 0;
  }

  for (size_t i = 0; i < tableCount; i++) {
    const UniquePtr<OffsetEntry>& entry = mOffsetTable[i];
    LockOffsetEntryArrayLengthInDebugBuild(observer, mOffsetTable);
    if (entry->mTextNode == textNode &&
        entry->OffsetInTextNodeIsInRangeOrEndOffset(offset)) {
      *aSelStatus = BlockSelectionStatus::eBlockContains;
      *aSelOffset =
          entry->mOffsetInTextInBlock + (offset - entry->mOffsetInTextNode);
      *aSelLength = 0;

      // Now move the caret so that it is actually in the text node.
      // We do this to keep things in sync.
      //
      // In most cases, the user shouldn't see any movement in the caret
      // on screen.
      return SetSelectionInternal(*aSelOffset, *aSelLength, true);
    }
  }

  return NS_ERROR_FAILURE;
}

nsresult TextServicesDocument::GetUncollapsedSelection(
    BlockSelectionStatus* aSelStatus, uint32_t* aSelOffset,
    uint32_t* aSelLength) {
  RefPtr<Selection> selection =
      mSelCon->GetSelection(nsISelectionController::SELECTION_NORMAL);
  NS_ENSURE_TRUE(selection, NS_ERROR_FAILURE);

  // It is assumed that the calling function has made sure that the
  // selection is not collapsed, and that the input params to this
  // method are initialized to some defaults.

  const size_t tableCount = mOffsetTable.Length();

  // Get pointers to the first and last offset entries
  // in the table.

  UniquePtr<OffsetEntry>& eStart = mOffsetTable[0];
  UniquePtr<OffsetEntry>& eEnd =
      tableCount > 1 ? mOffsetTable[tableCount - 1] : eStart;
  LockOffsetEntryArrayLengthInDebugBuild(observer, mOffsetTable);

  const RawRangeBoundary startRef = eStart->StartRef();
  const RawRangeBoundary endRef = eEnd->EndRef();

  const uint32_t rangeCount = selection->RangeCount();
  MOZ_ASSERT(rangeCount);

  // Find the first range in the selection that intersects
  // the current text block.
  Maybe<int32_t> e1s2;
  Maybe<int32_t> e2s1;
  const dom::Range* selectionRange = nullptr;
  for (const uint32_t i : IntegerRange(rangeCount)) {
    MOZ_ASSERT(selection->RangeCount() == rangeCount);
    selectionRange = selection->GetRangeAt(i);
    if (NS_WARN_IF(!selectionRange)) {
      return NS_ERROR_FAILURE;
    }

    e1s2 = nsContentUtils::ComparePoints<TreeKind::ShadowIncludingDOM>(
        startRef, selectionRange->EndRef());
    if (NS_WARN_IF(!e1s2)) {
      return NS_ERROR_FAILURE;
    }

    e2s1 = nsContentUtils::ComparePoints<TreeKind::ShadowIncludingDOM>(
        endRef, selectionRange->StartRef());
    if (NS_WARN_IF(!e2s1)) {
      return NS_ERROR_FAILURE;
    }

    // Break out of the loop if the text block intersects the current range.

    if (*e1s2 <= 0 && *e2s1 >= 0) {
      break;
    }
  }

  // We're done if we didn't find an intersecting range.

  if (rangeCount < 1 || *e1s2 > 0 || *e2s1 < 0) {
    *aSelStatus = BlockSelectionStatus::eBlockOutside;
    *aSelOffset = *aSelLength = UINT32_MAX;
    return NS_OK;
  }

  // Now that we have an intersecting range, find out more info:
  const Maybe<int32_t> e1s1 =
      nsContentUtils::ComparePoints<TreeKind::ShadowIncludingDOM>(
          startRef, selectionRange->StartRef());
  if (NS_WARN_IF(!e1s1)) {
    return NS_ERROR_FAILURE;
  }

  const Maybe<int32_t> e2s2 =
      nsContentUtils::ComparePoints<TreeKind::ShadowIncludingDOM>(
          endRef, selectionRange->EndRef());
  if (NS_WARN_IF(!e2s2)) {
    return NS_ERROR_FAILURE;
  }

  if (rangeCount > 1) {
    // There are multiple selection ranges, we only deal
    // with the first one that intersects the current,
    // text block, so mark this a as a partial.
    *aSelStatus = BlockSelectionStatus::eBlockPartial;
  } else if (*e1s1 > 0 && *e2s2 < 0) {
    // The range extends beyond the start and
    // end of the current text block.
    *aSelStatus = BlockSelectionStatus::eBlockInside;
  } else if (*e1s1 <= 0 && *e2s2 >= 0) {
    // The current text block contains the entire
    // range.
    *aSelStatus = BlockSelectionStatus::eBlockContains;
  } else {
    // The range partially intersects the block.
    *aSelStatus = BlockSelectionStatus::eBlockPartial;
  }

  // Now create a range based on the intersection of the
  // text block and range:

  nsCOMPtr<nsINode> p1, p2;
  uint32_t o1, o2;

  // The start of the range will be the rightmost
  // start node.

  if (*e1s1 >= 0) {
    p1 = eStart->mTextNode;
    o1 = eStart->mOffsetInTextNode;
  } else {
    p1 = selectionRange->GetStartContainer();
    o1 = selectionRange->StartOffset();
  }

  // The end of the range will be the leftmost
  // end node.

  if (*e2s2 <= 0) {
    p2 = eEnd->mTextNode;
    o2 = eEnd->EndOffsetInTextNode();
  } else {
    p2 = selectionRange->GetEndContainer();
    o2 = selectionRange->EndOffset();
  }

  const RefPtr<const dom::Range> range =
      dom::Range::Create(p1, o1, p2, o2, IgnoreErrors());
  if (NS_WARN_IF(!range)) {
    return NS_ERROR_FAILURE;
  }

  // Now iterate over this range to figure out the selection's
  // block offset and length.

  RefPtr<FilteredContentIterator> filteredIter;
  nsresult rv =
      CreateFilteredContentIterator(range, getter_AddRefs(filteredIter));
  NS_ENSURE_SUCCESS(rv, rv);

  // Find the first text node in the range.
  nsCOMPtr<nsIContent> content;
  filteredIter->First();
  if (!p1->IsText()) {
    bool found = false;
    for (; !filteredIter->IsDone(); filteredIter->Next()) {
      nsINode* node = filteredIter->GetCurrentNode();
      if (node->IsText()) {
        p1 = node->AsText();
        o1 = 0;
        found = true;
        break;
      }
    }
    NS_ENSURE_TRUE(found, NS_ERROR_FAILURE);
  }

  // Find the last text node in the range.
  filteredIter->Last();
  if (!p2->IsText()) {
    bool found = false;
    for (; !filteredIter->IsDone(); filteredIter->Prev()) {
      nsINode* node = filteredIter->GetCurrentNode();
      if (node->IsText()) {
        p2 = node->AsText();
        o2 = p2->AsText()->Length();
        found = true;

        break;
      }
    }
    NS_ENSURE_TRUE(found, NS_ERROR_FAILURE);
  }

  bool found = false;
  *aSelLength = 0;

  for (size_t i = 0; i < tableCount; i++) {
    const UniquePtr<OffsetEntry>& entry = mOffsetTable[i];
    LockOffsetEntryArrayLengthInDebugBuild(observer, mOffsetTable);
    if (!found) {
      if (entry->mTextNode == p1.get() &&
          entry->OffsetInTextNodeIsInRangeOrEndOffset(o1)) {
        *aSelOffset =
            entry->mOffsetInTextInBlock + (o1 - entry->mOffsetInTextNode);
        if (p1 == p2 && entry->OffsetInTextNodeIsInRangeOrEndOffset(o2)) {
          // The start and end of the range are in the same offset
          // entry. Calculate the length of the range then we're done.
          *aSelLength = o2 - o1;
          break;
        }
        // Add the length of the sub string in this offset entry
        // that follows the start of the range.
        *aSelLength = entry->EndOffsetInTextNode() - o1;
        found = true;
      }
    } else {  // Found.
      if (entry->mTextNode == p2.get() &&
          entry->OffsetInTextNodeIsInRangeOrEndOffset(o2)) {
        // We found the end of the range. Calculate the length of the
        // sub string that is before the end of the range, then we're done.
        *aSelLength += o2 - entry->mOffsetInTextNode;
        break;
      }
      // The entire entry must be in the range.
      *aSelLength += entry->mLength;
    }
  }

  return NS_OK;
}

// static
nsresult TextServicesDocument::GetRangeEndPoints(
    const AbstractRange* aAbstractRange, nsINode** aStartContainer,
    uint32_t* aStartOffset, nsINode** aEndContainer, uint32_t* aEndOffset) {
  if (NS_WARN_IF(!aAbstractRange) || NS_WARN_IF(!aStartContainer) ||
      NS_WARN_IF(!aEndContainer) || NS_WARN_IF(!aEndOffset)) {
    return NS_ERROR_INVALID_ARG;
  }

  nsCOMPtr<nsINode> startContainer = aAbstractRange->GetStartContainer();
  if (NS_WARN_IF(!startContainer)) {
    return NS_ERROR_FAILURE;
  }
  nsCOMPtr<nsINode> endContainer = aAbstractRange->GetEndContainer();
  if (NS_WARN_IF(!endContainer)) {
    return NS_ERROR_FAILURE;
  }

  startContainer.forget(aStartContainer);
  endContainer.forget(aEndContainer);
  *aStartOffset = aAbstractRange->StartOffset();
  *aEndOffset = aAbstractRange->EndOffset();
  return NS_OK;
}

// static
nsresult TextServicesDocument::FirstTextNode(
    FilteredContentIterator* aFilteredIter, IteratorStatus* aIteratorStatus) {
  if (aIteratorStatus) {
    *aIteratorStatus = IteratorStatus::eDone;
  }

  for (aFilteredIter->First(); !aFilteredIter->IsDone();
       aFilteredIter->Next()) {
    if (aFilteredIter->GetCurrentNode()->NodeType() == nsINode::TEXT_NODE) {
      if (aIteratorStatus) {
        *aIteratorStatus = IteratorStatus::eValid;
      }
      break;
    }
  }

  return NS_OK;
}

// static
nsresult TextServicesDocument::LastTextNode(
    FilteredContentIterator* aFilteredIter, IteratorStatus* aIteratorStatus) {
  if (aIteratorStatus) {
    *aIteratorStatus = IteratorStatus::eDone;
  }

  for (aFilteredIter->Last(); !aFilteredIter->IsDone(); aFilteredIter->Prev()) {
    if (aFilteredIter->GetCurrentNode()->NodeType() == nsINode::TEXT_NODE) {
      if (aIteratorStatus) {
        *aIteratorStatus = IteratorStatus::eValid;
      }
      break;
    }
  }

  return NS_OK;
}

// static
nsresult TextServicesDocument::FirstTextNodeInCurrentBlock(
    FilteredContentIterator* aFilteredIter) {
  NS_ENSURE_TRUE(aFilteredIter, NS_ERROR_NULL_POINTER);

  ClearDidSkip(aFilteredIter);

  // Walk backwards over adjacent text nodes until
  // we hit a block boundary:
  RefPtr<Text> lastTextNode;
  while (!aFilteredIter->IsDone()) {
    nsCOMPtr<nsIContent> content =
        aFilteredIter->GetCurrentNode()->IsContent()
            ? aFilteredIter->GetCurrentNode()->AsContent()
            : nullptr;
    // We don't observe layout updates, therefore, we should consider whether
    // block or inline only with the default definition of the element.
    if (lastTextNode && content &&
        (HTMLEditUtils::IsBlockElement(*content,
                                       BlockInlineCheck::UseHTMLDefaultStyle) ||
         content->IsHTMLElement(nsGkAtoms::br))) {
      break;
    }
    if (content && content->IsText()) {
      if (lastTextNode && !TextServicesDocument::HasSameBlockNodeParent(
                              *content->AsText(), *lastTextNode)) {
        // We're done, the current text node is in a
        // different block.
        break;
      }
      lastTextNode = content->AsText();
    }

    aFilteredIter->Prev();

    if (DidSkip(aFilteredIter)) {
      break;
    }
  }

  if (lastTextNode) {
    aFilteredIter->PositionAt(lastTextNode);
  }

  // XXX: What should we return if last is null?

  return NS_OK;
}

// static
nsresult TextServicesDocument::FirstTextNodeInPrevBlock(
    FilteredContentIterator* aFilteredIter) {
  NS_ENSURE_TRUE(aFilteredIter, NS_ERROR_NULL_POINTER);

  // XXX: What if mFilteredIter is not currently on a text node?

  // Make sure mFilteredIter is pointing to the first text node in the
  // current block:

  nsresult rv = FirstTextNodeInCurrentBlock(aFilteredIter);

  NS_ENSURE_SUCCESS(rv, NS_ERROR_FAILURE);

  // Point mFilteredIter to the first node before the first text node:

  aFilteredIter->Prev();

  if (aFilteredIter->IsDone()) {
    return NS_ERROR_FAILURE;
  }

  // Now find the first text node of the next block:

  return FirstTextNodeInCurrentBlock(aFilteredIter);
}

// static
nsresult TextServicesDocument::FirstTextNodeInNextBlock(
    FilteredContentIterator* aFilteredIter) {
  bool crossedBlockBoundary = false;

  NS_ENSURE_TRUE(aFilteredIter, NS_ERROR_NULL_POINTER);

  ClearDidSkip(aFilteredIter);

  RefPtr<Text> previousTextNode;
  while (!aFilteredIter->IsDone()) {
    if (nsCOMPtr<nsIContent> content =
            aFilteredIter->GetCurrentNode()->IsContent()
                ? aFilteredIter->GetCurrentNode()->AsContent()
                : nullptr) {
      if (content->IsText()) {
        if (crossedBlockBoundary ||
            (previousTextNode && !TextServicesDocument::HasSameBlockNodeParent(
                                     *previousTextNode, *content->AsText()))) {
          break;
        }
        previousTextNode = content->AsText();
      }
      // We don't observe layout updates, therefore, we should consider whether
      // block or inline only with the default definition of the element.
      else if (!crossedBlockBoundary &&
               (HTMLEditUtils::IsBlockElement(
                    *content, BlockInlineCheck::UseHTMLDefaultStyle) ||
                content->IsHTMLElement(nsGkAtoms::br))) {
        crossedBlockBoundary = true;
      }
    }

    aFilteredIter->Next();

    if (!crossedBlockBoundary && DidSkip(aFilteredIter)) {
      crossedBlockBoundary = true;
    }
  }

  return NS_OK;
}

nsresult TextServicesDocument::GetFirstTextNodeInPrevBlock(
    nsIContent** aContent) {
  NS_ENSURE_TRUE(aContent, NS_ERROR_NULL_POINTER);

  *aContent = nullptr;

  // Save the iterator's current content node so we can restore
  // it when we are done:

  nsINode* node = mFilteredIter->GetCurrentNode();

  nsresult rv = FirstTextNodeInPrevBlock(mFilteredIter);

  if (NS_FAILED(rv)) {
    // Try to restore the iterator before returning.
    mFilteredIter->PositionAt(node);
    return rv;
  }

  if (!mFilteredIter->IsDone()) {
    nsCOMPtr<nsIContent> current =
        mFilteredIter->GetCurrentNode()->IsContent()
            ? mFilteredIter->GetCurrentNode()->AsContent()
            : nullptr;
    current.forget(aContent);
  }

  // Restore the iterator:

  return mFilteredIter->PositionAt(node);
}

nsresult TextServicesDocument::GetFirstTextNodeInNextBlock(
    nsIContent** aContent) {
  NS_ENSURE_TRUE(aContent, NS_ERROR_NULL_POINTER);

  *aContent = nullptr;

  // Save the iterator's current content node so we can restore
  // it when we are done:

  nsINode* node = mFilteredIter->GetCurrentNode();

  nsresult rv = FirstTextNodeInNextBlock(mFilteredIter);

  if (NS_FAILED(rv)) {
    // Try to restore the iterator before returning.
    mFilteredIter->PositionAt(node);
    return rv;
  }

  if (!mFilteredIter->IsDone()) {
    nsCOMPtr<nsIContent> current =
        mFilteredIter->GetCurrentNode()->IsContent()
            ? mFilteredIter->GetCurrentNode()->AsContent()
            : nullptr;
    current.forget(aContent);
  }

  // Restore the iterator:
  return mFilteredIter->PositionAt(node);
}

Result<TextServicesDocument::IteratorStatus, nsresult>
TextServicesDocument::OffsetEntryArray::Init(
    FilteredContentIterator& aFilteredIter, IteratorStatus aIteratorStatus,
    dom::Range* aIterRange, nsAString* aAllTextInBlock /* = nullptr */) {
  Clear();

  if (aAllTextInBlock) {
    aAllTextInBlock->Truncate();
  }

  if (aIteratorStatus == IteratorStatus::eDone) {
    return IteratorStatus::eDone;
  }

  // If we have an aIterRange, retrieve the endpoints so
  // they can be used in the while loop below to trim entries
  // for text nodes that are partially selected by aIterRange.

  nsCOMPtr<nsINode> rngStartNode, rngEndNode;
  uint32_t rngStartOffset = 0, rngEndOffset = 0;
  if (aIterRange) {
    nsresult rv = TextServicesDocument::GetRangeEndPoints(
        aIterRange, getter_AddRefs(rngStartNode), &rngStartOffset,
        getter_AddRefs(rngEndNode), &rngEndOffset);
    if (NS_FAILED(rv)) {
      NS_WARNING("TextServicesDocument::GetRangeEndPoints() failed");
      return Err(rv);
    }
  }

  // The text service could have added text nodes to the beginning
  // of the current block and called this method again. Make sure
  // we really are at the beginning of the current block:

  nsresult rv =
      TextServicesDocument::FirstTextNodeInCurrentBlock(&aFilteredIter);
  if (NS_FAILED(rv)) {
    NS_WARNING("TextServicesDocument::FirstTextNodeInCurrentBlock() failed");
    return Err(rv);
  }

  TextServicesDocument::ClearDidSkip(&aFilteredIter);

  uint32_t offset = 0;
  RefPtr<Text> firstTextNode, previousTextNode;
  while (!aFilteredIter.IsDone()) {
    if (nsCOMPtr<nsIContent> content =
            aFilteredIter.GetCurrentNode()->IsContent()
                ? aFilteredIter.GetCurrentNode()->AsContent()
                : nullptr) {
      // We don't observe layout updates, therefore, we should consider whether
      // block or inline only with the default definition of the element.
      if (HTMLEditUtils::IsBlockElement(
              *content, BlockInlineCheck::UseHTMLDefaultStyle) ||
          content->IsHTMLElement(nsGkAtoms::br)) {
        break;
      }
      if (content->IsText()) {
        if (previousTextNode && !TextServicesDocument::HasSameBlockNodeParent(
                                    *previousTextNode, *content->AsText())) {
          break;
        }

        nsString str;
        content->AsText()->GetNodeValue(str);

        // Add an entry for this text node into the offset table:

        UniquePtr<OffsetEntry>& entry =
            *AppendElement(std::make_unique<OffsetEntry>(*content->AsText(),
                                                         offset, str.Length()));
        LockOffsetEntryArrayLengthInDebugBuild(observer, *this);

        // If one or both of the endpoints of the iteration range
        // are in the text node for this entry, make sure the entry
        // only accounts for the portion of the text node that is
        // in the range.

        uint32_t startOffset = 0;
        uint32_t endOffset = str.Length();
        bool adjustStr = false;

        if (entry->mTextNode == rngStartNode) {
          entry->mOffsetInTextNode = startOffset = rngStartOffset;
          adjustStr = true;
        }

        if (entry->mTextNode == rngEndNode) {
          endOffset = rngEndOffset;
          adjustStr = true;
        }

        if (adjustStr) {
          entry->mLength = endOffset - startOffset;
          str = Substring(str, startOffset, entry->mLength);
        }

        offset += str.Length();

        if (aAllTextInBlock) {
          // Append the text node's string to the output string:
          if (!firstTextNode) {
            *aAllTextInBlock = str;
          } else {
            *aAllTextInBlock += str;
          }
        }

        previousTextNode = content->AsText();

        if (!firstTextNode) {
          firstTextNode = content->AsText();
        }
      }
    }

    aFilteredIter.Next();

    if (TextServicesDocument::DidSkip(&aFilteredIter)) {
      break;
    }
  }

  if (firstTextNode) {
    // Always leave the iterator pointing at the first
    // text node of the current block!
    aFilteredIter.PositionAt(firstTextNode);
    return aIteratorStatus;
  }

  // If we never ran across a text node, the iterator
  // might have been pointing to something invalid to
  // begin with.
  return IteratorStatus::eDone;
}

void TextServicesDocument::OffsetEntryArray::RemoveInvalidElements() {
  for (size_t i = 0; i < Length();) {
    if (ElementAt(i)->mIsValid) {
      i++;
      continue;
    }

    RemoveElementAt(i);
    if (!mSelection.IsSet()) {
      continue;
    }
    if (mSelection.StartIndex() == i) {
      NS_ASSERTION(false, "What should we do in this case?");
      mSelection.Reset();
    } else if (mSelection.StartIndex() > i) {
      MOZ_DIAGNOSTIC_ASSERT(mSelection.StartIndex() - 1 < Length());
      MOZ_DIAGNOSTIC_ASSERT(mSelection.EndIndex() - 1 < Length());
      mSelection.SetIndexes(mSelection.StartIndex() - 1,
                            mSelection.EndIndex() - 1);
    } else if (mSelection.EndIndex() >= i) {
      MOZ_DIAGNOSTIC_ASSERT(mSelection.EndIndex() - 1 < Length());
      mSelection.SetIndexes(mSelection.StartIndex(), mSelection.EndIndex() - 1);
    }
  }
}

nsresult TextServicesDocument::OffsetEntryArray::SplitElementAt(
    size_t aIndex, uint32_t aOffsetInTextNode) {
  OffsetEntry* leftEntry = ElementAt(aIndex).get();
  MOZ_ASSERT(leftEntry);
  NS_ASSERTION((aOffsetInTextNode > 0), "aOffsetInTextNode == 0");
  NS_ASSERTION((aOffsetInTextNode < leftEntry->mLength),
               "aOffsetInTextNode >= mLength");

  if (aOffsetInTextNode < 1 || aOffsetInTextNode >= leftEntry->mLength) {
    return NS_ERROR_FAILURE;
  }

  const uint32_t oldLength = leftEntry->mLength - aOffsetInTextNode;

  // XXX(Bug 1631371) Check if this should use a fallible operation as it
  // pretended earlier.
  UniquePtr<OffsetEntry>& rightEntry = *InsertElementAt(
      aIndex + 1,
      std::make_unique<OffsetEntry>(leftEntry->mTextNode,
                                    leftEntry->mOffsetInTextInBlock + oldLength,
                                    aOffsetInTextNode));
  LockOffsetEntryArrayLengthInDebugBuild(observer, *this);
  leftEntry->mLength = oldLength;
  rightEntry->mOffsetInTextNode = leftEntry->mOffsetInTextNode + oldLength;

  return NS_OK;
}

Maybe<size_t> TextServicesDocument::OffsetEntryArray::FirstIndexOf(
    const Text& aTextNode) const {
  for (size_t i = 0; i < Length(); i++) {
    if (ElementAt(i)->mTextNode == &aTextNode) {
      return Some(i);
    }
  }
  return Nothing();
}

// Spellchecker code has this. See bug 211343
#define IS_NBSP_CHAR(c) (((unsigned char)0xa0) == (c))

Result<EditorDOMRangeInTexts, nsresult>
TextServicesDocument::OffsetEntryArray::FindWordRange(
    nsAString& aAllTextInBlock, const EditorRawDOMPoint& aStartPointToScan) {
  MOZ_ASSERT(aStartPointToScan.IsInTextNode());
  // It's assumed that aNode is a text node. The first thing
  // we do is get its index in the offset table so we can
  // calculate the dom point's string offset.
  Maybe<size_t> maybeEntryIndex =
      FirstIndexOf(*aStartPointToScan.ContainerAs<Text>());
  if (NS_WARN_IF(maybeEntryIndex.isNothing())) {
    NS_WARNING(
        "TextServicesDocument::OffsetEntryArray::FirstIndexOf() didn't find "
        "entries");
    return Err(NS_ERROR_FAILURE);
  }

  // Next we map offset into a string offset.

  const UniquePtr<OffsetEntry>& entry = ElementAt(*maybeEntryIndex);
  LockOffsetEntryArrayLengthInDebugBuild(observer, *this);
  uint32_t strOffset = entry->mOffsetInTextInBlock +
                       aStartPointToScan.Offset() - entry->mOffsetInTextNode;

  // Now we use the word breaker to find the beginning and end
  // of the word from our calculated string offset.

  const char16_t* str = aAllTextInBlock.BeginReading();
  MOZ_ASSERT(strOffset <= aAllTextInBlock.Length(),
             "The string offset shouldn't be greater than the string length!");

  intl::WordRange res = intl::WordBreaker::FindWord(aAllTextInBlock, strOffset);

  // Strip out the NBSPs at the ends
  while (res.mBegin <= res.mEnd && IS_NBSP_CHAR(str[res.mBegin])) {
    res.mBegin++;
  }
  if (str[res.mEnd] == static_cast<char16_t>(0x20)) {
    uint32_t realEndWord = res.mEnd - 1;
    while (realEndWord > res.mBegin && IS_NBSP_CHAR(str[realEndWord])) {
      realEndWord--;
    }
    if (realEndWord < res.mEnd - 1) {
      res.mEnd = realEndWord + 1;
    }
  }

  // Now that we have the string offsets for the beginning
  // and end of the word, run through the offset table and
  // convert them back into dom points.

  EditorDOMPointInText wordStart, wordEnd;
  size_t lastIndex = Length() - 1;
  for (size_t i = 0; i <= lastIndex; i++) {
    // Check to see if res.mBegin is within the range covered
    // by this entry. Note that if res.mBegin is after the last
    // character covered by this entry, we will use the next
    // entry if there is one.
    const UniquePtr<OffsetEntry>& entry = ElementAt(i);
    LockOffsetEntryArrayLengthInDebugBuild(observer, *this);
    if (entry->mOffsetInTextInBlock <= res.mBegin &&
        (res.mBegin < entry->EndOffsetInTextInBlock() ||
         (res.mBegin == entry->EndOffsetInTextInBlock() && i == lastIndex))) {
      wordStart.Set(entry->mTextNode, entry->mOffsetInTextNode + res.mBegin -
                                          entry->mOffsetInTextInBlock);
    }

    // Check to see if res.mEnd is within the range covered
    // by this entry.
    if (entry->mOffsetInTextInBlock <= res.mEnd &&
        res.mEnd <= entry->EndOffsetInTextInBlock()) {
      if (res.mBegin == res.mEnd &&
          res.mEnd == entry->EndOffsetInTextInBlock() && i != lastIndex) {
        // Wait for the next round so that we use the same entry
        // we did for aWordStartNode.
        continue;
      }

      wordEnd.Set(entry->mTextNode, entry->mOffsetInTextNode + res.mEnd -
                                        entry->mOffsetInTextInBlock);
      break;
    }
  }

  return EditorDOMRangeInTexts(wordStart, wordEnd);
}

/**
 * nsIEditActionListener implementation:
 *   Don't implement the behavior directly here.  The methods won't be called
 *   if the instance is created for inline spell checker created for editor.
 *   If you need to listen a new edit action, you need to add similar
 *   non-virtual method and you need to call it from EditorBase directly.
 */

NS_IMETHODIMP
TextServicesDocument::DidDeleteNode(nsINode* aChild, nsresult aResult) {
  if (NS_WARN_IF(NS_FAILED(aResult)) || NS_WARN_IF(!aChild) ||
      !aChild->IsContent()) {
    return NS_OK;
  }
  DidDeleteContent(*aChild->AsContent());
  return NS_OK;
}

NS_IMETHODIMP TextServicesDocument::DidJoinContents(
    const EditorRawDOMPoint& aJoinedPoint, const nsINode* aRemovedNode) {
  if (MOZ_UNLIKELY(NS_WARN_IF(!aJoinedPoint.IsSetAndValid()) ||
                   NS_WARN_IF(!aRemovedNode->IsContent()))) {
    return NS_OK;
  }
  DidJoinContents(aJoinedPoint, *aRemovedNode->AsContent());
  return NS_OK;
}

NS_IMETHODIMP
TextServicesDocument::DidInsertText(CharacterData* /*aTextNode*/,
                                    int32_t /*aOffset*/,
                                    const nsAString& /*aString*/,
                                    nsresult /*aResult*/) {
  return NS_OK;
}

NS_IMETHODIMP
TextServicesDocument::WillDeleteText(CharacterData* /*aTextNode*/,
                                     int32_t /*aOffset*/, int32_t /*aLength*/) {
  return NS_OK;
}

NS_IMETHODIMP
TextServicesDocument::WillDeleteRanges(
    const nsTArray<RefPtr<dom::Range>>& /*aRangesToDelete*/) {
  return NS_OK;
}

#undef LockOffsetEntryArrayLengthInDebugBuild

}  // namespace mozilla
