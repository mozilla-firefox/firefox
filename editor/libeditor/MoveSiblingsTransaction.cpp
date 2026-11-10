/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "MoveNodeTransaction.h"

#include "EditorBase.h"           // for EditorBase
#include "EditorDOMAPIWrapper.h"  // for AutoNodeAPIWrapper
#include "EditorDOMPoint.h"       // for EditorDOMPoint
#include "HTMLEditor.h"           // for HTMLEditor
#include "HTMLEditUtils.h"        // for HTMLEditUtils

#include "mozilla/Likely.h"
#include "mozilla/Logging.h"
#include "mozilla/ToString.h"

#include "nsDebug.h"     // for NS_WARNING, etc.
#include "nsError.h"     // for NS_ERROR_NULL_POINTER, etc.
#include "nsIContent.h"  // for nsIContent
#include "nsString.h"    // for nsString

namespace mozilla {

using namespace dom;

template already_AddRefed<MoveSiblingsTransaction>
MoveSiblingsTransaction::MaybeCreate(HTMLEditor& aHTMLEditor,
                                     nsIContent& aFirstContentToMove,
                                     nsIContent& aLastContentToMove,
                                     const EditorDOMPoint& aPointToInsert);
template already_AddRefed<MoveSiblingsTransaction>
MoveSiblingsTransaction::MaybeCreate(HTMLEditor& aHTMLEditor,
                                     nsIContent& aFirstContentToMove,
                                     nsIContent& aLastContentToMove,
                                     const EditorRawDOMPoint& aPointToInsert);

// static
template <typename PT, typename CT>
already_AddRefed<MoveSiblingsTransaction> MoveSiblingsTransaction::MaybeCreate(
    HTMLEditor& aHTMLEditor, nsIContent& aFirstContentToMove,
    nsIContent& aLastContentToMove,
    const EditorDOMPointBase<PT, CT>& aPointToInsert) {
  if (NS_WARN_IF(!aFirstContentToMove.GetParentNode()) ||
      NS_WARN_IF(&aFirstContentToMove == &aLastContentToMove) ||
      NS_WARN_IF(aFirstContentToMove.GetParentNode() !=
                 aLastContentToMove.GetParentNode()) ||
      NS_WARN_IF(!aPointToInsert.IsSet())) {
    return nullptr;
  }

  // The destination should be editable, but it may be in an orphan node
  // or sub-tree to reduce number of DOM mutation events.  In such case,
  // we're okay to move a node into the non-editable content because we
  // can assume that the caller will insert it into an editable element.
  if (NS_WARN_IF(aPointToInsert.IsInComposedDoc() &&
                 !HTMLEditUtils::IsSimplyEditableNode(
                     *aPointToInsert.GetContainer()))) {
    return nullptr;
  }
  const uint32_t numberOfSiblings = [&]() -> uint32_t {
    uint32_t num = 1;
    for (nsIContent* content = aFirstContentToMove.GetNextSibling(); content;
         content = content->GetNextSibling()) {
      // TODO: We should not allow to move a node to improper container element.
      //       However, this is currently used to move invalid parent while
      //       processing the nodes.  Therefore, treating the case as error
      //       breaks a lot.
      if (NS_WARN_IF(content->IsInComposedDoc() &&
                     !HTMLEditUtils::IsRemovableNode(*content))) {
        return 0;
      }
      num++;
      if (content == &aLastContentToMove) {
        return num;
      }
    }
    return 0;
  }();
  if (NS_WARN_IF(!numberOfSiblings)) {
    return nullptr;
  }
  RefPtr<MoveSiblingsTransaction> transaction = new MoveSiblingsTransaction(
      aHTMLEditor, aFirstContentToMove, aLastContentToMove, numberOfSiblings,
      aPointToInsert);
  return transaction.forget();
}

template <typename PT, typename CT>
MoveSiblingsTransaction::MoveSiblingsTransaction(
    HTMLEditor& aHTMLEditor, nsIContent& aFirstContentToMove,
    nsIContent& aLastContentToMove, uint32_t aNumberOfSiblings,
    const EditorDOMPointBase<PT, CT>& aPointToInsert)
    : MoveNodeTransactionBase(aHTMLEditor, aLastContentToMove,
                              aPointToInsert.template To<EditorRawDOMPoint>()) {
  mSiblingsToMove.SetCapacity(aNumberOfSiblings);
  for (nsIContent* content = &aFirstContentToMove; content;
       content = content->GetNextSibling()) {
    mSiblingsToMove.AppendElement(*content);
    if (content == &aLastContentToMove) {
      break;
    }
  }
  MOZ_ASSERT(mSiblingsToMove.Length() == aNumberOfSiblings);
}

std::ostream& operator<<(std::ostream& aStream,
                         const MoveSiblingsTransaction& aTransaction) {
  auto DumpNodeDetails = [&](const nsINode* aNode) {
    if (aNode) {
      if (aNode->IsText()) {
        nsAutoString data;
        aNode->AsText()->GetData(data);
        aStream << " (#text \"" << NS_ConvertUTF16toUTF8(data).get() << "\")";
      } else {
        aStream << " (" << *aNode << ")";
      }
    }
  };
  aStream << "{ mSiblingsToMove[0]=" << aTransaction.mSiblingsToMove[0].get();
  DumpNodeDetails(aTransaction.mSiblingsToMove[0]);
  aStream << ", mSiblingsToMove[" << aTransaction.mSiblingsToMove.Length() - 1
          << aTransaction.mSiblingsToMove.LastElement().get();
  DumpNodeDetails(aTransaction.mSiblingsToMove.LastElement());
  aStream << ", mContainer=" << aTransaction.mContainer.get();
  DumpNodeDetails(aTransaction.mContainer);
  aStream << ", mReference=" << aTransaction.mReference.get();
  DumpNodeDetails(aTransaction.mReference);
  aStream << ", mOldContainer=" << aTransaction.mOldContainer.get();
  DumpNodeDetails(aTransaction.mOldContainer);
  aStream << ", mOldNextSibling=" << aTransaction.mOldNextSibling.get();
  DumpNodeDetails(aTransaction.mOldNextSibling);
  aStream << ", mHTMLEditor=" << aTransaction.mHTMLEditor.get() << " }";
  return aStream;
}

NS_IMPL_CYCLE_COLLECTION_INHERITED(MoveSiblingsTransaction, EditTransactionBase,
                                   mSiblingsToMove)

NS_IMPL_ADDREF_INHERITED(MoveSiblingsTransaction, EditTransactionBase)
NS_IMPL_RELEASE_INHERITED(MoveSiblingsTransaction, EditTransactionBase)
NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(MoveSiblingsTransaction)
NS_INTERFACE_MAP_END_INHERITING(EditTransactionBase)

NS_IMETHODIMP MoveSiblingsTransaction::DoTransaction() {
  MOZ_LOG(GetLogModule(), LogLevel::Info,
          ("%p MoveSiblingsTransaction::%s this=%s", this, __FUNCTION__,
           ToString(*this).c_str()));
  mDone = true;
  return DoTransactionInternal();
}

nsresult MoveSiblingsTransaction::DoTransactionInternal() {
  MOZ_DIAGNOSTIC_ASSERT(mHTMLEditor);
  MOZ_DIAGNOSTIC_ASSERT(!mSiblingsToMove.IsEmpty());
  MOZ_DIAGNOSTIC_ASSERT(mContainer);
  MOZ_DIAGNOSTIC_ASSERT(mOldContainer);

  {
    const OwningNonNull<HTMLEditor> htmlEditor = *mHTMLEditor;
    const OwningNonNull<nsINode> newContainer = *mContainer;
    const nsCOMPtr<nsIContent> newNextSibling = mReference;
    const CopyableAutoTArray<OwningNonNull<nsIContent>, 64> siblingsToMove(
        mSiblingsToMove);
    AutoMoveNodeSelNotify notifier(
        htmlEditor->RangeUpdaterRef(),
        mReference ? EditorRawDOMPoint(mReference)
                   : EditorRawDOMPoint::AtEndOf(*newContainer));
    // First, remove all nodes from the DOM if they are removable.  Then,
    // IMEContentObserver can use cache to avoid to compute the start offset of
    // each deleting text.
    RemoveAllSiblingsToMove(htmlEditor, siblingsToMove, notifier);
    // Next, insert all removed nodes into the DOM.  Then, IMEContentObserver
    // can use cache to avoid to compute the start offset of each inserting
    // text.
    InsertAllSiblingsToMove(htmlEditor, siblingsToMove, newContainer,
                            newNextSibling, notifier);
  }
  return NS_WARN_IF(mHTMLEditor->Destroyed()) ? NS_ERROR_EDITOR_DESTROYED
                                              : NS_OK;
}

NS_IMETHODIMP MoveSiblingsTransaction::UndoTransaction() {
  MOZ_LOG(GetLogModule(), LogLevel::Info,
          ("%p MoveSiblingsTransaction::%s this=%s", this, __FUNCTION__,
           ToString(*this).c_str()));

  mDone = false;

  if (NS_WARN_IF(!mHTMLEditor) || NS_WARN_IF(mSiblingsToMove.IsEmpty()) ||
      NS_WARN_IF(!IsSiblingsToMoveValid()) || NS_WARN_IF(!mOldContainer)) {
    // Perhaps, nulled-out by the cycle collector.
    return NS_ERROR_FAILURE;
  }

  // If the original point has been changed, refer mOldNextSibling if it's
  // reasonable.  Otherwise, use end of the old container.
  if (mOldNextSibling && mOldContainer != mOldNextSibling->GetParentNode()) {
    // TODO: Check whether the new container is proper one for containing
    //       content in mSiblingsToMove.  However, there are few testcases so
    //       that we shouldn't change here without creating a lot of undo tests.
    if (mOldNextSibling->GetParentNode() &&
        (mOldNextSibling->IsInComposedDoc() ||
         !mOldContainer->IsInComposedDoc())) {
      mOldContainer = mOldNextSibling->GetParentNode();
    } else {
      mOldNextSibling = nullptr;  // end of mOldContainer
    }
  }

  if (MOZ_UNLIKELY(mOldContainer->IsInComposedDoc() &&
                   !HTMLEditUtils::IsSimplyEditableNode(*mOldContainer))) {
    NS_WARNING(
        "MoveSiblingsTransaction::UndoTransaction() couldn't move the "
        "content into the old container due to non-editable one");
    return NS_ERROR_FAILURE;
  }

  // And store the latest node which should be referred at redoing.
  mContainer = mSiblingsToMove.LastElement()->GetParentNode();
  mReference = mSiblingsToMove.LastElement()->GetNextSibling();

  {
    const OwningNonNull<HTMLEditor> htmlEditor = *mHTMLEditor;
    const OwningNonNull<nsINode> oldContainer = *mOldContainer;
    const nsCOMPtr<nsIContent> oldNextSibling = mOldNextSibling;
    const CopyableAutoTArray<OwningNonNull<nsIContent>, 64> siblingsToMove(
        mSiblingsToMove);
    AutoMoveNodeSelNotify notifier(
        htmlEditor->RangeUpdaterRef(),
        oldNextSibling ? EditorRawDOMPoint(oldNextSibling)
                       : EditorRawDOMPoint::AtEndOf(*oldContainer));
    // First, remove all nodes from the DOM if they are removable.  Then,
    // IMEContentObserver can use cache to avoid to compute the start offset of
    // each deleting text.
    RemoveAllSiblingsToMove(htmlEditor, siblingsToMove, notifier);
    // Next, insert all removed nodes into the DOM.  Then, IMEContentObserver
    // can use cache to avoid to compute the start offset of each inserting
    // text.
    InsertAllSiblingsToMove(htmlEditor, siblingsToMove, oldContainer,
                            oldNextSibling, notifier);
  }

  return NS_WARN_IF(mHTMLEditor->Destroyed()) ? NS_ERROR_EDITOR_DESTROYED
                                              : NS_OK;
}

NS_IMETHODIMP MoveSiblingsTransaction::RedoTransaction() {
  MOZ_LOG(GetLogModule(), LogLevel::Info,
          ("%p MoveSiblingsTransaction::%s this=%s", this, __FUNCTION__,
           ToString(*this).c_str()));

  mDone = true;

  if (NS_WARN_IF(!mHTMLEditor) || NS_WARN_IF(mSiblingsToMove.IsEmpty()) ||
      NS_WARN_IF(!IsSiblingsToMoveValid()) || NS_WARN_IF(!mContainer)) {
    // Perhaps, nulled-out by the cycle collector.
    return NS_ERROR_FAILURE;
  }

  // If the inserting point has been changed, refer mReference if it's
  // reasonable.  Otherwise, use end of the container.
  if (mReference && mContainer != mReference->GetParentNode()) {
    // TODO: Check whether the new container is proper one for containing
    //       mContentToMove.  However, there are few testcases so that we
    //       shouldn't change here without creating a lot of redo tests.
    if (mReference->GetParentNode() &&
        (mReference->IsInComposedDoc() || !mContainer->IsInComposedDoc())) {
      mContainer = mReference->GetParentNode();
    } else {
      mReference = nullptr;  // end of mContainer
    }
  }

  if (MOZ_UNLIKELY(mContainer->IsInComposedDoc() &&
                   !HTMLEditUtils::IsSimplyEditableNode(*mContainer))) {
    NS_WARNING(
        "MoveSiblingsTransaction::RedoTransaction() couldn't move the "
        "content into the new container due to non-editable one");
    return NS_ERROR_FAILURE;
  }

  // And store the latest node which should be back.
  mOldContainer = mSiblingsToMove.LastElement()->GetParentNode();
  mOldNextSibling = mSiblingsToMove.LastElement()->GetNextSibling();

  nsresult rv = DoTransactionInternal();
  if (NS_FAILED(rv)) {
    NS_WARNING("MoveSiblingsTransaction::DoTransactionInternal() failed");
    return rv;
  }
  return NS_OK;
}

nsIContent* MoveSiblingsTransaction::GetFirstMovedContent() const {
  nsINode* const expectedContainer = mDone ? mContainer : mOldContainer;
  for (const OwningNonNull<nsIContent>& content : mSiblingsToMove) {
    if (MOZ_LIKELY(content->GetParentNode() == expectedContainer)) {
      return content;
    }
  }
  return nullptr;
}

nsIContent* MoveSiblingsTransaction::GetLastMovedContent() const {
  nsINode* const expectedContainer = mDone ? mContainer : mOldContainer;
  for (const OwningNonNull<nsIContent>& content : Reversed(mSiblingsToMove)) {
    if (MOZ_LIKELY(content->GetParentNode() == expectedContainer)) {
      return content;
    }
  }
  return nullptr;
}

void MoveSiblingsTransaction::RemoveAllSiblingsToMove(
    HTMLEditor& aHTMLEditor,
    const nsTArray<OwningNonNull<nsIContent>>& aClonedSiblingsToMove,
    AutoMoveNodeSelNotify& aNotifier) const {
  // Be aware, if we're undoing or redoing, some aClonedSiblingsToMove may not
  // be the adjacent sibling of prev/next element in the array.  Therefore, we
  // may need to compute the index within the expensive path.

  // First, we need to make AutoMoveNodeSelNotify instances store all indices of
  // the moving content nodes.
  {
    for (const OwningNonNull<nsIContent>& contentToMove :
         aClonedSiblingsToMove) {
      if (contentToMove->IsInComposedDoc() &&
          !HTMLEditUtils::IsRemovableNode(contentToMove)) {
        continue;
      }
      aNotifier.AppendContentWhichWillBeMoved(contentToMove);
    }
  }
  // Then, remove all nodes unless not removable.
  for (const size_t i : IntegerRange(aNotifier.MovingContentCount())) {
    nsIContent* const contentToMove = aNotifier.GetContentAt(i);
    MOZ_ASSERT(contentToMove);
    AutoNodeAPIWrapper nodeWrapper(aHTMLEditor,
                                   // MOZ_KnownLive because it's guaranteed by
                                   // both notifier and aClonedSiblingsToMove.
                                   MOZ_KnownLive(*contentToMove));
    if (NS_FAILED(nodeWrapper.Remove())) {
      NS_WARNING("AutoNodeAPIWrapper::Remove() failed, but ignored");
    } else {
      NS_WARNING_ASSERTION(
          nodeWrapper.IsExpectedResult(),
          "Temporarily removing node caused other mutations, but ignored");
    }
  }
}

nsresult MoveSiblingsTransaction::InsertAllSiblingsToMove(
    HTMLEditor& aHTMLEditor,
    const nsTArray<OwningNonNull<nsIContent>>& /*aClonedSiblingsToMove*/,
    nsINode& aParentNode, nsIContent* aReferenceNode,
    AutoMoveNodeSelNotify& aNotifier) const {
  MOZ_ASSERT(mHTMLEditor);
  nsresult rv = NS_SUCCESS_DOM_NO_OPERATION;
  for (const size_t i : IntegerRange(aNotifier.MovingContentCount())) {
    nsIContent* const contentToMove = aNotifier.GetContentAt(i);
    MOZ_ASSERT(contentToMove);
    if (Element* const elementToMove = Element::FromNodeOrNull(contentToMove)) {
      if (!elementToMove->HasAttr(nsGkAtoms::mozdirty)) {
        nsresult rvMarkElementDirty = aHTMLEditor.MarkElementDirty(
            MOZ_KnownLive(*contentToMove->AsElement()));
        NS_WARNING_ASSERTION(
            NS_SUCCEEDED(rvMarkElementDirty),
            "EditorBase::MarkElementDirty() failed, but ignored");
        (void)rvMarkElementDirty;
      }
    }

    AutoNodeAPIWrapper nodeWrapper(aHTMLEditor, aParentNode);
    // MOZ_KnownLive because of guaranteed by both aNotifier and
    // aClonedSiblingsToMove.
    nsresult rvInner =
        nodeWrapper.InsertBefore(MOZ_KnownLive(*contentToMove), aReferenceNode);
    if (NS_FAILED(rvInner)) {
      NS_WARNING("AutoNodeAPIWrapper::InsertBefore() failed");
      rv = rvInner;
    } else {
      NS_WARNING_ASSERTION(nodeWrapper.IsExpectedResult(),
                           "Moving a node caused other mutations, but ignored");
    }
  }

  Document* const document = aHTMLEditor.GetDocument();
  for (const size_t i : IntegerRange(aNotifier.MovingContentCount())) {
    nsIContent* const content = aNotifier.GetContentAt(i);
    MOZ_ASSERT(content);
    if (MOZ_LIKELY(content->GetParentNode() &&
                   content->OwnerDoc() == document)) {
      aNotifier.DidMoveContent(*content);
    }
  }
  return NS_WARN_IF(aHTMLEditor.Destroyed()) ? NS_ERROR_EDITOR_DESTROYED : rv;
}

}  // namespace mozilla
