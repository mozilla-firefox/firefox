/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "EditorBase.h"

#include <stdio.h>   // for nullptr, stdout
#include <string.h>  // for strcmp

#include "AutoClonedRangeArray.h"  // for AutoClonedRangeArray and AutoClonedSelectionRangeArray
#include "AutoSelectionRestorer.h"
#include "ChangeAttributeTransaction.h"
#include "CompositionTransaction.h"
#include "DeleteContentTransactionBase.h"
#include "DeleteMultipleRangesTransaction.h"
#include "DeleteNodeTransaction.h"
#include "DeleteRangeTransaction.h"
#include "DeleteTextTransaction.h"
#include "EditAction.h"           // for EditSubAction
#include "EditorDOMAPIWrapper.h"  // for AutoCharacterDataAPIWrapper, etc
#include "EditorDOMPoint.h"       // for EditorDOMPoint
#include "EditorForwards.h"
#include "EditorUtils.h"          // for various helper classes.
#include "EditTransactionBase.h"  // for EditTransactionBase
#include "EditorEventListener.h"  // for EditorEventListener
#include "HTMLEditor.h"           // for HTMLEditor
#include "HTMLEditorInlines.h"
#include "HTMLEditUtils.h"           // for HTMLEditUtils
#include "InsertNodeTransaction.h"   // for InsertNodeTransaction
#include "InsertTextTransaction.h"   // for InsertTextTransaction
#include "JoinNodesTransaction.h"    // for JoinNodesTransaction
#include "PlaceholderTransaction.h"  // for PlaceholderTransaction
#include "SplitNodeTransaction.h"    // for SplitNodeTransaction
#include "TextEditor.h"              // for TextEditor

#include "ErrorList.h"
#include "gfxFontUtils.h"  // for gfxFontUtils
#include "mozilla/Assertions.h"
#include "mozilla/AsyncEventDispatcher.h"
#include "mozilla/EditorDOMPoint.h"
#include "mozilla/intl/BidiEmbeddingLevel.h"
#include "mozilla/BasePrincipal.h"            // for BasePrincipal
#include "mozilla/ComposerCommandsUpdater.h"  // for ComposerCommandsUpdater
#include "mozilla/ContentEvents.h"            // for InternalClipboardEvent
#include "mozilla/DebugOnly.h"                // for DebugOnly
#include "mozilla/EditorSpellCheck.h"         // for EditorSpellCheck
#include "mozilla/Encoding.h"  // for Encoding (used in Document::GetDocumentCharacterSet)
#include "mozilla/EventDispatcher.h"        // for EventChainPreVisitor, etc.
#include "mozilla/FlushType.h"              // for FlushType::Frames
#include "mozilla/IMEContentObserver.h"     // for IMEContentObserver
#include "mozilla/IMEStateManager.h"        // for IMEStateManager
#include "mozilla/InputEventOptions.h"      // for InputEventOptions
#include "mozilla/IntegerRange.h"           // for IntegerRange
#include "mozilla/Logging.h"                //for MOZ_LOG
#include "mozilla/mozalloc.h"               // for operator new, etc.
#include "mozilla/mozInlineSpellChecker.h"  // for mozInlineSpellChecker
#include "mozilla/mozSpellChecker.h"        // for mozSpellChecker
#include "mozilla/Preferences.h"            // for Preferences
#include "mozilla/PresShell.h"              // for PresShell
#include "mozilla/RangeBoundary.h"       // for RawRangeBoundary, RangeBoundary
#include "mozilla/ScopeExit.h"           // for MakeScopeExit
#include "mozilla/Services.h"            // for GetObserverService
#include "mozilla/StaticPrefs_bidi.h"    // for StaticPrefs::bidi_*
#include "mozilla/StaticPrefs_dom.h"     // for StaticPrefs::dom_*
#include "mozilla/StaticPrefs_editor.h"  // for StaticPrefs::editor_*
#include "mozilla/StaticPrefs_layout.h"  // for StaticPrefs::layout_*
#include "mozilla/TextComposition.h"     // for TextComposition
#include "mozilla/TextControlElement.h"  // for TextControlElement
#include "mozilla/TextInputListener.h"   // for TextInputListener
#include "mozilla/TextServicesDocument.h"  // for TextServicesDocument
#include "mozilla/TextEvents.h"
#include "mozilla/ToString.h"
#include "mozilla/TransactionManager.h"    // for TransactionManager
#include "mozilla/dom/AbstractRange.h"     // for AbstractRange
#include "mozilla/dom/Attr.h"              // for Attr
#include "mozilla/dom/BorrowedAttrInfo.h"  // for BorrowedAttrInfo
#include "mozilla/dom/BrowsingContext.h"   // for BrowsingContext
#include "mozilla/dom/CharacterData.h"     // for CharacterData
#include "mozilla/dom/ContentParent.h"     // for ContentParent
#include "mozilla/dom/DataTransfer.h"      // for DataTransfer
#include "mozilla/dom/Document.h"          // for Document
#include "mozilla/dom/DocumentInlines.h"   // for GetObservingPresShell
#include "mozilla/dom/DragEvent.h"         // for DragEvent
#include "mozilla/dom/EditContext.h"       // for EditContext
#include "mozilla/dom/Element.h"           // for Element, nsINode::AsElement
#include "mozilla/dom/EventTarget.h"       // for EventTarget
#include "mozilla/dom/HTMLBodyElement.h"
#include "mozilla/dom/HTMLBRElement.h"
#include "mozilla/dom/Range.h"        // for Range
#include "mozilla/dom/Selection.h"    // for Selection, etc.
#include "mozilla/dom/StaticRange.h"  // for StaticRange
#include "mozilla/dom/Text.h"
#include "mozilla/dom/Event.h"
#include "mozilla/Utf16.h"
#include "nsAString.h"                // for nsAString::Length, etc.
#include "nsCCUncollectableMarker.h"  // for nsCCUncollectableMarker
#include "nsCaret.h"                  // for nsCaret
#include "nsCaseTreatment.h"
#include "nsCharTraits.h"              // for mozilla::IsHighSurrogate, etc.
#include "nsContentUtils.h"            // for nsContentUtils
#include "nsCopySupport.h"             // for nsCopySupport
#include "nsDOMString.h"               // for DOMStringIsNull
#include "nsDebug.h"                   // for NS_WARNING, etc.
#include "nsError.h"                   // for NS_OK, etc.
#include "nsFocusManager.h"            // for nsFocusManager
#include "nsFrameSelection.h"          // for nsFrameSelection
#include "nsGenericHTMLElement.h"      // for nsGenericHTMLElement
#include "nsGkAtoms.h"                 // for nsGkAtoms, nsGkAtoms::dir
#include "nsIClipboard.h"              // for nsIClipboard
#include "nsIContent.h"                // for nsIContent
#include "nsIContentInlines.h"         // for nsINode::IsInDesignMode()
#include "nsIDocumentEncoder.h"        // for nsIDocumentEncoder
#include "nsIDocumentStateListener.h"  // for nsIDocumentStateListener
#include "nsIDocShell.h"               // for nsIDocShell
#include "nsIEditActionListener.h"     // for nsIEditActionListener
#include "nsIFrame.h"                  // for nsIFrame
#include "nsIInlineSpellChecker.h"     // for nsIInlineSpellChecker, etc.
#include "nsNameSpaceManager.h"        // for kNameSpaceID_None, etc.
#include "nsINode.h"                   // for nsINode, etc.
#include "nsIScriptElement.h"          // for nsIScriptElement
#include "nsISelectionController.h"    // for nsISelectionController, etc.
#include "nsISelectionDisplay.h"       // for nsISelectionDisplay, etc.
#include "nsISupports.h"               // for nsISupports
#include "nsISupportsUtils.h"          // for NS_ADDREF, NS_IF_ADDREF
#include "nsITransferable.h"           // for nsITransferable
#include "nsIWeakReference.h"          // for nsISupportsWeakReference
#include "nsIWidget.h"                 // for nsIWidget, IMEState, etc.
#include "nsPIDOMWindow.h"             // for nsPIDOMWindow
#include "nsPresContext.h"             // for nsPresContext
#include "nsReadableUtils.h"           // for EmptyString, ToNewCString
#include "nsString.h"                  // for nsAutoString, nsString, etc.
#include "nsStringFwd.h"               // for nsString
#include "nsStyleConsts.h"             // for StyleDirection::Rtl, etc.
#include "nsStyleStruct.h"             // for nsStyleDisplay, nsStyleText, etc.
#include "nsStyleStructFwd.h"          // for nsIFrame::StyleUIReset, etc.
#include "nsTextNode.h"                // for nsTextNode
#include "nsThreadUtils.h"             // for nsRunnable
#include "prtime.h"                    // for PR_Now

class nsIOutputStream;
class nsITransferable;

namespace mozilla {

using namespace dom;
using namespace widget;

using EmptyCheckOption = HTMLEditUtils::EmptyCheckOption;
using LeafNodeOption = HTMLEditUtils::LeafNodeOption;

static LazyLogModule gEventLog("EditorEvent");
static LazyLogModule gHTMLEditorEditActionStartLog("HTMLEditorEditActionStart");

LazyLogModule gTextInputLog("EditorTextInput");

/******************************************************************************
 * mozilla::EditorBase::AutoTextEditVerifier
 *****************************************************************************/
class MOZ_STACK_CLASS EditorBase::AutoTextEditVerifier {
 public:
  // NOTE: This constructor is marked as const because this wants the users of
  // this class to guarantee the lifetime of aText until this is destroyed.
  MOZ_CAN_RUN_SCRIPT explicit AutoTextEditVerifier(EditorBase& aEditorBase,
                                                   const Text& aText)
      : mText(aText), mDoCheck(false) {
    if (aEditorBase.IsTextEditor() || aEditorBase.GetEditActionEditContext()) {
      return;
    }
    // The most expensive cost of this check must be that we need to duplicate
    // the data as a string. Fortunately, known unsafe situation is only when
    // the `Text` is a descendant of a <script>. For avoiding the bad affect to
    // usual web apps, we should check whether we need to check it strictly only
    // when the known unsafe case.
    for (Element* const element : mText.AncestorsOfType<Element>()) {
      if (element->IsScriptElement()) {
        mDoCheck = true;
        break;
      }
    }
    if (!mDoCheck) [[likely]] {
      return;
    }
    mText.AppendTextTo(mExpectedData);
    mParentNode = mText.GetParentNode();
  }

  [[nodiscard]] bool IsExpectedData() const {
    if (!mDoCheck) [[likely]] {
      return true;
    }
    if (mText.TextDataLength() != mExpectedData.Length() ||
        mParentNode != mText.GetParentNode()) [[unlikely]] {
      return false;
    }
    return mText.DataBuffer().Equals(mExpectedData);
  }

  void WillInsertText(uint32_t aOffset, const nsAString& aStringToInsert) {
    if (!mDoCheck) {
      return;
    }
    // FYI: CharacterData::InsertData() returns error if aOffset is greater than
    // its data length, but in that case, IsExpectedData() won't be used. For
    // unexpected behavior change of CharacterData, we should make mExpectedData
    // even in the case.
    mExpectedData.Insert(aStringToInsert,
                         std::min<uint32_t>(aOffset, mExpectedData.Length()));
  }

  void WillDeleteText(uint32_t aOffset, uint32_t aCount) {
    if (!mDoCheck) {
      return;
    }
    // FYI: CharacterData::DeleteData() returns error if aOffset is greater than
    // its data length, but in that case, IsExpectedData() won't be used. For
    // unexpected behavior change of CharacterData, we should make mExpectedData
    // even in the case.
    const uint32_t offset = std::min<uint32_t>(aOffset, mExpectedData.Length());
    const uint32_t count = aCount > mExpectedData.Length() - offset
                               ? mExpectedData.Length() - offset
                               : aCount;
    if (!count) {
      return;
    }
    mExpectedData.Cut(offset, count);
  }

  void WillReplaceText(uint32_t aOffset, uint32_t aCount,
                       const nsAString& aStringToInsert) {
    WillDeleteText(aOffset, aCount);
    WillInsertText(aOffset, aStringToInsert);
  }

  void WillSetText(const nsAString& aStringToSet) {
    WillDeleteText(0, mText.TextDataLength());
    WillInsertText(0, aStringToSet);
  }

 private:
  MOZ_KNOWN_LIVE const Text& mText;
  nsCOMPtr<nsINode> mParentNode;
  nsAutoString mExpectedData;
  bool mDoCheck;
};

/*****************************************************************************
 * mozilla::EditorBase
 *****************************************************************************/
template EditorDOMPoint EditorBase::GetFirstIMESelectionStartPoint() const;
template EditorRawDOMPoint EditorBase::GetFirstIMESelectionStartPoint() const;
template EditorDOMPoint EditorBase::GetLastIMESelectionEndPoint() const;
template EditorRawDOMPoint EditorBase::GetLastIMESelectionEndPoint() const;

template Result<CreateContentResult, nsresult>
EditorBase::InsertNodeWithTransaction(nsIContent& aContentToInsert,
                                      const EditorDOMPoint& aPointToInsert);
template Result<CreateElementResult, nsresult>
EditorBase::InsertNodeWithTransaction(Element& aContentToInsert,
                                      const EditorDOMPoint& aPointToInsert);
template Result<CreateTextResult, nsresult>
EditorBase::InsertNodeWithTransaction(Text& aContentToInsert,
                                      const EditorDOMPoint& aPointToInsert);

template EditorDOMPoint EditorBase::GetFirstSelectionStartPoint() const;
template EditorRawDOMPoint EditorBase::GetFirstSelectionStartPoint() const;
template EditorDOMPoint EditorBase::GetFirstSelectionEndPoint() const;
template EditorRawDOMPoint EditorBase::GetFirstSelectionEndPoint() const;

template EditorBase::AutoCaretBidiLevelManager::AutoCaretBidiLevelManager(
    const EditorBase& aEditorBase, nsIEditor::EDirection aDirectionAndAmount,
    const EditorDOMPoint& aPointAtCaret);
template EditorBase::AutoCaretBidiLevelManager::AutoCaretBidiLevelManager(
    const EditorBase& aEditorBase, nsIEditor::EDirection aDirectionAndAmount,
    const EditorRawDOMPoint& aPointAtCaret);
template void EditorBase::AutoCaretBidiLevelManager::Init(
    const EditorBase& aEditorBase, nsIEditor::EDirection aDirectionAndAmount,
    const EditorDOMPoint& aPointAtCaret);
template void EditorBase::AutoCaretBidiLevelManager::Init(
    const EditorBase& aEditorBase, nsIEditor::EDirection aDirectionAndAmount,
    const EditorRawDOMPoint& aPointAtCaret);

EditorBase::EditorBase(EditorType aEditorType)
    : mEditActionData(nullptr),
      mPlaceholderName(nullptr),
      mModCount(0),
      mFlags(0),
      mUpdateCount(0),
      mPlaceholderBatch(0),
      mNewlineHandling(StaticPrefs::editor_singleLine_pasteNewlines()),
      mCaretStyle(StaticPrefs::layout_selection_caret_style()),
      mDocDirtyState(-1),
      mSpellcheckCheckboxState(eTriUnset),
      mInitSucceeded(false),
      mAllowsTransactionsToChangeSelection(true),
      mDidPreDestroy(false),
      mDidPostCreate(false),
      mDispatchInputEvent(true),
      mIsInEditSubAction(false),
      mHidingCaret(false),
      mSpellCheckerDictionaryUpdated(true),
      mIsHTMLEditorClass(aEditorType == EditorType::HTML) {
#ifdef XP_WIN
  if (!mCaretStyle && !IsTextEditor()) {
    // Wordpad-like caret behavior.
    mCaretStyle = 1;
  }
#endif  // #ifdef XP_WIN
  if (mNewlineHandling < nsIEditor::eNewlinesPasteIntact ||
      mNewlineHandling > nsIEditor::eNewlinesStripSurroundingWhitespace) {
    mNewlineHandling = nsIEditor::eNewlinesPasteToFirst;
  }
}

EditorBase::~EditorBase() {
  MOZ_ASSERT(!IsInitialized() || mDidPreDestroy,
             "Why PreDestroy hasn't been called?");

  if (mComposition) {
    mComposition->OnEditorDestroyed();
    mComposition = nullptr;
  }
  // If this editor is still hiding the caret, we need to restore it.
  HideCaret(false);
  mTransactionManager = nullptr;
}

NS_IMPL_CYCLE_COLLECTION_CLASS(EditorBase)

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(EditorBase)
  // Remove event listeners first since EditorEventListener may need
  // mDocument, mEventTarget, etc.
  if (tmp->mEventListener) {
    tmp->mEventListener->Disconnect();
    tmp->mEventListener = nullptr;
  }

  NS_IMPL_CYCLE_COLLECTION_UNLINK(mRootElement)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mSelectionController)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mDocument)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mIMEContentObserver)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mInlineSpellChecker)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mTextServicesDocument)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mTextInputListener)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mTransactionManager)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mActionListeners)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mDocStateListeners)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mEventTarget)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mPlaceholderTransaction)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mCachedDocumentEncoder)
  NS_IMPL_CYCLE_COLLECTION_UNLINK_WEAK_REFERENCE
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(EditorBase)
  Document* currentDoc =
      tmp->mRootElement ? tmp->mRootElement->GetUncomposedDoc() : nullptr;
  if (currentDoc && nsCCUncollectableMarker::InGeneration(
                        cb, currentDoc->GetMarkedCCGeneration())) {
    return NS_SUCCESS_INTERRUPTED_TRAVERSE;
  }
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mRootElement)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mSelectionController)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mDocument)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mIMEContentObserver)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mInlineSpellChecker)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mTextServicesDocument)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mTextInputListener)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mTransactionManager)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mActionListeners)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mDocStateListeners)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mEventTarget)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mEventListener)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mPlaceholderTransaction)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mCachedDocumentEncoder)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(EditorBase)
  NS_INTERFACE_MAP_ENTRY(nsISelectionListener)
  NS_INTERFACE_MAP_ENTRY(nsISupportsWeakReference)
  NS_INTERFACE_MAP_ENTRY(nsIEditor)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsIEditor)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTING_ADDREF(EditorBase)
NS_IMPL_CYCLE_COLLECTING_RELEASE(EditorBase)

nsresult EditorBase::InitInternal(Document& aDocument, Element* aRootElement,
                                  nsISelectionController& aSelectionController,
                                  uint32_t aFlags) {
  MOZ_ASSERT_IF(
      !mEditActionData ||
          !mEditActionData->HasEditorDestroyedDuringHandlingEditAction(),
      GetTopLevelEditSubAction() == EditSubAction::eNone);

  // First only set flags, but other stuff shouldn't be initialized now.
  // Note that SetFlags() will be called by PostCreate().
  mFlags = aFlags;

  mDocument = &aDocument;
  // nsISelectionController should be stored only when we're a `TextEditor`.
  // Otherwise, in `HTMLEditor`, it's `PresShell`, and grabbing it causes
  // a circular reference and memory leak.
  // XXX Should we move `mSelectionController to `TextEditor`?
  MOZ_ASSERT_IF(!IsTextEditor(), &aSelectionController == GetPresShell());
  if (IsTextEditor()) {
    MOZ_ASSERT(&aSelectionController != GetPresShell());
    mSelectionController = &aSelectionController;
  }

  if (mEditActionData) {
    // During edit action, selection is cached. But this selection is invalid
    // now since selection controller is updated, so we have to update this
    // cache.
    RefPtr<Selection> selection = aSelectionController.GetSelection(
        nsISelectionController::SELECTION_NORMAL);
    NS_WARNING_ASSERTION(selection,
                         "SelectionController::GetSelection() failed");
    if (selection) {
      mEditActionData->UpdateSelectionCache(*selection);
    }
  }

  // set up root element if we are passed one.
  if (aRootElement) {
    mRootElement = aRootElement;
  }

  // If this is an editor for <input> or <textarea>, the text node which
  // has composition string is always recreated with same content. Therefore,
  // we need to nodify mComposition of text node destruction and replacing
  // composing string when this receives eCompositionChange event next time.
  if (mComposition && mComposition->GetContainerTextNode() &&
      !mComposition->GetContainerTextNode()->IsInComposedDoc()) {
    mComposition->OnTextNodeRemoved();
  }

  // Show the caret.
  DebugOnly<nsresult> rvIgnored = aSelectionController.SetCaretReadOnly(false);
  NS_WARNING_ASSERTION(
      NS_SUCCEEDED(rvIgnored),
      "nsISelectionController::SetCaretReadOnly(false) failed, but ignored");
  // Show all the selection reflected to user.
  rvIgnored =
      aSelectionController.SetSelectionFlags(nsISelectionDisplay::DISPLAY_ALL);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rvIgnored),
                       "nsISelectionController::SetSelectionFlags("
                       "nsISelectionDisplay::DISPLAY_ALL) failed, but ignored");

  // Make sure that the editor will be destroyed properly
  mDidPreDestroy = false;
  // Make sure that the editor will be created properly
  mDidPostCreate = false;

  MOZ_ASSERT(IsBeingInitialized());

  AutoEditActionDataSetter editActionData(*this, EditAction::eInitializing);
  if (NS_WARN_IF(!editActionData.CanHandle())) {
    return NS_ERROR_FAILURE;
  }

  SelectionRef().AddSelectionListener(this);

  return NS_OK;
}

bool EditorBase::MaybeNodeRemovalsObservedByDevTools() const {
  if (IsTextEditor()) {
    // DOM mutation event listeners cannot catch the changes of
    // <input type="text"> nor <textarea>.
    return false;
  }
#ifdef DEBUG
  // On debug build, this should always return true for testing complicated
  // path without mutation event listeners because when mutation event
  // listeners do not touch the DOM, editor needs to run as there is no
  // mutation event listeners.
  return true;
#else   // #ifdef DEBUG
  Document* const doc = GetDocument();
  return doc && doc->MaybeNeedsToNotifyDevToolsOfNodeRemovalsInOwnerDoc();
#endif  // #ifdef DEBUG #else
}

nsresult EditorBase::EnsureEmptyTextFirstChild() {
  MOZ_ASSERT(IsTextEditor());
  RefPtr<Element> root = GetRoot();
  nsIContent* firstChild = root->GetFirstChild();

  if (!firstChild || !firstChild->IsText()) {
    RefPtr<nsTextNode> newTextNode = CreateTextNode(u""_ns);
    if (!newTextNode) {
      NS_WARNING("EditorBase::CreateTextNode() failed");
      return NS_ERROR_UNEXPECTED;
    }
    IgnoredErrorResult ignoredError;
    root->InsertChildBefore(newTextNode, root->GetFirstChild(), true,
                            ignoredError);
    MOZ_ASSERT(!ignoredError.Failed());
  }

  return NS_OK;
}

nsresult EditorBase::PostCreateInternal() {
  MOZ_ASSERT(IsEditActionDataAvailable());

  // Synchronize some stuff for the flags.  SetFlags() will initialize
  // something by the flag difference.  This is first time of that, so, all
  // initializations must be run.  For such reason, we need to invert mFlags
  // value first.
  mFlags = ~mFlags;
  nsresult rv = SetFlags(~mFlags);
  if (NS_FAILED(rv)) {
    NS_WARNING("EditorBase::SetFlags() failed");
    return EditorBase::ToGenericNSResult(rv);
  }

  // These operations only need to happen on the first PostCreate call
  if (!mDidPostCreate) {
    mDidPostCreate = true;

    // Set up listeners
    CreateEventListeners();
    nsresult rv = InstallEventListeners();
    if (NS_FAILED(rv)) {
      NS_WARNING("EditorBase::InstallEventListeners() failed");
      return EditorBase::ToGenericNSResult(rv);
    }

    // nuke the modification count, so the doc appears unmodified
    // do this before we notify listeners
    DebugOnly<nsresult> rvIgnored = ResetModificationCount();
    NS_WARNING_ASSERTION(
        NS_SUCCEEDED(rvIgnored),
        "EditorBase::ResetModificationCount() failed, but ignored");

    // update the UI with our state
    rvIgnored = NotifyDocumentListeners(eDocumentCreated);
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rvIgnored),
                         "EditorBase::NotifyDocumentListeners(eDocumentCreated)"
                         " failed, but ignored");
    rvIgnored = NotifyDocumentListeners(eDocumentStateChanged);
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rvIgnored),
                         "EditorBase::NotifyDocumentListeners("
                         "eDocumentStateChanged) failed, but ignored");
  }

  // update nsTextStateManager and caret if we have focus
  if (RefPtr<Element> focusedElement = GetFocusedElement()) {
    DebugOnly<nsresult> rvIgnored = InitializeSelection(*focusedElement);
    NS_WARNING_ASSERTION(
        NS_SUCCEEDED(rvIgnored),
        "EditorBase::InitializeSelection() failed, but ignored");

    // If the text control gets reframed during focus, Focus() would not be
    // called, so take a chance here to see if we need to spell check the text
    // control.
    nsresult rv = FlushPendingSpellCheck();
    if (MOZ_UNLIKELY(rv == NS_ERROR_EDITOR_DESTROYED)) {
      NS_WARNING(
          "EditorBase::FlushPendingSpellCheck() caused destroying the editor");
      return EditorBase::ToGenericNSResult(NS_ERROR_EDITOR_DESTROYED);
    }
    NS_WARNING_ASSERTION(
        NS_SUCCEEDED(rv),
        "EditorBase::FlushPendingSpellCheck() failed, but ignored");

    Result<IMEState, nsresult> newStateOrError = GetPreferredIMEState();
    if (MOZ_UNLIKELY(newStateOrError.isErr())) {
      NS_WARNING("EditorBase::GetPreferredIMEState() failed");
      return NS_OK;
    }
    IMEStateManager::UpdateIMEState(newStateOrError.unwrap(), focusedElement,
                                    *this);
  }

  // FYI: This call might cause destroying this editor.
  IMEStateManager::OnEditorInitialized(*this);

  return NS_OK;
}

void EditorBase::SetTextInputListener(TextInputListener* aTextInputListener) {
  MOZ_ASSERT(!mTextInputListener || !aTextInputListener ||
             mTextInputListener == aTextInputListener);
  mTextInputListener = aTextInputListener;
}

void EditorBase::SetIMEContentObserver(
    IMEContentObserver* aIMEContentObserver) {
  MOZ_ASSERT(!mIMEContentObserver || !aIMEContentObserver ||
             mIMEContentObserver == aIMEContentObserver);
  mIMEContentObserver = aIMEContentObserver;
}

void EditorBase::CreateEventListeners() {
  // Don't create the handler twice
  if (!mEventListener) {
    mEventListener = new EditorEventListener();
  }
}

nsresult EditorBase::InstallEventListeners() {
  // FIXME InstallEventListeners() should not be called if we failed to set
  // document or create an event listener.  So, these checks should be
  // MOZ_DIAGNOSTIC_ASSERT instead.
  MOZ_ASSERT(GetDocument());
  if (MOZ_UNLIKELY(!GetDocument()) || NS_WARN_IF(!mEventListener)) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  // Initialize the event target.
  mEventTarget = GetExposedRoot();
  if (NS_WARN_IF(!mEventTarget)) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  nsresult rv = mEventListener->Connect(this);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "EditorEventListener::Connect() failed");
  if (mComposition) {
    // If mComposition has already been destroyed, we should forget it.
    // This may happen if it ended while we don't listen to composition
    // events.
    if (mComposition->Destroyed()) {
      // XXX We may need to fix existing composition transaction here.
      //     However, this may be called when it's not safe.
      //     Perhaps, we should stop handling composition with events.
      mComposition = nullptr;
    }
    // Otherwise, Restart to handle composition with new editor contents.
    else {
      mComposition->StartHandlingComposition(this);
    }
  }
  return rv;
}

void EditorBase::RemoveEventListeners() {
  if (!mEventListener) {
    return;
  }
  mEventListener->Disconnect();
  if (mComposition) {
    // Even if this is called, don't release mComposition because this is
    // may be reused after reframing.
    mComposition->EndHandlingComposition(this);
  }
  mEventTarget = nullptr;
}

bool EditorBase::IsListeningToEvents() const {
  return mEventListener && !mEventListener->DetachedFromEditor();
}

bool EditorBase::GetDesiredSpellCheckState() {
  // Check user override on this element
  if (mSpellcheckCheckboxState != eTriUnset) {
    return (mSpellcheckCheckboxState == eTriTrue);
  }

  // Check user preferences
  int32_t spellcheckLevel = StaticPrefs::layout_spellcheckDefault();

  if (!spellcheckLevel) {
    return false;  // Spellchecking forced off globally
  }

  if (!CanEnableSpellCheck()) {
    return false;
  }

  PresShell* presShell = GetPresShell();
  if (presShell) {
    nsPresContext* context = presShell->GetPresContext();
    if (context && !context->IsDynamic()) {
      return false;
    }
  }

  // Check DOM state
  nsCOMPtr<nsIContent> content = GetExposedRoot();
  if (!content) {
    return false;
  }

  auto element = nsGenericHTMLElement::FromNode(content);
  if (!element) {
    return false;
  }

  // XXX I'm not sure whether we don't use this path when we're a plaintext mail
  // composer.
  if (IsHTMLEditor() && !AsHTMLEditor()->IsPlaintextMailComposer()) {
    // Some of the page content might be editable and some not, if spellcheck=
    // is explicitly set anywhere, so if there's anything editable on the page,
    // return true and let the spellchecker figure it out.
    Document* doc = content->GetComposedDoc();
    return doc && doc->IsEditingOn();
  }

  return element->Spellcheck();
}

void EditorBase::PreDestroyInternal() {
  MOZ_ASSERT(!mDidPreDestroy);

  mInitSucceeded = false;

  Selection* selection = GetSelection();
  if (selection) {
    selection->RemoveSelectionListener(this);
  }

  IMEStateManager::OnEditorDestroying(*this);

  // Let spellchecker clean up its observers etc. It is important not to
  // actually free the spellchecker here, since the spellchecker could have
  // caused flush notifications, which could have gotten here if a textbox
  // is being removed. Setting the spellchecker to nullptr could free the
  // object that is still in use! It will be freed when the editor is
  // destroyed.
  if (mInlineSpellChecker) {
    DebugOnly<nsresult> rvIgnored =
        mInlineSpellChecker->Cleanup(IsTextEditor());
    NS_WARNING_ASSERTION(
        NS_SUCCEEDED(rvIgnored),
        "mozInlineSpellChecker::Cleanup() failed, but ignored");
  }

  // tell our listeners that the doc is going away
  DebugOnly<nsresult> rvIgnored =
      NotifyDocumentListeners(eDocumentToBeDestroyed);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rvIgnored),
                       "EditorBase::NotifyDocumentListeners("
                       "eDocumentToBeDestroyed) failed, but ignored");

  // Unregister event listeners
  RemoveEventListeners();
  // If this editor is still hiding the caret, we need to restore it.
  HideCaret(false);
  mActionListeners.Clear();
  mDocStateListeners.Clear();
  mInlineSpellChecker = nullptr;
  mTextServicesDocument = nullptr;
  mTextInputListener = nullptr;
  mSpellcheckCheckboxState = eTriUnset;
  mRootElement = nullptr;

  // Transaction may grab this instance.  Therefore, they should be released
  // here for stopping the circular reference with this instance.
  if (mTransactionManager) {
    DebugOnly<bool> disabledUndoRedo = DisableUndoRedo();
    NS_WARNING_ASSERTION(disabledUndoRedo,
                         "EditorBase::DisableUndoRedo() failed, but ignored");
    mTransactionManager = nullptr;
  }

  if (mEditActionData) {
    mEditActionData->OnEditorDestroy();
  }

  mDidPreDestroy = true;
}

NS_IMETHODIMP EditorBase::GetFlags(uint32_t* aFlags) {
  // NOTE: If you need to override this method, you need to make Flags()
  //       virtual.
  *aFlags = Flags();
  return NS_OK;
}

NS_IMETHODIMP EditorBase::SetFlags(uint32_t aFlags) {
  if (mFlags == aFlags) {
    return NS_OK;
  }

  // If we're a `TextEditor` instance, it's always a plaintext editor.
  // Therefore, `eEditorPlaintextMask` is not necessary and should not be set
  // for the performance reason.
  MOZ_ASSERT_IF(IsTextEditor(), !(aFlags & nsIEditor::eEditorPlaintextMask));
  // If we're an `HTMLEditor` instance, we cannot treat it as a single line
  // editor.  So, eEditorSingleLineMask is available only when we're a
  // `TextEditor` instance.
  MOZ_ASSERT_IF(IsHTMLEditor(), !(aFlags & nsIEditor::eEditorSingleLineMask));
  // If we're an `HTMLEditor` instance, we cannot treat it as a password editor.
  // So, eEditorPasswordMask is available only when we're a `TextEditor`
  // instance.
  MOZ_ASSERT_IF(IsHTMLEditor(), !(aFlags & nsIEditor::eEditorPasswordMask));
  // eEditorAllowInteraction changes the behavior of `HTMLEditor`.  So, it's
  // not available with `TextEditor` instance.
  MOZ_ASSERT_IF(IsTextEditor(), !(aFlags & nsIEditor::eEditorAllowInteraction));

  const bool isCalledByPostCreate = (mFlags == ~aFlags);
  const bool spellcheckerWasEnabled =
      !isCalledByPostCreate && CanEnableSpellCheck();
  const bool wasPasswordEditor = !isCalledByPostCreate && IsPasswordEditor();

  mFlags = aFlags;

  if (!IsInitialized()) {
    // If we're initializing, we shouldn't do anything now.
    // SetFlags() will be called by PostCreate(),
    // we should synchronize some stuff for the flags at that time.
    return NS_OK;
  }

  if (!isCalledByPostCreate && IsPasswordEditor() != wasPasswordEditor) {
    AsTextEditor()->ResetPasswordMaskData();
  }

  // The flag change may cause the spellchecker state change
  if (CanEnableSpellCheck() != spellcheckerWasEnabled) {
    SyncRealTimeSpell();
  }

  // If this is called from PostCreate(), it will update the IME state if it's
  // necessary.
  if (!mDidPostCreate) {
    return NS_OK;
  }

  // Might be changing editable state, so, we need to reset current IME state
  // if we're focused and the flag change causes IME state change.
  if (RefPtr<Element> focusedElement = GetFocusedElement()) {
    Result<IMEState, nsresult> newStateOrError = GetPreferredIMEState();
    NS_WARNING_ASSERTION(newStateOrError.isOk(),
                         "EditorBase::GetPreferredIMEState() failed");
    if (MOZ_LIKELY(newStateOrError.isOk())) {
      // NOTE: When the enabled state isn't going to be modified, this method
      // is going to do nothing.
      IMEStateManager::UpdateIMEState(newStateOrError.unwrap(), focusedElement,
                                      *this);
    }
  }

  return NS_OK;
}

NS_IMETHODIMP EditorBase::GetIsSelectionEditable(bool* aIsSelectionEditable) {
  if (NS_WARN_IF(!aIsSelectionEditable)) {
    return NS_ERROR_INVALID_ARG;
  }
  *aIsSelectionEditable = IsSelectionEditable();
  return NS_OK;
}

bool EditorBase::IsSelectionEditable() {
  if (ComputeEditContext()) {
    // If there's an active EditContext, then we should always allow
    // editing commands even if the DOM selection is not editable.
    return true;
  }
  AutoEditActionDataSetter editActionData(*this, EditAction::eNotEditing);
  if (NS_WARN_IF(!editActionData.CanHandle())) {
    return false;
  }

  if (IsTextEditor()) {
    // XXX we just check that the anchor node is editable at the moment
    //     we should check that all nodes in the selection are editable
    const nsINode* anchorNode = SelectionRef().GetAnchorNode();
    return anchorNode && anchorNode->IsContent() && anchorNode->IsEditable();
  }

  const nsINode* anchorNode = SelectionRef().GetAnchorNode();
  const nsINode* focusNode = SelectionRef().GetFocusNode();
  if (!anchorNode || !focusNode) {
    return false;
  }

  // if anchorNode or focusNode is in a native anonymous subtree, HTMLEditor
  // shouldn't edit content in it.
  // XXX This must be a bug of Selection API.
  if (MOZ_UNLIKELY(anchorNode->IsInNativeAnonymousSubtree() ||
                   focusNode->IsInNativeAnonymousSubtree())) {
    return false;
  }

  // Per the editing spec as of June 2012: we have to have a selection whose
  // start and end nodes are editable, and which share an ancestor editing
  // host.  (Bug 766387.)
  bool isSelectionEditable = SelectionRef().RangeCount() &&
                             anchorNode->IsEditable() &&
                             focusNode->IsEditable();
  if (!isSelectionEditable) {
    return false;
  }

  const nsINode* commonAncestor =
      SelectionRef().GetAnchorFocusRange()->GetClosestCommonInclusiveAncestor();
  while (commonAncestor && !commonAncestor->IsEditable()) {
    commonAncestor = commonAncestor->GetParentNode();
  }
  // If there is no editable common ancestor, return false.
  return !!commonAncestor;
}

NS_IMETHODIMP EditorBase::GetIsDocumentEditable(bool* aIsDocumentEditable) {
  if (NS_WARN_IF(!aIsDocumentEditable)) {
    return NS_ERROR_INVALID_ARG;
  }
  RefPtr<Document> document = GetDocument();
  *aIsDocumentEditable = document && IsModifiable();
  return NS_OK;
}

NS_IMETHODIMP EditorBase::GetDocument(Document** aDocument) {
  if (NS_WARN_IF(!aDocument)) {
    return NS_ERROR_INVALID_ARG;
  }
  *aDocument = do_AddRef(mDocument).take();
  return NS_WARN_IF(!*aDocument) ? NS_ERROR_NOT_INITIALIZED : NS_OK;
}

already_AddRefed<nsIWidget> EditorBase::GetWidget() const {
  nsPresContext* presContext = GetPresContext();
  if (NS_WARN_IF(!presContext)) {
    return nullptr;
  }
  nsCOMPtr<nsIWidget> widget = presContext->GetRootWidget();
  return NS_WARN_IF(!widget) ? nullptr : widget.forget();
}

NS_IMETHODIMP EditorBase::GetContentsMIMEType(nsAString& aContentsMIMEType) {
  aContentsMIMEType = mContentMIMEType;
  return NS_OK;
}

NS_IMETHODIMP EditorBase::SetContentsMIMEType(
    const nsAString& aContentsMIMEType) {
  mContentMIMEType.Assign(aContentsMIMEType);
  return NS_OK;
}

NS_IMETHODIMP EditorBase::GetSelectionController(
    nsISelectionController** aSelectionController) {
  if (NS_WARN_IF(!aSelectionController)) {
    return NS_ERROR_INVALID_ARG;
  }
  *aSelectionController = do_AddRef(GetSelectionController()).take();
  return NS_WARN_IF(!*aSelectionController) ? NS_ERROR_FAILURE : NS_OK;
}

NS_IMETHODIMP EditorBase::DeleteSelection(EDirection aAction,
                                          EStripWrappers aStripWrappers) {
  nsresult rv = DeleteSelectionAsAction(aAction, aStripWrappers);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "EditorBase::DeleteSelectionAsAction() failed");
  return rv;
}

NS_IMETHODIMP EditorBase::GetSelection(Selection** aSelection) {
  nsresult rv = GetSelection(SelectionType::eNormal, aSelection);
  NS_WARNING_ASSERTION(
      NS_SUCCEEDED(rv),
      "EditorBase::GetSelection(SelectionType::eNormal) failed");
  return rv;
}

nsresult EditorBase::GetSelection(SelectionType aSelectionType,
                                  Selection** aSelection) const {
  if (NS_WARN_IF(!aSelection)) {
    return NS_ERROR_INVALID_ARG;
  }
  if (IsEditActionDataAvailable()) {
    *aSelection = do_AddRef(&SelectionRef()).take();
    return NS_OK;
  }
  nsISelectionController* selectionController = GetSelectionController();
  if (NS_WARN_IF(!selectionController)) {
    *aSelection = nullptr;
    return NS_ERROR_NOT_INITIALIZED;
  }
  *aSelection = do_AddRef(selectionController->GetSelection(
                              ToRawSelectionType(aSelectionType)))
                    .take();
  return NS_WARN_IF(!*aSelection) ? NS_ERROR_FAILURE : NS_OK;
}

Selection* EditorBase::GetExistingSelection(
    SelectionType aSelectionType) const {
  if (aSelectionType == SelectionType::eNormal && IsEditActionDataAvailable()) {
    return &SelectionRef();
  }
  nsISelectionController* sc = GetSelectionController();
  if (!sc) {
    return nullptr;
  }
  return sc->GetExistingSelection(ToRawSelectionType(aSelectionType));
}

nsFrameSelection* EditorBase::GetEditableFrameSelection() const {
  EditContext* editContext = GetEditActionEditContext();
  return editContext && editContext->IsCanvas()
             ? nullptr
             : SelectionRef().GetFrameSelection();
}

nsresult EditorBase::DoTransactionInternal(nsITransaction* aTransaction) {
  MOZ_ASSERT(IsEditActionDataAvailable());
  MOZ_ASSERT_IF(
      // If the DOM is modified by a clipboard event handler,
      // HTMLEditor::OnModifyDocument() may need to do some transactions before
      // dispatching `beforeinput`.
      // FIXME: It shouldn't happen, and I think that it should be done once
      // before dispatching `input` event to hide the our editor hack from
      // the event listeners.
      GetEditAction() != EditAction::ePaste &&
          GetEditAction() != EditAction::eCut,
      !ShouldAlreadyHaveHandledBeforeInputEventDispatching());

  if (mPlaceholderBatch && !mPlaceholderTransaction) {
    MOZ_DIAGNOSTIC_ASSERT(mPlaceholderName);
    mPlaceholderTransaction = PlaceholderTransaction::Create(
        *this, *mPlaceholderName, std::move(mSelState));
    MOZ_ASSERT(mSelState.isNothing());

    // We will recurse, but will not hit this case in the nested call
    RefPtr<PlaceholderTransaction> placeholderTransaction =
        mPlaceholderTransaction;
    DebugOnly<nsresult> rvIgnored =
        DoTransactionInternal(placeholderTransaction);
    NS_WARNING_ASSERTION(
        NS_SUCCEEDED(rvIgnored),
        "EditorBase::DoTransactionInternal() failed, but ignored");

    if (mTransactionManager) {
      if (nsCOMPtr<nsITransaction> topTransaction =
              mTransactionManager->PeekUndoStack()) {
        if (RefPtr<EditTransactionBase> topTransactionBase =
                topTransaction->GetAsEditTransactionBase()) {
          if (PlaceholderTransaction* topPlaceholderTransaction =
                  topTransactionBase->GetAsPlaceholderTransaction()) {
            // there is a placeholder transaction on top of the undo stack.  It
            // is either the one we just created, or an earlier one that we are
            // now merging into.  From here on out remember this placeholder
            // instead of the one we just created.
            mPlaceholderTransaction = topPlaceholderTransaction;
          }
        }
      }
    }
  }

  if (aTransaction) {
    // XXX: Why are we doing selection specific batching stuff here?
    // XXX: Most entry points into the editor have auto variables that
    // XXX: should trigger Begin/EndUpdateViewBatch() calls that will make
    // XXX: these selection batch calls no-ops.
    // XXX:
    // XXX: I suspect that this was placed here to avoid multiple
    // XXX: selection changed notifications from happening until after
    // XXX: the transaction was done. I suppose that can still happen
    // XXX: if an embedding application called DoTransaction() directly
    // XXX: to pump its own transactions through the system, but in that
    // XXX: case, wouldn't we want to use Begin/EndUpdateViewBatch() or
    // XXX: its auto equivalent AutoUpdateViewBatch to ensure that
    // XXX: selection listeners have access to accurate frame data?
    // XXX:
    // XXX: Note that if we did add Begin/EndUpdateViewBatch() calls
    // XXX: we will need to make sure that they are disabled during
    // XXX: the init of the editor for text widgets to avoid layout
    // XXX: re-entry during initial reflow. - kin

    // get the selection and start a batch change
    SelectionBatcher selectionBatcher(SelectionRef(), __FUNCTION__);

    if (mTransactionManager) {
      RefPtr<TransactionManager> transactionManager(mTransactionManager);
      nsresult rv = transactionManager->DoTransaction(aTransaction);
      if (NS_FAILED(rv)) {
        NS_WARNING("TransactionManager::DoTransaction() failed");
        return rv;
      }
    } else {
      nsresult rv = aTransaction->DoTransaction();
      if (NS_FAILED(rv)) {
        NS_WARNING("nsITransaction::DoTransaction() failed");
        return rv;
      }
    }

    DoAfterDoTransaction(aTransaction);
  }

  return NS_OK;
}

NS_IMETHODIMP EditorBase::EnableUndo(bool aEnable) {
  // XXX Should we return NS_ERROR_FAILURE if EdnableUndoRedo() or
  //     DisableUndoRedo() returns false?
  if (aEnable) {
    DebugOnly<bool> enabledUndoRedo = EnableUndoRedo();
    NS_WARNING_ASSERTION(enabledUndoRedo,
                         "EditorBase::EnableUndoRedo() failed, but ignored");
    return NS_OK;
  }
  DebugOnly<bool> disabledUndoRedo = DisableUndoRedo();
  NS_WARNING_ASSERTION(disabledUndoRedo,
                       "EditorBase::DisableUndoRedo() failed, but ignored");
  return NS_OK;
}

NS_IMETHODIMP EditorBase::ClearUndoRedoXPCOM() {
  if (MOZ_UNLIKELY(!ClearUndoRedo())) {
    return NS_ERROR_FAILURE;  // We're handling a transaction
  }
  return NS_OK;
}

NS_IMETHODIMP EditorBase::Undo() {
  nsresult rv = UndoAsAction(1u);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv), "EditorBase::UndoAsAction() failed");
  return rv;
}

NS_IMETHODIMP EditorBase::UndoAll() {
  if (!mTransactionManager) {
    return NS_OK;
  }
  size_t numberOfUndoItems = mTransactionManager->NumberOfUndoItems();
  if (!numberOfUndoItems) {
    return NS_OK;  // no transactions
  }
  nsresult rv = UndoAsAction(numberOfUndoItems);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv), "EditorBase::UndoAsAction() failed");
  return rv;
}

NS_IMETHODIMP EditorBase::GetUndoRedoEnabled(bool* aIsEnabled) {
  MOZ_ASSERT(aIsEnabled);
  *aIsEnabled = IsUndoRedoEnabled();
  return NS_OK;
}

NS_IMETHODIMP EditorBase::GetCanUndo(bool* aCanUndo) {
  MOZ_ASSERT(aCanUndo);
  *aCanUndo = CanUndo();
  return NS_OK;
}

NS_IMETHODIMP EditorBase::Redo() {
  nsresult rv = RedoAsAction(1u);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv), "EditorBase::RedoAsAction() failed");
  return rv;
}

NS_IMETHODIMP EditorBase::GetCanRedo(bool* aCanRedo) {
  MOZ_ASSERT(aCanRedo);
  *aCanRedo = CanRedo();
  return NS_OK;
}

nsresult EditorBase::UndoAsAction(uint32_t aCount, nsIPrincipal* aPrincipal) {
  if (aCount == 0 || IsReadonly()) {
    return NS_OK;
  }

  // If we don't have transaction in the undo stack, we shouldn't notify
  // anybody of trying to undo since it's not useful notification but we
  // need to pay some runtime cost.
  if (!CanUndo()) {
    return NS_OK;
  }

  // If there is composition, we shouldn't allow to undo with committing
  // composition since Chrome doesn't allow it and it doesn't make sense
  // because committing composition causes one transaction and Undo(1)
  // undoes the committing composition.
  if (GetComposition()) {
    return NS_OK;
  }

  AutoEditActionDataSetter editActionData(*this, EditAction::eUndo, aPrincipal);
  nsresult rv = editActionData.CanHandleAndMaybeDispatchBeforeInputEvent();
  if (NS_FAILED(rv)) {
    NS_WARNING_ASSERTION(rv == NS_ERROR_EDITOR_ACTION_CANCELED,
                         "CanHandleAndMaybeDispatchBeforeInputEvent() failed");
    return EditorBase::ToGenericNSResult(rv);
  }
  if (editActionData.GetEditContext()) {
    // Only fire beforeinput for EditContext, don't actually undo.
    return NS_OK;
  }

  AutoUpdateViewBatch preventSelectionChangeEvent(*this, __FUNCTION__);

  NotifyEditorObservers(eNotifyEditorObserversOfBefore);
  if (NS_WARN_IF(!CanUndo()) || NS_WARN_IF(Destroyed())) {
    return NS_ERROR_FAILURE;
  }

  rv = NS_OK;
  {
    IgnoredErrorResult ignoredError;
    AutoEditSubActionNotifier startToHandleEditSubAction(
        *this, EditSubAction::eUndo, nsIEditor::eNone, ignoredError);
    if (NS_WARN_IF(ignoredError.ErrorCodeIs(NS_ERROR_EDITOR_DESTROYED))) {
      return EditorBase::ToGenericNSResult(ignoredError.StealNSResult());
    }
    NS_WARNING_ASSERTION(!ignoredError.Failed(),
                         "TextEditor::OnStartToHandleTopLevelEditSubAction() "
                         "failed, but ignored");

    RefPtr<TransactionManager> transactionManager(mTransactionManager);
    for (uint32_t i = 0; i < aCount; ++i) {
      if (NS_FAILED(transactionManager->Undo())) {
        NS_WARNING("TransactionManager::Undo() failed");
        break;
      }
      DoAfterUndoTransaction();
    }

    if (IsHTMLEditor()) {
      rv = AsHTMLEditor()->ReflectPaddingBRElementForEmptyEditor();
    }
  }

  NotifyEditorObservers(eNotifyEditorObserversOfEnd);
  return EditorBase::ToGenericNSResult(rv);
}

nsresult EditorBase::RedoAsAction(uint32_t aCount, nsIPrincipal* aPrincipal) {
  if (aCount == 0 || IsReadonly()) {
    return NS_OK;
  }

  // If we don't have transaction in the redo stack, we shouldn't notify
  // anybody of trying to redo since it's not useful notification but we
  // need to pay some runtime cost.
  if (!CanRedo()) {
    return NS_OK;
  }

  // If there is composition, we shouldn't allow to redo with committing
  // composition since Chrome doesn't allow it and it doesn't make sense
  // because committing composition causes removing all transactions from
  // the redo queue.  So, it becomes impossible to redo anything.
  if (GetComposition()) {
    return NS_OK;
  }

  AutoEditActionDataSetter editActionData(*this, EditAction::eRedo, aPrincipal);
  nsresult rv = editActionData.CanHandleAndMaybeDispatchBeforeInputEvent();
  if (NS_FAILED(rv)) {
    NS_WARNING_ASSERTION(rv == NS_ERROR_EDITOR_ACTION_CANCELED,
                         "CanHandleAndMaybeDispatchBeforeInputEvent() failed");
    return EditorBase::ToGenericNSResult(rv);
  }
  if (editActionData.GetEditContext()) {
    // Only fire beforeinput for EditContext, don't actually redo.
    return NS_OK;
  }

  AutoUpdateViewBatch preventSelectionChangeEvent(*this, __FUNCTION__);

  NotifyEditorObservers(eNotifyEditorObserversOfBefore);
  if (NS_WARN_IF(!CanRedo()) || NS_WARN_IF(Destroyed())) {
    return NS_ERROR_FAILURE;
  }

  rv = NS_OK;
  {
    IgnoredErrorResult ignoredError;
    AutoEditSubActionNotifier startToHandleEditSubAction(
        *this, EditSubAction::eRedo, nsIEditor::eNone, ignoredError);
    if (NS_WARN_IF(ignoredError.ErrorCodeIs(NS_ERROR_EDITOR_DESTROYED))) {
      return ignoredError.StealNSResult();
    }
    NS_WARNING_ASSERTION(!ignoredError.Failed(),
                         "TextEditor::OnStartToHandleTopLevelEditSubAction() "
                         "failed, but ignored");

    RefPtr<TransactionManager> transactionManager(mTransactionManager);
    for (uint32_t i = 0; i < aCount; ++i) {
      if (NS_FAILED(transactionManager->Redo())) {
        NS_WARNING("TransactionManager::Redo() failed");
        break;
      }
      DoAfterRedoTransaction();
    }

    if (IsHTMLEditor()) {
      rv = AsHTMLEditor()->ReflectPaddingBRElementForEmptyEditor();
    }
  }

  NotifyEditorObservers(eNotifyEditorObserversOfEnd);
  return EditorBase::ToGenericNSResult(rv);
}

NS_IMETHODIMP EditorBase::BeginTransaction() {
  AutoEditActionDataSetter editActionData(*this, EditAction::eUnknown);
  if (NS_WARN_IF(!editActionData.CanHandle())) {
    return NS_ERROR_FAILURE;
  }

  BeginTransactionInternal(__FUNCTION__);
  return NS_OK;
}

void EditorBase::BeginTransactionInternal(const char* aRequesterFuncName) {
  BeginUpdateViewBatch(aRequesterFuncName);

  if (NS_WARN_IF(!mTransactionManager)) {
    return;
  }

  RefPtr<TransactionManager> transactionManager(mTransactionManager);
  DebugOnly<nsresult> rvIgnored = transactionManager->BeginBatch(nullptr);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rvIgnored),
                       "TransactionManager::BeginBatch() failed, but ignored");
}

NS_IMETHODIMP EditorBase::EndTransaction() {
  AutoEditActionDataSetter editActionData(*this, EditAction::eUnknown);
  if (NS_WARN_IF(!editActionData.CanHandle())) {
    return NS_ERROR_FAILURE;
  }

  EndTransactionInternal(__FUNCTION__);
  return NS_OK;
}

void EditorBase::EndTransactionInternal(const char* aRequesterFuncName) {
  if (mTransactionManager) {
    RefPtr<TransactionManager> transactionManager(mTransactionManager);
    DebugOnly<nsresult> rvIgnored = transactionManager->EndBatch(false);
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rvIgnored),
                         "TransactionManager::EndBatch() failed, but ignored");
  }

  EndUpdateViewBatch(aRequesterFuncName);
}

void EditorBase::BeginPlaceholderTransaction(nsStaticAtom& aTransactionName,
                                             const char* aRequesterFuncName) {
  MOZ_ASSERT(IsEditActionDataAvailable());
  MOZ_ASSERT(mPlaceholderBatch >= 0, "negative placeholder batch count!");

  if (!mPlaceholderBatch) {
    NotifyEditorObservers(eNotifyEditorObserversOfBefore);
    // time to turn on the batch
    BeginUpdateViewBatch(aRequesterFuncName);
    mPlaceholderTransaction = nullptr;
    mPlaceholderName = &aTransactionName;
    mSelState.emplace();
    mSelState->SaveSelection(SelectionRef());
    // Composition transaction can modify multiple nodes and it merges text
    // node for ime into single text node.
    // So if current selection is into IME text node, it might be failed
    // to restore selection by UndoTransaction.
    // So we need update selection by range updater.
    if (mPlaceholderName == nsGkAtoms::IMETxnName) {
      RangeUpdaterRef().RegisterSelectionState(*mSelState);
    }
  }
  mPlaceholderBatch++;
}

void EditorBase::EndPlaceholderTransaction(
    ScrollSelectionIntoView aScrollSelectionIntoView,
    const char* aRequesterFuncName) {
  MOZ_ASSERT(IsEditActionDataAvailable());
  MOZ_ASSERT(mPlaceholderBatch > 0,
             "zero or negative placeholder batch count when ending batch!");

  if (!(--mPlaceholderBatch)) {
    // By making the assumption that no reflow happens during the calls
    // to EndUpdateViewBatch and ScrollSelectionFocusIntoView, we are able to
    // allow the selection to cache a frame offset which is used by the
    // caret drawing code. We only enable this cache here; at other times,
    // we have no way to know whether reflow invalidates it
    // See bugs 35296 and 199412.
    SelectionRef().SetCanCacheFrameOffset(true);

    // time to turn off the batch
    EndUpdateViewBatch(aRequesterFuncName);
    // make sure selection is in view

    // After ScrollSelectionFocusIntoView(), the pending notifications might be
    // flushed and PresShell/PresContext/Frames may be dead. See bug 418470.
    // XXX Even if we're destroyed, we need to keep handling below because
    //     this method changes a lot of status.  We should rewrite this safer.
    if (aScrollSelectionIntoView == ScrollSelectionIntoView::Yes) {
      DebugOnly<nsresult> rvIgnored = ScrollSelectionFocusIntoView();
      NS_WARNING_ASSERTION(
          NS_SUCCEEDED(rvIgnored),
          "EditorBase::ScrollSelectionFocusIntoView() failed, but Ignored");
    }

    // cached for frame offset are Not available now
    SelectionRef().SetCanCacheFrameOffset(false);

    if (mSelState) {
      // we saved the selection state, but never got to hand it to placeholder
      // (else we ould have nulled out this pointer), so destroy it to prevent
      // leaks.
      if (mPlaceholderName == nsGkAtoms::IMETxnName) {
        RangeUpdaterRef().DropSelectionState(*mSelState);
      }
      mSelState.reset();
    }
    // We might have never made a placeholder if no action took place.
    if (mPlaceholderTransaction) {
      // FYI: Disconnect placeholder transaction before dispatching "input"
      //      event because an input event listener may start other things.
      // TODO: We should forget EditActionDataSetter too.
      RefPtr<PlaceholderTransaction> placeholderTransaction =
          std::move(mPlaceholderTransaction);
      DebugOnly<nsresult> rvIgnored =
          placeholderTransaction->EndPlaceHolderBatch();
      NS_WARNING_ASSERTION(
          NS_SUCCEEDED(rvIgnored),
          "PlaceholderTransaction::EndPlaceHolderBatch() failed, but ignored");
      // notify editor observers of action but if composing, it's done by
      // compositionchange event handler.
      if (!mComposition) {
        NotifyEditorObservers(eNotifyEditorObserversOfEnd);
      }
    } else if (!mComposition) {
      // notify editor observers of action but if composing, it's done by
      // compositionchange event handler. (EditContext can reach here
      // with mPlaceholderTransaction = null while composing.)
      NotifyEditorObservers(eNotifyEditorObserversOfCancel);
    }
  }
}

NS_IMETHODIMP EditorBase::GetDocumentIsEmpty(bool* aDocumentIsEmpty) {
  MOZ_ASSERT(aDocumentIsEmpty);
  *aDocumentIsEmpty = IsEmpty();
  return NS_OK;
}

// XXX: The rule system should tell us which node to select all on (ie, the
//      root, or the body)
NS_IMETHODIMP EditorBase::SelectAll() {
  AutoEditActionDataSetter editActionData(*this, EditAction::eNotEditing);
  if (NS_WARN_IF(!editActionData.CanHandle())) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  nsresult rv = SelectAllInternal();
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv), "SelectAllInternal() failed");
  // This is low level API for XUL applcation.  So, we should return raw
  // error code here.
  return rv;
}

nsresult EditorBase::SelectAllInternal() {
  MOZ_ASSERT(IsInitialized());

  DebugOnly<nsresult> rvIgnored = CommitComposition();
  if (NS_WARN_IF(Destroyed())) {
    return NS_ERROR_EDITOR_DESTROYED;
  }
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rvIgnored),
                       "EditorBase::CommitComposition() failed, but ignored");

  // XXX Do we need to keep handling after committing composition causes moving
  //     focus to different element?  Although TextEditor has independent
  //     selection, so, we may not see any odd behavior even in such case.

  nsresult rv = SelectEntireDocument();
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "EditorBase::SelectEntireDocument() failed");
  return rv;
}

MOZ_CAN_RUN_SCRIPT_BOUNDARY NS_IMETHODIMP EditorBase::BeginningOfDocument() {
  MOZ_ASSERT(IsTextEditor());

  AutoEditActionDataSetter editActionData(*this, EditAction::eNotEditing);
  if (NS_WARN_IF(!editActionData.CanHandle())) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  // get the root element
  RefPtr<Element> rootElement = GetRoot();
  if (NS_WARN_IF(!rootElement)) {
    return NS_ERROR_NULL_POINTER;
  }

  // find first editable thingy
  nsCOMPtr<nsIContent> firstEditableLeaf;
  // If we're `TextEditor`, the first editable leaf node is a text node or
  // padding `<br>` element.  In the first case, we need to collapse selection
  // into it.
  if (rootElement->GetFirstChild() && rootElement->GetFirstChild()->IsText()) {
    firstEditableLeaf = rootElement->GetFirstChild();
  }
  if (!firstEditableLeaf) {
    // just the root node, set selection to inside the root
    nsresult rv = CollapseSelectionToStartOf(*rootElement);
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                         "EditorBase::CollapseSelectionToStartOf() failed");
    return rv;
  }

  if (firstEditableLeaf->IsText()) {
    // If firstEditableLeaf is text, set selection to beginning of the text
    // node.
    nsresult rv = CollapseSelectionToStartOf(*firstEditableLeaf);
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                         "EditorBase::CollapseSelectionToStartOf() failed");
    return rv;
  }

  // Otherwise, it's a leaf node and we set the selection just in front of it.
  nsCOMPtr<nsIContent> parent = firstEditableLeaf->GetParent();
  if (NS_WARN_IF(!parent)) {
    return NS_ERROR_NULL_POINTER;
  }

  MOZ_ASSERT(
      parent->ComputeIndexOf(firstEditableLeaf).valueOr(UINT32_MAX) == 0,
      "How come the first node isn't the left most child in its parent?");
  nsresult rv = CollapseSelectionToStartOf(*parent);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "EditorBase::CollapseSelectionToStartOf() failed");
  return rv;
}

NS_IMETHODIMP EditorBase::EndOfDocument() { return NS_ERROR_NOT_IMPLEMENTED; }

NS_IMETHODIMP EditorBase::GetDocumentModified(bool* aOutDocModified) {
  if (NS_WARN_IF(!aOutDocModified)) {
    return NS_ERROR_INVALID_ARG;
  }

  int32_t modCount = 0;
  DebugOnly<nsresult> rvIgnored = GetModificationCount(&modCount);
  NS_WARNING_ASSERTION(
      NS_SUCCEEDED(rvIgnored),
      "EditorBase::GetModificationCount() failed, but ignored");

  *aOutDocModified = (modCount != 0);
  return NS_OK;
}

NS_IMETHODIMP EditorBase::GetDocumentCharacterSet(
    nsACString& /*aCharacterSet*/) {
  return NS_ERROR_NOT_AVAILABLE;
}

nsresult EditorBase::GetDocumentCharsetInternal(nsACString& aCharset) const {
  Document* document = GetDocument();
  if (NS_WARN_IF(!document)) {
    return NS_ERROR_NOT_INITIALIZED;
  }
  document->GetDocumentCharacterSet()->Name(aCharset);
  return NS_OK;
}

NS_IMETHODIMP EditorBase::SetDocumentCharacterSet(
    const nsACString& /*aCharacterSet*/) {
  return NS_ERROR_NOT_AVAILABLE;
}

NS_IMETHODIMP EditorBase::OutputToString(const nsAString& aFormatType,
                                         uint32_t aDocumentEncoderFlags,
                                         nsAString& aOutputString) {
  AutoEditActionDataSetter editActionData(*this, EditAction::eNotEditing);
  if (NS_WARN_IF(!editActionData.CanHandle())) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  nsresult rv =
      ComputeValueInternal(aFormatType, aDocumentEncoderFlags, aOutputString);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "EditorBase::ComputeValueInternal() failed");
  // This is low level API for XUL application.  So, we should return raw
  // error code here.
  return rv;
}

nsresult EditorBase::ComputeValueInternal(const nsAString& aFormatType,
                                          uint32_t aDocumentEncoderFlags,
                                          nsAString& aOutputString) const {
  MOZ_ASSERT(IsEditActionDataAvailable());

  // First, let's try to get the value simply only from text node if the
  // caller wants plaintext value.
  if (aFormatType.LowerCaseEqualsLiteral("text/plain") &&
      !(aDocumentEncoderFlags & (nsIDocumentEncoder::OutputSelectionOnly |
                                 nsIDocumentEncoder::OutputWrap))) {
    // Shortcut for empty editor case.
    if (IsEmpty()) {
      aOutputString.Truncate();
      return NS_OK;
    }
    // NOTE: If it's neither <input type="text"> nor <textarea>, e.g., an HTML
    // editor which is in plaintext mode (e.g., plaintext email composer on
    // Thunderbird), it should be handled by the expensive path.
    if (IsTextEditor()) {
      // If it's necessary to check selection range or the editor wraps hard,
      // we need some complicated handling.  In such case, we need to use the
      // expensive path.
      // XXX Anything else what we cannot return the text node data simply?
      Result<EditActionResult, nsresult> result =
          AsTextEditor()->ComputeValueFromTextNodeAndBRElement(aOutputString);
      if (MOZ_UNLIKELY(result.isErr())) {
        NS_WARNING("TextEditor::ComputeValueFromTextNodeAndBRElement() failed");
        return result.unwrapErr();
      }
      if (!result.inspect().Ignored()) {
        return NS_OK;
      }
    }
  }

  nsAutoCString charset;
  nsresult rv = GetDocumentCharsetInternal(charset);
  if (NS_FAILED(rv) || charset.IsEmpty()) {
    charset.AssignLiteral("windows-1252");  // XXX Why don't we use "UTF-8"?
  }

  nsCOMPtr<nsIDocumentEncoder> encoder =
      GetAndInitDocEncoder(aFormatType, aDocumentEncoderFlags, charset);
  if (!encoder) {
    NS_WARNING("EditorBase::GetAndInitDocEncoder() failed");
    return NS_ERROR_FAILURE;
  }

  rv = encoder->EncodeToString(aOutputString);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "nsIDocumentEncoder::EncodeToString() failed");
  return rv;
}

already_AddRefed<nsIDocumentEncoder> EditorBase::GetAndInitDocEncoder(
    const nsAString& aFormatType, uint32_t aDocumentEncoderFlags,
    const nsACString& aCharset) const {
  MOZ_ASSERT(IsEditActionDataAvailable());

  nsCOMPtr<nsIDocumentEncoder> docEncoder;
  if (!mCachedDocumentEncoder ||
      !mCachedDocumentEncoderType.Equals(aFormatType)) {
    nsAutoCString formatType;
    LossyAppendUTF16toASCII(aFormatType, formatType);
    docEncoder = do_createDocumentEncoder(PromiseFlatCString(formatType).get());
    if (NS_WARN_IF(!docEncoder)) {
      return nullptr;
    }
    mCachedDocumentEncoder = docEncoder;
    mCachedDocumentEncoderType = aFormatType;
  } else {
    docEncoder = mCachedDocumentEncoder;
  }

  RefPtr<Document> doc = GetDocument();
  NS_ASSERTION(doc, "Need a document");

  nsresult rv = docEncoder->Init(
      doc, aFormatType,
      aDocumentEncoderFlags | nsIDocumentEncoder::RequiresReinitAfterOutput);
  if (NS_FAILED(rv)) {
    NS_WARNING("nsIDocumentEncoder::NativeInit() failed");
    return nullptr;
  }

  if (!aCharset.IsEmpty() && !aCharset.EqualsLiteral("null")) {
    DebugOnly<nsresult> rvIgnored = docEncoder->SetCharset(aCharset);
    NS_WARNING_ASSERTION(
        NS_SUCCEEDED(rvIgnored),
        "nsIDocumentEncoder::SetCharset() failed, but ignored");
  }

  const int32_t wrapWidth = std::max(WrapWidth(), 0);
  DebugOnly<nsresult> rvIgnored = docEncoder->SetWrapColumn(wrapWidth);
  NS_WARNING_ASSERTION(
      NS_SUCCEEDED(rvIgnored),
      "nsIDocumentEncoder::SetWrapColumn() failed, but ignored");

  // Set the selection, if appropriate.
  // We do this either if the OutputSelectionOnly flag is set,
  // in which case we use our existing selection ...
  if (aDocumentEncoderFlags & nsIDocumentEncoder::OutputSelectionOnly) {
    if (NS_FAILED(docEncoder->SetSelection(&SelectionRef()))) {
      NS_WARNING("nsIDocumentEncoder::SetSelection() failed");
      return nullptr;
    }
  }
  // ... or if the root element is not a body,
  // in which case we set the selection to encompass the root.
  else {
    Element* rootElement = GetRoot();
    if (NS_WARN_IF(!rootElement)) {
      return nullptr;
    }
    if (!rootElement->IsHTMLElement(nsGkAtoms::body)) {
      if (NS_FAILED(docEncoder->SetContainerNode(rootElement))) {
        NS_WARNING("nsIDocumentEncoder::SetContainerNode() failed");
        return nullptr;
      }
    }
  }

  return docEncoder.forget();
}

bool EditorBase::AreClipboardCommandsUnconditionallyEnabled() const {
  Document* document = GetDocument();
  return document && document->AreClipboardCommandsUnconditionallyEnabled();
}

bool EditorBase::CheckForClipboardCommandListener(
    nsAtom* aCommand, EventMessage aEventMessage) const {
  RefPtr<Document> document = GetDocument();
  if (!document) {
    return false;
  }

  // We exclude XUL and chrome docs here to maintain current behavior where
  // in these cases the editor element alone is expected to handle clipboard
  // command availability.
  if (!document->AreClipboardCommandsUnconditionallyEnabled()) {
    return false;
  }

  // So in web content documents, "unconditionally" enabled Cut/Copy are not
  // really unconditional; they're enabled if there is a listener that wants
  // to handle them. What they're not conditional on here is whether there is
  // currently a selection in the editor.
  RefPtr<PresShell> presShell = document->GetObservingPresShell();
  if (!presShell) {
    return false;
  }
  RefPtr<nsPresContext> presContext = presShell->GetPresContext();
  if (!presContext) {
    return false;
  }

  RefPtr<EventTarget> et = IsHTMLEditor()
                               ? AsHTMLEditor()->ComputeEditingHost(
                                     HTMLEditor::LimitInBodyElement::No)
                               : GetDOMEventTarget();

  while (et) {
    EventListenerManager* elm = et->GetExistingListenerManager();
    if (elm && elm->HasListenersFor(aCommand)) {
      return true;
    }
    InternalClipboardEvent event(true, aEventMessage);
    EventChainPreVisitor visitor(presContext, &event, nullptr,
                                 nsEventStatus_eIgnore, false, et);
    et->GetEventTargetParent(visitor);
    et = visitor.GetParentTarget();
  }

  return false;
}

already_AddRefed<DataTransfer> EditorBase::CreateDataTransferForPaste(
    EventMessage aEventMessage,
    nsIClipboard::ClipboardType aClipboardType) const {
  nsIGlobalObject* scopeObject = nullptr;
  if (PresShell* presShell = GetPresShell()) {
    if (Document* doc = presShell->GetDocument()) {
      scopeObject = doc->GetScopeObject();
    }
  }

  auto dataTransfer = MakeRefPtr<DataTransfer>(scopeObject, aEventMessage, true,
                                               Some(aClipboardType));
  return dataTransfer.forget();
}

already_AddRefed<DataTransfer> EditorBase::CreateDataTransferForPaste(
    EventMessage aEventMessage, nsITransferable* aTransferable) const {
  MOZ_ASSERT(aTransferable);
  nsIGlobalObject* scopeObject = nullptr;
  if (PresShell* presShell = GetPresShell()) {
    if (Document* doc = presShell->GetDocument()) {
      scopeObject = doc->GetScopeObject();
    }
  }

  auto dataTransfer =
      MakeRefPtr<DataTransfer>(scopeObject, aEventMessage, aTransferable);
  return dataTransfer.forget();
}

Result<EditorBase::ClipboardEventResult, nsresult>
EditorBase::DispatchClipboardEventAndUpdateClipboard(
    EventMessage aEventMessage,
    Maybe<nsIClipboard::ClipboardType> aClipboardType,
    DataTransfer* aDataTransfer /* = nullptr */) {
  MOZ_ASSERT(IsEditActionDataAvailable());

  // Clipboard events are fired before `beforeinput` event.  Therefore, we
  // need to forget mLastCollapsibleWhiteSpaceAppendedTextNode here to avoid
  // infinite loop caused by the hack.
  if (IsHTMLEditor()) {
    AsHTMLEditor()->mLastCollapsibleWhiteSpaceAppendedTextNode = nullptr;
  }

  const bool isPasting =
      aEventMessage == ePaste || aEventMessage == ePasteNoFormatting;
  if (isPasting) {
    CommitComposition();
    if (NS_WARN_IF(Destroyed())) {
      return Err(NS_ERROR_EDITOR_DESTROYED);
    }
  }

  RefPtr<PresShell> presShell = GetPresShell();
  if (NS_WARN_IF(!presShell)) {
    return Err(NS_ERROR_NOT_AVAILABLE);
  }

  const RefPtr<Selection> sel = [&]() {
    if (IsHTMLEditor() && aEventMessage == eCopy &&
        SelectionRef().IsCollapsed()) {
      // If we don't have a usable selection for copy and we're an HTML
      // editor (which is global for the document) try to use the last
      // focused selection instead.
      return nsCopySupport::GetSelectionForCopy(GetDocument());
    }
    return do_AddRef(&SelectionRef());
  }();

  const auto GetDOMEventName = [&]() -> const char* {
    switch (aEventMessage) {
      case eCopy:
        return "copy";
      case eCut:
        return "cut";
      case ePaste:
      case ePasteNoFormatting:
        return "paste";
      default:
        return ToChar(aEventMessage);
    }
  };

  MOZ_LOG(
      gEventLog, LogLevel::Info,
      ("%p %s: Dispatching \"%s\" event...", this,
       mIsHTMLEditorClass ? "HTMLEditor" : "TextEditor", GetDOMEventName()));
  bool actionTaken = false;
  const bool doDefault = nsCopySupport::FireClipboardEvent(
      aEventMessage, aClipboardType, presShell, sel, aDataTransfer,
      &actionTaken);
  MOZ_LOG(gEventLog, LogLevel::Info,
          ("%p %s: Dispatched \"%s\" event, defaultPrevented=%s", this,
           mIsHTMLEditorClass ? "HTMLEditor" : "TextEditor", GetDOMEventName(),
           doDefault ? "false" : "true"));
  NotifyOfDispatchingClipboardEvent();

  if (NS_WARN_IF(Destroyed())) {
    return Err(NS_ERROR_EDITOR_DESTROYED);
  }

  if (doDefault) {
    MOZ_ASSERT(actionTaken);
    return ClipboardEventResult::DoDefault;
  }
  // If we handle a "paste" and nsCopySupport::FireClipboardEvent sets
  // actionTaken to "false" means that it's an error.  Otherwise, the "paste"
  // event is just canceled.
  if (isPasting) {
    return actionTaken ? ClipboardEventResult::DefaultPreventedOfPaste
                       : ClipboardEventResult::IgnoredOrError;
  }
  // If we handle a "copy", actionTaken is set to true only when
  // nsCopySupport::FireClipboardEvent does not meet an error.
  // If we handle a "cut", actionTaken is set to true only when
  // nsCopySupport::FireClipboardEvent does not meet an error and
  // - the selection is collapsed in editable elements when the event is not
  //   canceled.
  // - the event is canceled but update the clipboard with the dataTransfer
  //   of the event.
  return actionTaken ? ClipboardEventResult::CopyOrCutHandled
                     : ClipboardEventResult::IgnoredOrError;
}

NS_IMETHODIMP EditorBase::Cut() {
  nsresult rv = CutAsAction();
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv), "EditorBase::CutAsAction() failed");
  return rv;
}

nsresult EditorBase::CutAsAction(nsIPrincipal* aPrincipal) {
  AutoEditActionDataSetter editActionData(*this, EditAction::eCut, aPrincipal);
  if (NS_WARN_IF(!editActionData.CanHandle())) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  {
    RefPtr<nsFocusManager> focusManager = nsFocusManager::GetFocusManager();
    if (NS_WARN_IF(!focusManager)) {
      return NS_ERROR_UNEXPECTED;
    }
    const RefPtr<Element> focusedElement = focusManager->GetFocusedElement();

    Result<ClipboardEventResult, nsresult> ret =
        DispatchClipboardEventAndUpdateClipboard(
            eCut, Some(nsIClipboard::kGlobalClipboard));
    if (MOZ_UNLIKELY(ret.isErr())) {
      NS_WARNING(
          "EditorBase::DispatchClipboardEventAndUpdateClipboard(eCut, "
          "nsIClipboard::kGlobalClipboard) failed");
      return EditorBase::ToGenericNSResult(ret.unwrapErr());
    }
    switch (ret.unwrap()) {
      case ClipboardEventResult::DoDefault:
        break;
      case ClipboardEventResult::CopyOrCutHandled:
        return NS_OK;
      case ClipboardEventResult::IgnoredOrError:
        return EditorBase::ToGenericNSResult(NS_ERROR_EDITOR_ACTION_CANCELED);
      case ClipboardEventResult::DefaultPreventedOfPaste:
        MOZ_ASSERT_UNREACHABLE("Invalid result for eCut");
    }

    // If focus is changed by a "cut" event listener, we should stop handling
    // the cut.
    const RefPtr<Element> newFocusedElement = focusManager->GetFocusedElement();
    if (MOZ_UNLIKELY(focusedElement != newFocusedElement)) {
      if (focusManager->GetFocusedWindow() != GetWindow()) {
        return NS_OK;
      }
      RefPtr<EditorBase> editorBase =
          nsContentUtils::GetActiveEditor(GetPresContext());
      if (!editorBase || (editorBase->IsHTMLEditor() &&
                          !editorBase->AsHTMLEditor()->IsActiveInDOMWindow())) {
        return NS_OK;
      }
      if (editorBase != this) {
        return NS_OK;
      }
    }
  }

  editActionData.SetSelectionCreatedByDoubleclick(
      SelectionRef().GetFrameSelection() &&
      SelectionRef().GetFrameSelection()->IsDoubleClickSelection());

  // Dispatch "beforeinput" event after dispatching "cut" event.
  nsresult rv = editActionData.MaybeDispatchBeforeInputEvent();
  if (NS_FAILED(rv)) {
    NS_WARNING_ASSERTION(rv == NS_ERROR_EDITOR_ACTION_CANCELED,
                         "MaybeDispatchBeforeInputEvent() failed");
    return EditorBase::ToGenericNSResult(rv);
  }
  // XXX This transaction name is referred by PlaceholderTransaction::Merge()
  //     so that we need to keep using it here.
  AutoPlaceholderBatch treatAsOneTransaction(*this, *nsGkAtoms::DeleteTxnName,
                                             ScrollSelectionIntoView::Yes,
                                             __FUNCTION__);
  rv = DeleteSelectionAsSubAction(
      eNone, IsTextEditor() ? nsIEditor::eNoStrip : nsIEditor::eStrip);
  NS_WARNING_ASSERTION(
      NS_SUCCEEDED(rv),
      "EditorBase::DeleteSelectionAsSubAction(eNone) failed, but ignored");
  return EditorBase::ToGenericNSResult(rv);
}

NS_IMETHODIMP EditorBase::CanCut(bool* aCanCut) {
  if (NS_WARN_IF(!aCanCut)) {
    return NS_ERROR_INVALID_ARG;
  }
  *aCanCut = IsCutCommandEnabled();
  return NS_OK;
}

bool EditorBase::IsCutCommandEnabled() const {
  AutoEditActionDataSetter editActionData(*this, EditAction::eNotEditing);
  if (NS_WARN_IF(!editActionData.CanHandle())) {
    return false;
  }

  if (IsModifiable() && IsCopyToClipboardAllowedInternal()) {
    return true;
  }

  // If there's an event listener for "cut", we always enable the command
  // as we don't really know what the listener may want to do in response.
  // We look up the event target chain for a possible listener on a parent
  // in addition to checking the immediate target.
  return CheckForClipboardCommandListener(nsGkAtoms::oncut, eCut);
}

NS_IMETHODIMP EditorBase::Copy() {
  AutoEditActionDataSetter editActionData(*this, EditAction::eCopy);
  if (NS_WARN_IF(!editActionData.CanHandle())) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  Result<ClipboardEventResult, nsresult> ret =
      DispatchClipboardEventAndUpdateClipboard(
          eCopy, Some(nsIClipboard::kGlobalClipboard));
  if (MOZ_UNLIKELY(ret.isErr())) {
    NS_WARNING(
        "EditorBase::DispatchClipboardEventAndUpdateClipboard(eCopy, "
        "nsIClipboard::kGlobalClipboard) failed");
    return EditorBase::ToGenericNSResult(ret.unwrapErr());
  }
  switch (ret.unwrap()) {
    case ClipboardEventResult::DoDefault:
    case ClipboardEventResult::CopyOrCutHandled:
      return NS_OK;
    case ClipboardEventResult::IgnoredOrError:
      return EditorBase::ToGenericNSResult(NS_ERROR_EDITOR_ACTION_CANCELED);
    case ClipboardEventResult::DefaultPreventedOfPaste:
      MOZ_ASSERT_UNREACHABLE("Invalid result for eCopy");
  }
  return NS_ERROR_UNEXPECTED;
}

NS_IMETHODIMP EditorBase::CanCopy(bool* aCanCopy) {
  if (NS_WARN_IF(!aCanCopy)) {
    return NS_ERROR_INVALID_ARG;
  }
  *aCanCopy = IsCopyCommandEnabled();
  return NS_OK;
}

bool EditorBase::IsCopyCommandEnabled() const {
  AutoEditActionDataSetter editActionData(*this, EditAction::eNotEditing);
  if (NS_WARN_IF(!editActionData.CanHandle())) {
    return false;
  }

  if (IsCopyToClipboardAllowedInternal()) {
    return true;
  }

  // Like "cut", always enable "copy" if there's a listener.
  return CheckForClipboardCommandListener(nsGkAtoms::oncopy, eCopy);
}

NS_IMETHODIMP EditorBase::Paste(nsIClipboard::ClipboardType aClipboardType) {
  if (uint32_t(aClipboardType) >= nsIClipboard::kClipboardTypeCount) {
    return NS_ERROR_INVALID_ARG;
  }
  const nsresult rv = PasteAsAction(aClipboardType, DispatchPasteEvent::Yes);
  NS_WARNING_ASSERTION(
      NS_SUCCEEDED(rv),
      "EditorBase::PasteAsAction(DispatchPasteEvent::Yes) failed");
  return rv;
}

nsresult EditorBase::PasteAsAction(nsIClipboard::ClipboardType aClipboardType,
                                   DispatchPasteEvent aDispatchPasteEvent,
                                   DataTransfer* aDataTransfer /* = nullptr */,
                                   nsIPrincipal* aPrincipal /* = nullptr */) {
  if (IsHTMLEditor() && IsReadonly()) {
    return NS_OK;
  }

  // Create the same DataTransfer object here so we can share it between
  // the clipboard event and the call to HandlePaste below. This prevents
  // race conditions with Content Analysis on like we see in bug 1918027.
  // Note that this is not needed if we're not going to dispatch the paste
  // event and no aDataTransfer was passed in.
  RefPtr<DataTransfer> dataTransfer = aDataTransfer;
  if (!aDataTransfer && aDispatchPasteEvent == DispatchPasteEvent::Yes) {
    dataTransfer = CreateDataTransferForPaste(ePaste, aClipboardType);
  }
  AutoEditActionDataSetter editActionData(*this, EditAction::ePaste,
                                          aPrincipal);
  const auto clearDataTransfer = MakeScopeExit([&] {
    // If the caller passed in aDataTransfer, they are responsible for clearing
    // this.
    if (!aDataTransfer && dataTransfer) {
      dataTransfer->ClearForPaste();
    }
  });
  if (NS_WARN_IF(!editActionData.CanHandle())) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  if (aDispatchPasteEvent == DispatchPasteEvent::Yes) {
    RefPtr<nsFocusManager> focusManager = nsFocusManager::GetFocusManager();
    if (NS_WARN_IF(!focusManager)) {
      return NS_ERROR_UNEXPECTED;
    }
    const RefPtr<Element> focusedElement = focusManager->GetFocusedElement();

    Result<ClipboardEventResult, nsresult> ret = Err(NS_ERROR_FAILURE);
    {
      // This method is not set up to pass back the new aDataTransfer
      // if it changes. If we need this in the future, we can change
      // aDataTransfer to be a RefPtr<DataTransfer>*.
      AutoTrackDataTransferForPaste trackDataTransfer(*this, dataTransfer);

      ret = DispatchClipboardEventAndUpdateClipboard(
          ePaste, Some(aClipboardType), dataTransfer);
      if (MOZ_UNLIKELY(ret.isErr())) {
        NS_WARNING(
            "EditorBase::DispatchClipboardEventAndUpdateClipboard(ePaste) "
            "failed");
        return EditorBase::ToGenericNSResult(ret.unwrapErr());
      }
    }
    switch (ret.inspect()) {
      case ClipboardEventResult::DoDefault:
        break;
      case ClipboardEventResult::DefaultPreventedOfPaste:
      case ClipboardEventResult::IgnoredOrError:
        return EditorBase::ToGenericNSResult(NS_ERROR_EDITOR_ACTION_CANCELED);
      case ClipboardEventResult::CopyOrCutHandled:
        MOZ_ASSERT_UNREACHABLE("Invalid result for ePaste");
    }

    // If focus is changed by a "paste" event listener, we should keep handling
    // the "pasting" in new focused editor because Chrome works as so.
    const RefPtr<Element> newFocusedElement = focusManager->GetFocusedElement();
    if (MOZ_UNLIKELY(focusedElement != newFocusedElement)) {
      // For the privacy reason, let's top handling it if new focused element is
      // in different document.
      if (focusManager->GetFocusedWindow() != GetWindow()) {
        return EditorBase::ToGenericNSResult(NS_ERROR_EDITOR_ACTION_CANCELED);
      }
      RefPtr<EditorBase> editorBase =
          nsContentUtils::GetActiveEditor(GetPresContext());
      if (!editorBase || (editorBase->IsHTMLEditor() &&
                          !editorBase->AsHTMLEditor()->IsActiveInDOMWindow())) {
        return EditorBase::ToGenericNSResult(NS_ERROR_EDITOR_ACTION_CANCELED);
      }
      if (editorBase != this) {
        nsresult rv = editorBase->PasteAsAction(
            aClipboardType, DispatchPasteEvent::No, dataTransfer, aPrincipal);
        NS_WARNING_ASSERTION(
            NS_SUCCEEDED(rv),
            "EditorBase::PasteAsAction(DispatchPasteEvent::No) failed");
        return EditorBase::ToGenericNSResult(rv);
      }
    }
    // Active EditContext could have changed in the paste handler.
    if (RefPtr<Document> document = GetDocument()) {
      // In certain cases, we don't update the active EditContext synchronously,
      // such as setting contenteditable on an ancestor of the focused
      // EditContext editor. So we need to ensure that it's up-to-date now.
      // TODO: Perhaps we can still update the active EditContext synchronously,
      //       and just fire events asynchronously in these cases?
      //       See https://github.com/w3c/edit-context/issues/127
      document->UpdateTextEditContext();
    }
    editActionData.UpdateEditContext();
  } else {
    // The caller must already have dispatched a "paste" event.
    editActionData.NotifyOfDispatchingClipboardEvent();
  }
  nsresult rv = HandlePaste(editActionData, aClipboardType, dataTransfer);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv), "EditorBase::HandlePaste() failed");
  return EditorBase::ToGenericNSResult(rv);
}

nsresult EditorBase::PasteNoFormattingAsAction(
    nsIClipboard::ClipboardType aClipboardType,
    DispatchPasteEvent aDispatchPasteEvent,
    DataTransfer* aDataTransfer /* = nullptr */,
    nsIPrincipal* aPrincipal /* = nullptr */) {
  if (IsReadonly()) {
    return NS_OK;
  }
  // Create the same DataTransfer object here so we can share it between
  // the clipboard event and its data with the call to
  // InsertFromTransferableWithSelection below. This prevents
  // race conditions with Content Analysis on like we see in bug 1918027.
  RefPtr<DataTransfer> dataTransfer =
      aDataTransfer ? RefPtr<DataTransfer>(aDataTransfer)
                    : RefPtr<DataTransfer>(CreateDataTransferForPaste(
                          ePasteNoFormatting, aClipboardType));

  auto clearDataTransfer = MakeScopeExit([&] {
    // If the caller passed in aDataTransfer, they are responsible for clearing
    // this.
    if (!aDataTransfer && dataTransfer) {
      dataTransfer->ClearForPaste();
    }
  });

  AutoEditActionDataSetter editActionData(*this, EditAction::ePaste,
                                          aPrincipal);
  if (NS_WARN_IF(!editActionData.CanHandle())) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  if (aDispatchPasteEvent == DispatchPasteEvent::Yes) {
    RefPtr<nsFocusManager> focusManager = nsFocusManager::GetFocusManager();
    if (NS_WARN_IF(!focusManager)) {
      return NS_ERROR_UNEXPECTED;
    }
    const RefPtr<Element> focusedElement = focusManager->GetFocusedElement();
    MOZ_ASSERT_IF(IsTextEditor(), focusedElement == GetExposedRoot());

    Result<ClipboardEventResult, nsresult> ret = Err(NS_ERROR_FAILURE);
    {
      // This method is not set up to pass back the new aDataTransfer
      // if it changes. If we need this in the future, we can change
      // aDataTransfer to be a RefPtr<DataTransfer>*.
      MOZ_ASSERT(!aDataTransfer);
      AutoTrackDataTransferForPaste trackDataTransfer(*this, dataTransfer);

      ret = DispatchClipboardEventAndUpdateClipboard(
          ePasteNoFormatting, Some(aClipboardType), dataTransfer);
      if (MOZ_UNLIKELY(ret.isErr())) {
        NS_WARNING(
            "EditorBase::DispatchClipboardEventAndUpdateClipboard("
            "ePasteNoFormatting) failed");
        return EditorBase::ToGenericNSResult(ret.unwrapErr());
      }
    }
    switch (ret.inspect()) {
      case ClipboardEventResult::DoDefault:
        break;
      case ClipboardEventResult::DefaultPreventedOfPaste:
      case ClipboardEventResult::IgnoredOrError:
        return EditorBase::ToGenericNSResult(NS_ERROR_EDITOR_ACTION_CANCELED);
      case ClipboardEventResult::CopyOrCutHandled:
        MOZ_ASSERT_UNREACHABLE("Invalid result for ePaste");
    }

    // If focus is changed by a "paste" event listener, we should keep handling
    // the "pasting" in new focused editor because Chrome works as so.
    const RefPtr<Element> newFocusedElement = focusManager->GetFocusedElement();
    if (MOZ_UNLIKELY(focusedElement != newFocusedElement)) {
      MOZ_ASSERT_IF(IsTextEditor(), newFocusedElement != GetExposedRoot());
      // For the privacy reason, let's top handling it if new focused element is
      // in different document.
      if (focusManager->GetFocusedWindow() != GetWindow()) {
        return EditorBase::ToGenericNSResult(NS_ERROR_EDITOR_ACTION_CANCELED);
      }
      RefPtr<EditorBase> editorBase =
          nsContentUtils::GetActiveEditor(GetPresContext());
      if (!editorBase || (editorBase->IsHTMLEditor() &&
                          !editorBase->AsHTMLEditor()->IsActiveInDOMWindow())) {
        return EditorBase::ToGenericNSResult(NS_ERROR_EDITOR_ACTION_CANCELED);
      }
      if (editorBase != this) {
        nsresult rv = editorBase->PasteNoFormattingAsAction(
            aClipboardType, DispatchPasteEvent::No, dataTransfer, aPrincipal);
        NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                             "EditorBase::PasteNoFormattingAsAction("
                             "DispatchPasteEvent::No) failed");
        return EditorBase::ToGenericNSResult(rv);
      }
    }
  }

  if (IsHTMLEditor()) {
    nsresult rv = MOZ_KnownLive(AsHTMLEditor())
                      ->HandlePasteNoFormatting(editActionData, aClipboardType,
                                                dataTransfer);
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                         "HTMLEditor::PasteNoFormattingAsAction() failed");
    return EditorBase::ToGenericNSResult(rv);
  }

  nsresult rv = HandlePaste(editActionData, aClipboardType, dataTransfer);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv), "EditorBase::HandlePaste() failed");
  return EditorBase::ToGenericNSResult(rv);
}

nsresult EditorBase::PasteAsQuotationAsAction(
    nsIClipboard::ClipboardType aClipboardType,
    DispatchPasteEvent aDispatchPasteEvent,
    DataTransfer* aDataTransfer /* = nullptr */,
    nsIPrincipal* aPrincipal /* = nullptr */) {
  MOZ_ASSERT(aClipboardType == nsIClipboard::kGlobalClipboard ||
             aClipboardType == nsIClipboard::kSelectionClipboard);

  if (IsHTMLEditor() && IsReadonly()) {
    return NS_OK;
  }

  // Create the same DataTransfer object here so we can share it between
  // the clipboard event and the call to HandlePasteAsQuotation below. This
  // prevents race conditions with Content Analysis on like we see in bug
  // 1918027.
  // Note that this is not needed if we're not going to dispatch the paste
  // event.
  RefPtr<DataTransfer> dataTransfer;
  if (aDispatchPasteEvent == DispatchPasteEvent::Yes) {
    dataTransfer = aDataTransfer
                       ? RefPtr<DataTransfer>(aDataTransfer)
                       : RefPtr<DataTransfer>(CreateDataTransferForPaste(
                             ePaste, aClipboardType));
  }
  const auto clearDataTransfer = MakeScopeExit([&] {
    // If the caller passed in aDataTransfer, they are responsible for clearing
    // this.
    if (!aDataTransfer && dataTransfer) {
      dataTransfer->ClearForPaste();
    }
  });
  AutoEditActionDataSetter editActionData(*this, EditAction::ePasteAsQuotation,
                                          aPrincipal);
  if (NS_WARN_IF(!editActionData.CanHandle())) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  if (aDispatchPasteEvent == DispatchPasteEvent::Yes) {
    RefPtr<nsFocusManager> focusManager = nsFocusManager::GetFocusManager();
    if (NS_WARN_IF(!focusManager)) {
      return NS_ERROR_UNEXPECTED;
    }
    const RefPtr<Element> focusedElement = focusManager->GetFocusedElement();

    Result<ClipboardEventResult, nsresult> ret = Err(NS_ERROR_FAILURE);
    {
      // This method is not set up to pass back the new aDataTransfer
      // if it changes. If we need this in the future, we can change
      // aDataTransfer to be a RefPtr<DataTransfer>*.
      MOZ_ASSERT(!aDataTransfer);
      AutoTrackDataTransferForPaste trackDataTransfer(*this, dataTransfer);

      ret = DispatchClipboardEventAndUpdateClipboard(
          ePaste, Some(aClipboardType), dataTransfer);
      if (MOZ_UNLIKELY(ret.isErr())) {
        NS_WARNING(
            "EditorBase::DispatchClipboardEventAndUpdateClipboard(ePaste) "
            "failed");
        return EditorBase::ToGenericNSResult(ret.unwrapErr());
      }
    }
    switch (ret.inspect()) {
      case ClipboardEventResult::DoDefault:
        break;
      case ClipboardEventResult::DefaultPreventedOfPaste:
      case ClipboardEventResult::IgnoredOrError:
        return EditorBase::ToGenericNSResult(NS_ERROR_EDITOR_ACTION_CANCELED);
      case ClipboardEventResult::CopyOrCutHandled:
        MOZ_ASSERT_UNREACHABLE("Invalid result for ePaste");
    }

    // If focus is changed by a "paste" event listener, we should keep handling
    // the "pasting" in new focused editor because Chrome works as so.
    const RefPtr<Element> newFocusedElement = focusManager->GetFocusedElement();
    if (MOZ_UNLIKELY(focusedElement != newFocusedElement)) {
      // For the privacy reason, let's top handling it if new focused element is
      // in different document.
      if (focusManager->GetFocusedWindow() != GetWindow()) {
        return EditorBase::ToGenericNSResult(NS_ERROR_EDITOR_ACTION_CANCELED);
      }
      RefPtr<EditorBase> editorBase =
          nsContentUtils::GetActiveEditor(GetPresContext());
      if (!editorBase || (editorBase->IsHTMLEditor() &&
                          !editorBase->AsHTMLEditor()->IsActiveInDOMWindow())) {
        return EditorBase::ToGenericNSResult(NS_ERROR_EDITOR_ACTION_CANCELED);
      }
      if (editorBase != this) {
        nsresult rv = editorBase->PasteAsQuotationAsAction(
            aClipboardType, DispatchPasteEvent::No, dataTransfer, aPrincipal);
        NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                             "EditorBase::PasteAsQuotationAsAction("
                             "DispatchPasteEvent::No) failed");
        return EditorBase::ToGenericNSResult(rv);
      }
    }
  } else {
    // The caller must already have dispatched a "paste" event.
    editActionData.NotifyOfDispatchingClipboardEvent();
  }

  nsresult rv =
      HandlePasteAsQuotation(editActionData, aClipboardType, dataTransfer);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "EditorBase::HandlePasteAsQuotation() failed");
  return EditorBase::ToGenericNSResult(rv);
}

nsresult EditorBase::PasteTransferableAsAction(
    nsITransferable* aTransferable, DispatchPasteEvent aDispatchPasteEvent,
    nsIPrincipal* aPrincipal /* = nullptr */,
    DataTransfer* aDataTransfer /* = nullptr */) {
  // FIXME: This may be called as a call of nsIEditor::PasteTransferable.
  // In this case, we should keep handling the paste even in the readonly mode.
  if (IsHTMLEditor() && IsReadonly()) {
    return NS_OK;
  }

  AutoEditActionDataSetter editActionData(*this, EditAction::ePaste,
                                          aPrincipal);
  if (NS_WARN_IF(!editActionData.CanHandle())) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  // The data to paste comes from aTransferable rather than from a clipboard,
  // so expose that through the paste event and use a nothing value for the
  // clipboard type. Creating the DataTransfer consumes the input streams in
  // aTransferable, so it must be created at most once and shared with
  // HandlePasteTransferable().
  RefPtr<DataTransfer> dataTransfer = aDataTransfer;
  if (!dataTransfer && aTransferable) {
    dataTransfer = CreateDataTransferForPaste(ePaste, aTransferable);
  }

  if (aDispatchPasteEvent == DispatchPasteEvent::Yes) {
    RefPtr<nsFocusManager> focusManager = nsFocusManager::GetFocusManager();
    if (NS_WARN_IF(!focusManager)) {
      return NS_ERROR_UNEXPECTED;
    }
    const RefPtr<Element> focusedElement = focusManager->GetFocusedElement();

    Result<ClipboardEventResult, nsresult> ret =
        DispatchClipboardEventAndUpdateClipboard(ePaste, Nothing(),
                                                 dataTransfer);
    if (MOZ_UNLIKELY(ret.isErr())) {
      NS_WARNING(
          "EditorBase::DispatchClipboardEventAndUpdateClipboard(ePaste) "
          "failed");
      return EditorBase::ToGenericNSResult(ret.unwrapErr());
    }
    switch (ret.inspect()) {
      case ClipboardEventResult::DoDefault:
        break;
      case ClipboardEventResult::DefaultPreventedOfPaste:
      case ClipboardEventResult::IgnoredOrError:
        return EditorBase::ToGenericNSResult(NS_ERROR_EDITOR_ACTION_CANCELED);
      case ClipboardEventResult::CopyOrCutHandled:
        MOZ_ASSERT_UNREACHABLE("Invalid result for ePaste");
    }

    // If focus is changed by a "paste" event listener, we should keep handling
    // the "pasting" in new focused editor because Chrome works as so.
    const RefPtr<Element> newFocusedElement = focusManager->GetFocusedElement();
    if (MOZ_UNLIKELY(focusedElement != newFocusedElement)) {
      // For the privacy reason, let's top handling it if new focused element is
      // in different document.
      if (focusManager->GetFocusedWindow() != GetWindow()) {
        return EditorBase::ToGenericNSResult(NS_ERROR_EDITOR_ACTION_CANCELED);
      }
      RefPtr<EditorBase> editorBase =
          nsContentUtils::GetActiveEditor(GetPresContext());
      if (!editorBase || (editorBase->IsHTMLEditor() &&
                          !editorBase->AsHTMLEditor()->IsActiveInDOMWindow())) {
        return EditorBase::ToGenericNSResult(NS_ERROR_EDITOR_ACTION_CANCELED);
      }
      if (editorBase != this) {
        nsresult rv = editorBase->PasteTransferableAsAction(
            aTransferable, DispatchPasteEvent::No, aPrincipal, dataTransfer);
        NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                             "EditorBase::PasteTransferableAsAction("
                             "DispatchPasteEvent::No) failed");
        return EditorBase::ToGenericNSResult(rv);
      }
    }
  } else {
    // The caller must already have dispatched a "paste" event.
    editActionData.NotifyOfDispatchingClipboardEvent();
  }

  if (NS_WARN_IF(!aTransferable)) {
    return NS_ERROR_INVALID_ARG;
  }

  nsresult rv =
      HandlePasteTransferable(editActionData, *aTransferable, dataTransfer);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "EditorBase::HandlePasteTransferable() failed");
  return EditorBase::ToGenericNSResult(rv);
}

nsresult EditorBase::PrepareToInsertContent(
    const EditorDOMPoint& aPointToInsert,
    DeleteSelectedContent aDeleteSelectedContent) {
  // TODO: Move this method to `EditorBase`.
  MOZ_ASSERT(IsEditActionDataAvailable());

  MOZ_ASSERT(aPointToInsert.IsSet());

  EditorDOMPoint pointToInsert(aPointToInsert);
  if (aDeleteSelectedContent == DeleteSelectedContent::Yes) {
    AutoTrackDOMPoint tracker(RangeUpdaterRef(), &pointToInsert);
    nsresult rv = DeleteSelectionAsSubAction(
        nsIEditor::eNone,
        IsTextEditor() ? nsIEditor::eNoStrip : nsIEditor::eStrip);
    if (NS_FAILED(rv)) {
      NS_WARNING("EditorBase::DeleteSelectionAsSubAction(eNone) failed");
      return rv;
    }
  }

  nsresult rv = CollapseSelectionTo(pointToInsert);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "EditorBase::CollapseSelectionTo() failed");
  return rv;
}

nsresult EditorBase::InsertTextAt(
    const nsAString& aStringToInsert, const EditorDOMPoint& aPointToInsert,
    DeleteSelectedContent aDeleteSelectedContent) {
  MOZ_ASSERT(IsEditActionDataAvailable());
  MOZ_ASSERT(aPointToInsert.IsSet());

  nsresult rv = PrepareToInsertContent(aPointToInsert, aDeleteSelectedContent);
  if (NS_FAILED(rv)) {
    NS_WARNING("EditorBase::PrepareToInsertContent() failed");
    return rv;
  }

  rv = InsertTextAsSubAction(aStringToInsert, InsertTextFor::NormalText);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "EditorBase::InsertTextAsSubAction() failed");
  return rv;
}

EditorBase::SafeToInsertData EditorBase::IsSafeToInsertData(
    nsIPrincipal* aSourcePrincipal) const {
  // Try to determine whether we should use a sanitizing fragment sink
  RefPtr<Document> destdoc = GetDocument();
  NS_ASSERTION(destdoc, "Where is our destination doc?");

  nsIDocShell* docShell = nullptr;
  if (RefPtr<BrowsingContext> bc = destdoc->GetBrowsingContext()) {
    RefPtr<BrowsingContext> root = bc->Top();
    MOZ_ASSERT(root, "root should not be null");

    docShell = root->GetDocShell();
  }

  bool isSafe =
      docShell && docShell->GetAppType() == nsIDocShell::APP_TYPE_EDITOR;

  if (!isSafe && aSourcePrincipal) {
    nsIPrincipal* destPrincipal = destdoc->NodePrincipal();
    NS_ASSERTION(destPrincipal, "How come we don't have a principal?");
    DebugOnly<nsresult> rvIgnored =
        aSourcePrincipal->Subsumes(destPrincipal, &isSafe);
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rvIgnored),
                         "nsIPrincipal::Subsumes() failed, but ignored");
  }

  return isSafe ? SafeToInsertData::Yes : SafeToInsertData::No;
}

NS_IMETHODIMP EditorBase::PasteTransferable(nsITransferable* aTransferable) {
  nsresult rv =
      PasteTransferableAsAction(aTransferable, DispatchPasteEvent::Yes);
  NS_WARNING_ASSERTION(
      NS_SUCCEEDED(rv),
      "EditorBase::PasteTransferableAsAction(DispatchPasteEvent::Yes) failed");
  return rv;
}

NS_IMETHODIMP EditorBase::CanPaste(nsIClipboard::ClipboardType aClipboardType,
                                   bool* aCanPaste) {
  if (uint32_t(aClipboardType) >= nsIClipboard::kClipboardTypeCount) {
    return NS_ERROR_INVALID_ARG;
  }
  if (NS_WARN_IF(!aCanPaste)) {
    return NS_ERROR_INVALID_ARG;
  }
  *aCanPaste = CanPaste(aClipboardType);
  return NS_OK;
}

NS_IMETHODIMP EditorBase::SetAttribute(Element* aElement,
                                       const nsAString& aAttribute,
                                       const nsAString& aValue) {
  if (NS_WARN_IF(aAttribute.IsEmpty()) || NS_WARN_IF(!aElement)) {
    return NS_ERROR_INVALID_ARG;
  }

  AutoEditActionDataSetter editActionData(*this, EditAction::eSetAttribute);
  nsresult rv = editActionData.CanHandleAndMaybeDispatchBeforeInputEvent();
  if (NS_FAILED(rv)) {
    NS_WARNING_ASSERTION(rv == NS_ERROR_EDITOR_ACTION_CANCELED,
                         "CanHandleAndMaybeDispatchBeforeInputEvent() failed");
    return EditorBase::ToGenericNSResult(rv);
  }

  RefPtr<nsAtom> attribute = NS_Atomize(aAttribute);
  rv = SetAttributeWithTransaction(*aElement, *attribute, aValue);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "EditorBase::SetAttributeWithTransaction() failed");
  return EditorBase::ToGenericNSResult(rv);
}

nsresult EditorBase::SetAttributeWithTransaction(Element& aElement,
                                                 nsAtom& aAttribute,
                                                 const nsAString& aValue) {
  const RefPtr<ChangeAttributeTransaction> transaction =
      ChangeAttributeTransaction::Create(*this, aElement, aAttribute, aValue);
  nsresult rv = DoTransactionInternal(transaction);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "EditorBase::DoTransactionInternal() failed");
  return rv;
}

NS_IMETHODIMP EditorBase::RemoveAttribute(Element* aElement,
                                          const nsAString& aAttribute) {
  if (NS_WARN_IF(aAttribute.IsEmpty()) || NS_WARN_IF(!aElement)) {
    return NS_ERROR_INVALID_ARG;
  }

  AutoEditActionDataSetter editActionData(*this, EditAction::eRemoveAttribute);
  nsresult rv = editActionData.CanHandleAndMaybeDispatchBeforeInputEvent();
  if (NS_FAILED(rv)) {
    NS_WARNING_ASSERTION(rv == NS_ERROR_EDITOR_ACTION_CANCELED,
                         "CanHandleAndMaybeDispatchBeforeInputEvent() failed");
    return EditorBase::ToGenericNSResult(rv);
  }

  RefPtr<nsAtom> attribute = NS_Atomize(aAttribute);
  rv = RemoveAttributeWithTransaction(*aElement, *attribute);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "EditorBase::RemoveAttributeWithTransaction() failed");
  return EditorBase::ToGenericNSResult(rv);
}

nsresult EditorBase::RemoveAttributeWithTransaction(Element& aElement,
                                                    nsAtom& aAttribute) {
  if (!aElement.HasAttr(&aAttribute)) {
    return NS_OK;
  }
  const RefPtr<ChangeAttributeTransaction> transaction =
      ChangeAttributeTransaction::CreateToRemove(*this, aElement, aAttribute);
  nsresult rv = DoTransactionInternal(transaction);
  if (NS_WARN_IF(Destroyed())) {
    return NS_ERROR_EDITOR_DESTROYED;
  }
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "EditorBase::DoTransactionInternal() failed");
  return rv;
}

nsresult EditorBase::MarkElementDirty(Element& aElement) {
  // Mark the node dirty, but not for webpages (bug 599983)
  if (!OutputsMozDirty()) {
    return NS_OK;
  }
  nsresult rv = AutoElementAttrAPIWrapper(*this, aElement)
                    .SetAttr(nsGkAtoms::mozdirty, EmptyString(), false);
  NS_WARNING_ASSERTION(
      NS_SUCCEEDED(rv),
      "AutoElementAttrAPIWrapper::SetAttr() failed, but ignored");
  return rv;
}

NS_IMETHODIMP EditorBase::GetInlineSpellChecker(
    bool aAutoCreate, nsIInlineSpellChecker** aInlineSpellChecker) {
  if (NS_WARN_IF(!aInlineSpellChecker)) {
    return NS_ERROR_INVALID_ARG;
  }

  if (mDidPreDestroy) {
    // Don't allow people to get or create the spell checker once the editor
    // is going away.
    *aInlineSpellChecker = nullptr;
    return aAutoCreate ? NS_ERROR_NOT_AVAILABLE : NS_OK;
  }

  // We don't want to show the spell checking UI if there are no spell check
  // dictionaries available.
  if (!mozInlineSpellChecker::CanEnableInlineSpellChecking()) {
    *aInlineSpellChecker = nullptr;
    return NS_ERROR_FAILURE;
  }

  if (!mInlineSpellChecker && aAutoCreate) {
    mInlineSpellChecker = new mozInlineSpellChecker();
  }

  if (mInlineSpellChecker) {
    nsresult rv = mInlineSpellChecker->Init(this);
    if (NS_FAILED(rv)) {
      NS_WARNING("mozInlineSpellChecker::Init() failed");
      mInlineSpellChecker = nullptr;
      return rv;
    }
  }

  *aInlineSpellChecker = do_AddRef(mInlineSpellChecker).take();
  return NS_OK;
}

void EditorBase::SyncRealTimeSpell() {
  AutoEditActionDataSetter editActionData(*this, EditAction::eNotEditing);
  if (NS_WARN_IF(!editActionData.CanHandle())) {
    return;
  }

  bool enable = GetDesiredSpellCheckState();

  // Initializes mInlineSpellChecker
  nsCOMPtr<nsIInlineSpellChecker> spellChecker;
  DebugOnly<nsresult> rvIgnored =
      GetInlineSpellChecker(enable, getter_AddRefs(spellChecker));
  NS_WARNING_ASSERTION(
      NS_SUCCEEDED(rvIgnored),
      "EditorBase::GetInlineSpellChecker() failed, but ignored");

  if (mInlineSpellChecker) {
    if (!mSpellCheckerDictionaryUpdated && enable) {
      DebugOnly<nsresult> rvIgnored =
          mInlineSpellChecker->UpdateCurrentDictionary();
      NS_WARNING_ASSERTION(NS_SUCCEEDED(rvIgnored),
                           "mozInlineSpellChecker::UpdateCurrentDictionary() "
                           "failed, but ignored");
      mSpellCheckerDictionaryUpdated = true;
    }

    // We might have a mInlineSpellChecker even if there are no dictionaries
    // available since we don't destroy the mInlineSpellChecker when the last
    // dictionariy is removed, but in that case spellChecker is null
    DebugOnly<nsresult> rvIgnored =
        mInlineSpellChecker->SetEnableRealTimeSpell(enable && spellChecker);
    NS_WARNING_ASSERTION(
        NS_SUCCEEDED(rvIgnored),
        "mozInlineSpellChecker::SetEnableRealTimeSpell() failed, but ignored");
  }
}

NS_IMETHODIMP EditorBase::SetSpellcheckUserOverride(bool enable) {
  mSpellcheckCheckboxState = enable ? eTriTrue : eTriFalse;
  SyncRealTimeSpell();
  return NS_OK;
}

NS_IMETHODIMP EditorBase::InsertNode(nsINode* aNodeToInsert,
                                     nsINode* aContainer, uint32_t aOffset,
                                     bool aPreserveSelection,
                                     uint8_t aOptionalArgCount) {
  MOZ_DIAGNOSTIC_ASSERT(IsHTMLEditor());

  nsCOMPtr<nsIContent> contentToInsert =
      nsIContent::FromNodeOrNull(aNodeToInsert);
  if (NS_WARN_IF(!contentToInsert) || NS_WARN_IF(!aContainer)) {
    return NS_ERROR_NULL_POINTER;
  }

  AutoEditActionDataSetter editActionData(*this, EditAction::eInsertNode);
  nsresult rv = editActionData.CanHandleAndMaybeDispatchBeforeInputEvent();
  if (NS_FAILED(rv)) {
    NS_WARNING_ASSERTION(rv == NS_ERROR_EDITOR_ACTION_CANCELED,
                         "CanHandleAndMaybeDispatchBeforeInputEvent() failed");
    return EditorBase::ToGenericNSResult(rv);
  }

  // Make dispatch `input` event after stopping preserving selection.
  AutoPlaceholderBatch treatAsOneTransaction(
      *this,
      ScrollSelectionIntoView::No,  // not a user interaction
      __FUNCTION__);

  Maybe<AutoTransactionsConserveSelection> preseveSelection;
  if (aOptionalArgCount && aPreserveSelection) {
    preseveSelection.emplace(*this);
  }

  const uint32_t offset = std::min(aOffset, aContainer->Length());
  Result<CreateContentResult, nsresult> insertContentResult =
      InsertNodeWithTransaction(*contentToInsert,
                                EditorDOMPoint(aContainer, offset));
  if (MOZ_UNLIKELY(insertContentResult.isErr())) {
    NS_WARNING("EditorBase::InsertNodeWithTransaction() failed");
    return EditorBase::ToGenericNSResult(insertContentResult.unwrapErr());
  }
  rv = insertContentResult.inspect().SuggestCaretPointTo(
      *this, {SuggestCaret::OnlyIfHasSuggestion,
              SuggestCaret::OnlyIfTransactionsAllowedToDoIt,
              SuggestCaret::AndIgnoreTrivialError});
  if (NS_FAILED(rv)) {
    NS_WARNING("CreateContentResult::SuggestCaretPointTo() failed");
    return EditorBase::ToGenericNSResult(rv);
  }
  NS_WARNING_ASSERTION(
      rv != NS_SUCCESS_EDITOR_BUT_IGNORED_TRIVIAL_ERROR,
      "CreateContentResult::SuggestCaretPointTo() failed, but ignored");
  return NS_OK;
}

template <typename ContentNodeType>
Result<CreateNodeResultBase<ContentNodeType>, nsresult>
EditorBase::InsertNodeWithTransaction(ContentNodeType& aContentToInsert,
                                      const EditorDOMPoint& aPointToInsert) {
  MOZ_ASSERT(IsEditActionDataAvailable());
  MOZ_ASSERT_IF(IsTextEditor(), !aContentToInsert.IsText());

  if (NS_WARN_IF(!aPointToInsert.IsSet())) {
    return Err(NS_ERROR_INVALID_ARG);
  }
  MOZ_ASSERT(aPointToInsert.IsSetAndValid());

  IgnoredErrorResult ignoredError;
  AutoEditSubActionNotifier startToHandleEditSubAction(
      *this, EditSubAction::eInsertNode, nsIEditor::eNext, ignoredError);
  if (NS_WARN_IF(ignoredError.ErrorCodeIs(NS_ERROR_EDITOR_DESTROYED))) {
    return Err(ignoredError.StealNSResult());
  }
  NS_WARNING_ASSERTION(
      !ignoredError.Failed(),
      "TextEditor::OnStartToHandleTopLevelEditSubAction() failed, but ignored");

  RefPtr<InsertNodeTransaction> transaction =
      InsertNodeTransaction::Create(*this, aContentToInsert, aPointToInsert);
  nsresult rv = DoTransactionInternal(transaction);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "EditorBase::DoTransactionInternal() failed");

  DebugOnly<nsresult> rvIgnored =
      RangeUpdaterRef().SelAdjInsertNode(aPointToInsert);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rvIgnored),
                       "RangeUpdater::SelAdjInsertNode() failed, but ignored");

  if (NS_WARN_IF(Destroyed())) {
    return Err(NS_ERROR_EDITOR_DESTROYED);
  }
  if (NS_WARN_IF(aContentToInsert.GetParentNode() !=
                 aPointToInsert.GetContainer())) {
    return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
  }
  if (NS_FAILED(rv)) {
    return Err(rv);
  }

  if (IsHTMLEditor()) {
    TopLevelEditSubActionDataRef().DidInsertContent(*this, aContentToInsert);
  }

  return CreateNodeResultBase<ContentNodeType>(
      aContentToInsert, transaction->SuggestPointToPutCaret<EditorDOMPoint>());
}

Result<CreateElementResult, nsresult>
EditorBase::InsertPaddingBRElementForEmptyLastLineWithTransaction(
    const EditorDOMPoint& aPointToInsert) {
  MOZ_ASSERT(IsEditActionDataAvailable());
  MOZ_ASSERT(IsHTMLEditor() || !aPointToInsert.IsInTextNode());

  if (MOZ_UNLIKELY(!aPointToInsert.IsSet())) {
    return Err(NS_ERROR_FAILURE);
  }

  EditorDOMPoint pointToInsert;
  if (IsTextEditor()) {
    pointToInsert = aPointToInsert;
  } else {
    Result<EditorDOMPoint, nsresult> maybePointToInsert =
        MOZ_KnownLive(AsHTMLEditor())
            ->PrepareToInsertLineBreak(HTMLEditor::LineBreakType::BRElement,
                                       aPointToInsert);
    if (maybePointToInsert.isErr()) {
      return maybePointToInsert.propagateErr();
    }
    MOZ_ASSERT(maybePointToInsert.inspect().IsSetAndValid());
    pointToInsert = maybePointToInsert.unwrap();
  }

  Result<CreateElementResult, nsresult> insertBRElementResultOrError =
      InsertBRElement(WithTransaction::Yes,
                      BRElementType::PaddingForEmptyLastLine, pointToInsert);
  NS_WARNING_ASSERTION(insertBRElementResultOrError.isOk(),
                       "EditorBase::InsertBRElement(WithTransaction::Yes, "
                       "BRElementType::PaddingForEmptyLastLine) failed");
  return insertBRElementResultOrError;
}

nsresult EditorBase::UpdateBRElementType(HTMLBRElement& aBRElement,
                                         BRElementType aNewType) {
  const bool brElementIsHidden = aBRElement.IsPaddingForEmptyEditor() ||
                                 aBRElement.IsPaddingForEmptyLastLine();
  const bool brElementWillBeHidden = aNewType != BRElementType::Normal;
  const auto SetBRElementFlags = [&]() {
    switch (aNewType) {
      case BRElementType::Normal:
        if (brElementIsHidden) {
          aBRElement.UnsetFlags(NS_PADDING_FOR_EMPTY_EDITOR |
                                NS_PADDING_FOR_EMPTY_LAST_LINE);
        }
        break;
      case BRElementType::PaddingForEmptyEditor:
        if (brElementIsHidden) {
          aBRElement.UnsetFlags(NS_PADDING_FOR_EMPTY_LAST_LINE);
        }
        aBRElement.SetFlags(NS_PADDING_FOR_EMPTY_EDITOR);
        break;
      case BRElementType::PaddingForEmptyLastLine:
        if (brElementIsHidden) {
          aBRElement.UnsetFlags(NS_PADDING_FOR_EMPTY_EDITOR);
        }
        aBRElement.SetFlags(NS_PADDING_FOR_EMPTY_LAST_LINE);
        break;
    }
  };
  // If the <br> element is in the composed doc, it must be observed by
  // IMEContentObserver.  However, IMEContentObserver cannot observe the state
  // change, but changing the <br> type may make the <br> element visible or
  // invisible for ContentEventHandler.  Therefore, IMEContentObserver needs to
  // notify IME of the state change as a text change notification of adding or
  // removing a line break.  Therefore, we need to reconnect the <br> element
  // temporarily for making IMEContentObserver observable this change.
  if (!aBRElement.IsInComposedDoc() ||
      brElementIsHidden == brElementWillBeHidden) {
    SetBRElementFlags();
    return NS_OK;
  }
  EditorDOMPoint pointToInsert(&aBRElement);
  {
    AutoEditorDOMPointChildInvalidator lockOffset(pointToInsert);
    nsresult rv = DeleteNodeWithTransaction(aBRElement);
    if (NS_FAILED(rv)) {
      NS_WARNING("EditorBase::DeleteNodeWithTransaction() failed");
      return rv;
    }
  }
  if (NS_WARN_IF(!pointToInsert.IsSetAndValid())) {
    return NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE;
  }
  SetBRElementFlags();
  Result<CreateElementResult, nsresult> result =
      InsertNodeWithTransaction<Element>(aBRElement, pointToInsert);
  if (MOZ_UNLIKELY(result.isErr())) {
    NS_WARNING("EditorBase::InsertNodeWithTransaction() failed");
    return result.unwrapErr();
  }
  result.inspect().IgnoreCaretPointSuggestion();
  return NS_OK;
}

Result<CreateElementResult, nsresult> EditorBase::InsertBRElement(
    WithTransaction aWithTransaction, BRElementType aBRElementType,
    const EditorDOMPoint& aPointToInsert) {
  MOZ_ASSERT(aPointToInsert.IsSetAndValid());

  const RefPtr<HTMLBRElement> newBRElement =
      HTMLBRElement::FromNodeOrNull(RefPtr{CreateHTMLContent(nsGkAtoms::br)});
  if (MOZ_UNLIKELY(!newBRElement)) {
    NS_WARNING("EditorBase::CreateHTMLContent() failed");
    return Err(NS_ERROR_FAILURE);
  }
  nsresult rv = MarkElementDirty(*newBRElement);
  if (MOZ_UNLIKELY(rv == NS_ERROR_EDITOR_DESTROYED)) {
    NS_WARNING("EditorBase::MarkElementDirty() caused destroying the editor");
    return Err(NS_ERROR_EDITOR_DESTROYED);
  }
  if (aBRElementType != BRElementType::Normal) {
    nsresult rv = UpdateBRElementType(*newBRElement, aBRElementType);
    if (NS_FAILED(rv)) {
      NS_WARNING("EditorBase::UpdateBRElementType() failed");
      return Err(rv);
    }
  }
  if (aWithTransaction == WithTransaction::Yes) {
    Result<CreateElementResult, nsresult> insertBRElementResultOrError =
        InsertNodeWithTransaction<Element>(*newBRElement, aPointToInsert);
    if (MOZ_UNLIKELY(insertBRElementResultOrError.isErr())) {
      NS_WARNING("EditorBase::InsertNodeWithTransaction() failed");
      return insertBRElementResultOrError.propagateErr();
    }
    CreateElementResult insertBRElementResult =
        insertBRElementResultOrError.unwrap();
    insertBRElementResult.IgnoreCaretPointSuggestion();
  } else {
    (void)aPointToInsert.Offset();
    RefPtr<InsertNodeTransaction> transaction =
        InsertNodeTransaction::Create(*this, *newBRElement, aPointToInsert);
    nsresult rv = transaction->DoTransaction();
    if (NS_WARN_IF(Destroyed())) {
      return Err(NS_ERROR_EDITOR_DESTROYED);
    }
    if (NS_FAILED(rv)) {
      NS_WARNING("InsertNodeTransaction::DoTransaction() failed");
      return Err(rv);
    }
    RangeUpdaterRef().SelAdjInsertNode(EditorRawDOMPoint(
        aPointToInsert.GetContainer(), aPointToInsert.Offset()));
  }
  if (NS_WARN_IF(newBRElement->GetParentNode() !=
                 aPointToInsert.GetContainer())) {
    return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
  }
  return CreateElementResult(
      *newBRElement,
      EditorDOMPoint(newBRElement, aBRElementType == BRElementType::Normal
                                       ? InterlinePosition::StartOfNextLine
                                       : InterlinePosition::EndOfLine));
}

NS_IMETHODIMP EditorBase::DeleteNode(nsINode* /*aNode*/,
                                     bool /*aPreserveSelection*/,
                                     uint8_t /*aOptionalArgCount*/) {
  MOZ_ASSERT_UNREACHABLE("Do not use this API with TextEditor");
  return NS_ERROR_NOT_IMPLEMENTED;
}

nsresult EditorBase::DeleteNodeWithTransaction(nsIContent& aContent) {
  MOZ_ASSERT(IsEditActionDataAvailable());
  MOZ_ASSERT(!GetEditActionEditContext());
  MOZ_ASSERT_IF(IsTextEditor(), !aContent.IsText());

  // Do nothing if the node is read-only.
  if (IsHTMLEditor() && NS_WARN_IF(!HTMLEditUtils::IsRemovableNode(aContent))) {
    return NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE;
  }

  IgnoredErrorResult ignoredError;
  AutoEditSubActionNotifier startToHandleEditSubAction(
      *this, EditSubAction::eDeleteNode, nsIEditor::ePrevious, ignoredError);
  if (NS_WARN_IF(ignoredError.ErrorCodeIs(NS_ERROR_EDITOR_DESTROYED))) {
    return ignoredError.StealNSResult();
  }
  NS_WARNING_ASSERTION(
      !ignoredError.Failed(),
      "TextEditor::OnStartToHandleTopLevelEditSubAction() failed, but ignored");

  if (IsHTMLEditor()) {
    TopLevelEditSubActionDataRef().WillDeleteContent(*this, aContent);
  }

  // FYI: DeleteNodeTransaction grabs aContent while it's alive.  So, it's safe
  //      to refer aContent even after calling DoTransaction().
  RefPtr<DeleteNodeTransaction> deleteNodeTransaction =
      DeleteNodeTransaction::MaybeCreate(*this, aContent);
  NS_WARNING_ASSERTION(deleteNodeTransaction,
                       "DeleteNodeTransaction::MaybeCreate() failed");
  nsresult rv;
  if (deleteNodeTransaction) {
    rv = DoTransactionInternal(deleteNodeTransaction);
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                         "EditorBase::DoTransactionInternal() failed");

    if (mTextServicesDocument && NS_SUCCEEDED(rv)) {
      RefPtr<TextServicesDocument> textServicesDocument = mTextServicesDocument;
      textServicesDocument->DidDeleteContent(aContent);
    }
  } else {
    rv = NS_ERROR_FAILURE;
  }

  if (!mActionListeners.IsEmpty()) {
    for (auto& listener : mActionListeners.Clone()) {
      DebugOnly<nsresult> rvIgnored = listener->DidDeleteNode(&aContent, rv);
      NS_WARNING_ASSERTION(
          NS_SUCCEEDED(rvIgnored),
          "nsIEditActionListener::DidDeleteNode() failed, but ignored");
    }
  }

  return NS_WARN_IF(Destroyed()) ? NS_ERROR_EDITOR_DESTROYED : rv;
}

NS_IMETHODIMP EditorBase::NotifySelectionChanged(Document* aDocument,
                                                 Selection* aSelection,
                                                 int16_t aReason,
                                                 int32_t /*aAmount*/) {
  if (NS_WARN_IF(!aDocument) || NS_WARN_IF(!aSelection)) {
    return NS_ERROR_INVALID_ARG;
  }

  if (IsDispatchingInputEvent()) {
    mEditActionData
        ->SetSelectionCreatedByDoubleclickOfEditActionDispatchingInputEvent(
            aSelection->GetFrameSelection() &&
            aSelection->GetFrameSelection()->IsDoubleClickSelection());
  }

  if (mTextInputListener) {
    RefPtr<TextInputListener> textInputListener = mTextInputListener;
    textInputListener->OnSelectionChange(*aSelection, aReason);
  }

  if (mIMEContentObserver) {
    RefPtr<IMEContentObserver> observer = mIMEContentObserver;
    observer->OnSelectionChange(*aSelection);
  }

  return NS_OK;
}

void EditorBase::NotifyEditorObservers(
    NotificationForEditorObservers aNotification) {
  MOZ_ASSERT(IsEditActionDataAvailable());

  switch (aNotification) {
    case eNotifyEditorObserversOfEnd:
      mIsInEditSubAction = false;

      if (mEditActionData) {
        mEditActionData->MarkAsHandled();
      }

      if (mTextInputListener) {
        // TODO: TextInputListener::OnEditActionHandled() may return
        //       NS_ERROR_OUT_OF_MEMORY.  If so and if
        //       TextControlState::SetValue() setting value with us, we should
        //       return the result to EditorBase::ReplaceTextAsAction(),
        //       EditorBase::DeleteSelectionAsAction() and
        //       TextEditor::InsertTextAsAction().  However, it requires a lot
        //       of changes in editor classes, but it's not so important since
        //       editor does not use fallible allocation.  Therefore, normally,
        //       the process must be crashed anyway.
        RefPtr<TextInputListener> listener = mTextInputListener;
        nsresult rv =
            listener->OnEditActionHandled(MOZ_KnownLive(*AsTextEditor()));
        MOZ_RELEASE_ASSERT(rv != NS_ERROR_OUT_OF_MEMORY,
                           "Setting value failed due to out of memory");
        NS_WARNING_ASSERTION(
            NS_SUCCEEDED(rv),
            "TextInputListener::OnEditActionHandled() failed, but ignored");
      }

      if (mIMEContentObserver) {
        RefPtr<IMEContentObserver> observer = mIMEContentObserver;
        observer->OnEditActionHandled();
      }

      if (!mDispatchInputEvent || IsEditActionAborted() ||
          IsEditActionCanceled()) {
        MOZ_LOG(
            gEventLog, LogLevel::Warning,
            ("%p %s: Not dispatching \"input\" event (mDispatchInputEvent=%s, "
             "IsEditActionAborted()=%s, IsEditActionCanceled()=%s",
             this, mIsHTMLEditorClass ? "HTMLEditor" : "TextEditor",
             mDispatchInputEvent ? "true" : "false",
             IsEditActionAborted() ? "true" : "false",
             IsEditActionCanceled() ? "true" : "false"));
        break;
      }

      DispatchInputEvent();
      break;
    case eNotifyEditorObserversOfBefore:
      if (NS_WARN_IF(mIsInEditSubAction)) {
        return;
      }

      mIsInEditSubAction = true;

      if (mIMEContentObserver) {
        RefPtr<IMEContentObserver> observer = mIMEContentObserver;
        observer->BeforeEditAction();
      }
      return;
    case eNotifyEditorObserversOfCancel:
      mIsInEditSubAction = false;

      if (mEditActionData) {
        mEditActionData->MarkAsHandled();
      }

      if (mIMEContentObserver) {
        RefPtr<IMEContentObserver> observer = mIMEContentObserver;
        observer->CancelEditAction();
      }
      break;
    default:
      MOZ_CRASH("Handle all notifications here");
      break;
  }

  if (IsHTMLEditor() && !Destroyed()) {
    // We may need to show resizing handles or update existing ones after
    // all transactions are done. This way of doing is preferred to DOM
    // mutation events listeners because all the changes the user can apply
    // to a document may result in multiple events, some of them quite hard
    // to listen too (in particular when an ancestor of the selection is
    // changed but the selection itself is not changed).
    DebugOnly<nsresult> rvIgnored =
        MOZ_KnownLive(AsHTMLEditor())->RefreshEditingUI();
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rvIgnored),
                         "HTMLEditor::RefreshEditingUI() failed, but ignored");
  }
}

void EditorBase::DispatchInputEvent() {
  MOZ_ASSERT(IsEditActionDataAvailable());
  MOZ_ASSERT(!IsEditActionCanceled(),
             "If preceding beforeinput event is canceled, we shouldn't "
             "dispatch input event");
  MOZ_ASSERT(
      !ShouldAlreadyHaveHandledBeforeInputEventDispatching(),
      "We've not handled beforeinput event but trying to dispatch input event");

  // We don't need to dispatch multiple input events if there is a pending
  // input event.  However, it may have different event target.  If we resolved
  // this issue, we need to manage the pending events in an array.  But it's
  // overwork.  We don't need to do it for the very rare case.
  // TODO: However, we start to set InputEvent.inputType.  So, each "input"
  //       event now notifies web app each change.  So, perhaps, we should
  //       not omit input events.

  RefPtr<Element> targetElement = GetInputEventTargetElement();
  if (NS_WARN_IF(!targetElement)) {
    MOZ_LOG(gEventLog, LogLevel::Error,
            ("%p %s: Failed dispatching \"input\" event due to no target", this,
             mIsHTMLEditorClass ? "HTMLEditor" : "TextEditor"));
    return;
  }
  RefPtr<DataTransfer> dataTransfer = GetInputEventDataTransfer();
  const EditAction editAction = GetEditAction();
  if (editAction == EditAction::eCancelComposition ||
      editAction == EditAction::eCommitComposition) {
    MOZ_ASSERT(!mComposition);
    if (MOZ_UNLIKELY(!CanDispatchInputEventAfterCompositionEnd())) {
      MOZ_LOG(gEventLog, LogLevel::Info,
              ("%p %s: Blocked to dispatch \"input\" event immediately after "
               "eCompositionEnd",
               this, mIsHTMLEditorClass ? "HTMLEditor" : "TextEditor"));
      return;
    }
  }
  const EditorInputType inputType = ToInputType(editAction);
  mEditActionData->WillDispatchInputEvent();
  MOZ_LOG(gEventLog, LogLevel::Info,
          ("%p %s: Dispatching \"input\" event: { inputType=\"%s\" }...", this,
           mIsHTMLEditorClass ? "HTMLEditor" : "TextEditor",
           ToString(inputType).c_str()));
  DebugOnly<nsresult> rvIgnored = nsContentUtils::DispatchInputEvent(
      targetElement, eEditorInput, inputType, this,
      dataTransfer ? InputEventOptions(dataTransfer,
                                       InputEventOptions::NeverCancelable::No)
                   : InputEventOptions(GetInputEventData(),
                                       InputEventOptions::NeverCancelable::No));
  MOZ_LOG(gEventLog, LogLevel::Debug,
          ("%p %s: Dispatched \"input\" event: { inputType=\"%s\" }", this,
           mIsHTMLEditorClass ? "HTMLEditor" : "TextEditor",
           ToString(inputType).c_str()));
  mEditActionData->DidDispatchInputEvent();
  NS_WARNING_ASSERTION(
      NS_SUCCEEDED(rvIgnored),
      "nsContentUtils::DispatchInputEvent() failed, but ignored");
}

NS_IMETHODIMP EditorBase::AddEditActionListener(
    nsIEditActionListener* aListener) {
  if (NS_WARN_IF(!aListener)) {
    return NS_ERROR_INVALID_ARG;
  }

  // If given edit action listener is text services document for the inline
  // spell checker, store it as reference of concrete class for performance
  // reason.
  if (mInlineSpellChecker) {
    EditorSpellCheck* editorSpellCheck =
        mInlineSpellChecker->GetEditorSpellCheck();
    if (editorSpellCheck) {
      mozSpellChecker* spellChecker = editorSpellCheck->GetSpellChecker();
      if (spellChecker) {
        TextServicesDocument* textServicesDocument =
            spellChecker->GetTextServicesDocument();
        if (static_cast<nsIEditActionListener*>(textServicesDocument) ==
            aListener) {
          mTextServicesDocument = textServicesDocument;
          return NS_OK;
        }
      }
    }
  }

  // Make sure the listener isn't already on the list
  if (!mActionListeners.Contains(aListener)) {
    mActionListeners.AppendElement(*aListener);
    NS_WARNING_ASSERTION(
        mActionListeners.Length() != 1,
        "nsIEditActionListener installed, this editor becomes slower");
  }

  return NS_OK;
}

NS_IMETHODIMP EditorBase::RemoveEditActionListener(
    nsIEditActionListener* aListener) {
  if (NS_WARN_IF(!aListener)) {
    return NS_ERROR_INVALID_ARG;
  }

  if (static_cast<nsIEditActionListener*>(mTextServicesDocument) == aListener) {
    mTextServicesDocument = nullptr;
    return NS_OK;
  }

  NS_WARNING_ASSERTION(mActionListeners.Length() != 1,
                       "All nsIEditActionListeners have been removed, this "
                       "editor becomes faster");
  mActionListeners.RemoveElement(aListener);

  return NS_OK;
}

NS_IMETHODIMP EditorBase::AddDocumentStateListener(
    nsIDocumentStateListener* aListener) {
  if (NS_WARN_IF(!aListener)) {
    return NS_ERROR_INVALID_ARG;
  }

  if (!mDocStateListeners.Contains(aListener)) {
    mDocStateListeners.AppendElement(*aListener);
  }

  return NS_OK;
}

NS_IMETHODIMP EditorBase::RemoveDocumentStateListener(
    nsIDocumentStateListener* aListener) {
  if (NS_WARN_IF(!aListener)) {
    return NS_ERROR_INVALID_ARG;
  }

  mDocStateListeners.RemoveElement(aListener);

  return NS_OK;
}

NS_IMETHODIMP EditorBase::ForceCompositionEnd() {
  nsresult rv = CommitComposition();
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "EditorBase::CommitComposition() failed");
  return rv;
}

nsresult EditorBase::CommitComposition() {
  nsPresContext* presContext = GetPresContext();
  if (NS_WARN_IF(!presContext)) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  if (!mComposition) {
    return NS_OK;
  }
  nsresult rv =
      IMEStateManager::NotifyIME(REQUEST_TO_COMMIT_COMPOSITION, presContext);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv), "IMEStateManager::NotifyIME() failed");
  return rv;
}

NS_IMETHODIMP EditorBase::GetComposing(bool* aResult) {
  if (NS_WARN_IF(!aResult)) {
    return NS_ERROR_INVALID_ARG;
  }
  *aResult = IsIMEComposing();
  return NS_OK;
}

NS_IMETHODIMP EditorBase::GetRootElement(Element** aRootElement) {
  if (NS_WARN_IF(!aRootElement)) {
    return NS_ERROR_INVALID_ARG;
  }
  *aRootElement = do_AddRef(mRootElement).take();
  return NS_WARN_IF(!*aRootElement) ? NS_ERROR_NOT_AVAILABLE : NS_OK;
}

void EditorBase::OnStartToHandleTopLevelEditSubAction(
    EditSubAction aTopLevelEditSubAction,
    nsIEditor::EDirection aDirectionOfTopLevelEditSubAction, ErrorResult& aRv) {
  MOZ_ASSERT(IsEditActionDataAvailable());
  MOZ_ASSERT(!aRv.Failed());
  mEditActionData->SetTopLevelEditSubAction(aTopLevelEditSubAction,
                                            aDirectionOfTopLevelEditSubAction);
}

nsresult EditorBase::OnEndHandlingTopLevelEditSubAction() {
  MOZ_ASSERT(IsEditActionDataAvailable());
  mEditActionData->SetTopLevelEditSubAction(EditSubAction::eNone, eNone);
  return NS_OK;
}

nsresult EditorBase::DoInsertText(Text& aText, uint32_t aOffset,
                                  const nsAString& aStringToInsert) {
  AutoTextEditVerifier verify(*this, aText);
  verify.WillInsertText(aOffset, aStringToInsert);
  const nsresult rvInsertData = [&]() MOZ_CAN_RUN_SCRIPT_BOUNDARY_LAMBDA {
    AutoCharacterDataAPIWrapper charDataWrapper(*this, aText);
    nsresult rv = charDataWrapper.InsertData(aOffset, aStringToInsert);
    if (NS_WARN_IF(Destroyed())) [[unlikely]] {
      return NS_ERROR_EDITOR_DESTROYED;
    }
    if (NS_FAILED(rv)) [[unlikely]] {
      NS_WARNING("AutoCharacterDataAPIWrapper::InsertData() failed");
      return rv;
    }
    NS_WARNING_ASSERTION(!verify.IsExpectedData() ||
                             charDataWrapper.IsExpectedResult(aStringToInsert),
                         "Inserting data caused other mutations, but ignored");
    return NS_OK;
  }();
  const nsresult rvDidInsertText = [&]() MOZ_CAN_RUN_SCRIPT_BOUNDARY_LAMBDA {
    if (IsTextEditor() && !aStringToInsert.IsEmpty()) {
      nsresult rv = MOZ_KnownLive(AsTextEditor())
                        ->DidInsertText(aText.TextLength(), aOffset,
                                        aStringToInsert.Length());
      if (NS_WARN_IF(Destroyed())) [[unlikely]] {
        return NS_ERROR_EDITOR_DESTROYED;
      }
      if (NS_FAILED(rv)) [[unlikely]] {
        NS_WARNING("TextEditor::DidInsertText() failed");
        return rv;
      }
    }
    return NS_OK;
  }();
  // NOTE: TextEditor::DidInsertText() might cause destroying the editor even
  // when rvInsertData is not NS_ERROR_EDITOR_DESTROYED. Let's check Destroyed()
  // first.
  if (Destroyed()) [[unlikely]] {
    return NS_ERROR_EDITOR_DESTROYED;
  }
  if (NS_FAILED(rvInsertData)) [[unlikely]] {
    return rvInsertData;
  }
  if (NS_FAILED(rvDidInsertText)) [[unlikely]] {
    return rvDidInsertText;
  }
  if (NS_WARN_IF(!verify.IsExpectedData())) [[unlikely]] {
    return NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE;
  }
  return NS_OK;
}

nsresult EditorBase::DoDeleteText(Text& aText, uint32_t aOffset,
                                  uint32_t aCount) {
  AutoTextEditVerifier verify(*this, aText);
  verify.WillDeleteText(aOffset, aCount);
  if (IsTextEditor() && aCount > 0) {
    AsTextEditor()->WillDeleteText(aText.TextLength(), aOffset, aCount);
  }
  const nsresult rvDeleteData = [&]() MOZ_CAN_RUN_SCRIPT_BOUNDARY_LAMBDA {
    AutoCharacterDataAPIWrapper charDataWrapper(*this, aText);
    nsresult rv = charDataWrapper.DeleteData(aOffset, aCount);
    if (NS_WARN_IF(Destroyed())) [[unlikely]] {
      return NS_ERROR_EDITOR_DESTROYED;
    }
    if (NS_FAILED(rv)) [[unlikely]] {
      NS_WARNING("AutoCharacterDataAPIWrapper::DeleteData() failed");
      return rv;
    }
    NS_WARNING_ASSERTION(!verify.IsExpectedData() ||
                             charDataWrapper.IsExpectedResult(EmptyString()),
                         "Deleting data caused other mutations, but ignored");
    return NS_OK;
  }();
  if (NS_FAILED(rvDeleteData)) [[unlikely]] {
    return rvDeleteData;
  }
  MOZ_ASSERT(!Destroyed());
  if (NS_WARN_IF(!verify.IsExpectedData())) [[unlikely]] {
    return NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE;
  }
  return NS_OK;
}

nsresult EditorBase::DoReplaceText(Text& aText, uint32_t aOffset,
                                   uint32_t aCount,
                                   const nsAString& aStringToInsert) {
  AutoTextEditVerifier verify(*this, aText);
  verify.WillReplaceText(aOffset, aCount, aStringToInsert);
  if (IsTextEditor() && aCount > 0) {
    AsTextEditor()->WillDeleteText(aText.TextLength(), aOffset, aCount);
  }
  const nsresult rvReplaceData = [&]() MOZ_CAN_RUN_SCRIPT_BOUNDARY_LAMBDA {
    AutoCharacterDataAPIWrapper charDataWrapper(*this, aText);
    nsresult rv = charDataWrapper.ReplaceData(aOffset, aCount, aStringToInsert);
    if (NS_WARN_IF(Destroyed())) [[unlikely]] {
      return NS_ERROR_EDITOR_DESTROYED;
    }
    if (NS_FAILED(rv)) [[unlikely]] {
      NS_WARNING("AutoCharacterDataAPIWrapper::ReplaceData() failed");
      return rv;
    }
    NS_WARNING_ASSERTION(!verify.IsExpectedData() ||
                             charDataWrapper.IsExpectedResult(aStringToInsert),
                         "Replacing data caused other mutations, but ignored");
    return NS_OK;
  }();
  const nsresult rvDidInsertText = [&]() MOZ_CAN_RUN_SCRIPT_BOUNDARY_LAMBDA {
    if (IsTextEditor() && !aStringToInsert.IsEmpty()) {
      nsresult rv = MOZ_KnownLive(AsTextEditor())
                        ->DidInsertText(aText.TextLength(), aOffset,
                                        aStringToInsert.Length());
      if (NS_WARN_IF(Destroyed())) [[unlikely]] {
        return NS_ERROR_EDITOR_DESTROYED;
      }
      if (NS_FAILED(rv)) [[unlikely]] {
        NS_WARNING("TextEditor::DidInsertText() failed");
        return rv;
      }
    }
    return NS_OK;
  }();
  // NOTE: TextEditor::DidInsertText() might cause destroying the editor even
  // when rvReplaceData is not NS_ERROR_EDITOR_DESTROYED. Let's check
  // Destroyed() first.
  if (Destroyed()) [[unlikely]] {
    return NS_ERROR_EDITOR_DESTROYED;
  }
  if (NS_FAILED(rvReplaceData)) [[unlikely]] {
    return rvReplaceData;
  }
  if (NS_FAILED(rvDidInsertText)) [[unlikely]] {
    return rvDidInsertText;
  }
  if (NS_WARN_IF(!verify.IsExpectedData())) [[unlikely]] {
    return NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE;
  }
  return NS_OK;
}

nsresult EditorBase::DoSetText(Text& aText, const nsAString& aStringToSet) {
  AutoTextEditVerifier verify(*this, aText);
  verify.WillSetText(aStringToSet);
  if (IsTextEditor()) {
    uint32_t length = aText.TextLength();
    if (length > 0) {
      AsTextEditor()->WillDeleteText(length, 0, length);
    }
  }
  const nsresult rvSetData = [&]() MOZ_CAN_RUN_SCRIPT_BOUNDARY_LAMBDA {
    AutoCharacterDataAPIWrapper charDataWrapper(*this, aText);
    nsresult rv = charDataWrapper.SetData(aStringToSet);
    if (NS_WARN_IF(Destroyed())) [[unlikely]] {
      return NS_ERROR_EDITOR_DESTROYED;
    }
    if (NS_FAILED(rv)) [[unlikely]] {
      NS_WARNING("AutoCharacterDataAPIWrapper::SetData() failed");
      return rv;
    }
    NS_WARNING_ASSERTION(!verify.IsExpectedData() ||
                             charDataWrapper.IsExpectedResult(aStringToSet),
                         "Setting data caused other mutations, but ignored");
    return NS_OK;
  }();
  const nsresult rvDidInsertText = [&]() MOZ_CAN_RUN_SCRIPT_BOUNDARY_LAMBDA {
    if (IsTextEditor() && !aStringToSet.IsEmpty()) {
      nsresult rv =
          MOZ_KnownLive(AsTextEditor())
              ->DidInsertText(aText.Length(), 0, aStringToSet.Length());
      if (NS_WARN_IF(Destroyed())) [[unlikely]] {
        return NS_ERROR_EDITOR_DESTROYED;
      }
      if (NS_FAILED(rv)) {
        NS_WARNING("TextEditor::DidInsertText() failed");
        return rv;
      }
    }
    return NS_OK;
  }();
  // NOTE: TextEditor::DidInsertText() might cause destroying the editor even
  // when rvSetData is not NS_ERROR_EDITOR_DESTROYED. Let's check Destroyed()
  // first.
  if (Destroyed()) [[unlikely]] {
    return NS_ERROR_EDITOR_DESTROYED;
  }
  if (NS_FAILED(rvSetData)) [[unlikely]] {
    return rvSetData;
  }
  if (NS_FAILED(rvDidInsertText)) [[unlikely]] {
    return rvDidInsertText;
  }
  if (NS_WARN_IF(!verify.IsExpectedData())) [[unlikely]] {
    return NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE;
  }
  return NS_OK;
}

nsresult EditorBase::CloneAttributeWithTransaction(nsAtom& aAttribute,
                                                   Element& aDestElement,
                                                   Element& aSourceElement) {
  nsAutoString attrValue;
  if (aSourceElement.GetAttr(&aAttribute, attrValue)) {
    nsresult rv =
        SetAttributeWithTransaction(aDestElement, aAttribute, attrValue);
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                         "EditorBase::SetAttributeWithTransaction() failed");
    return rv;
  }
  nsresult rv = RemoveAttributeWithTransaction(aDestElement, aAttribute);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "EditorBase::RemoveAttributeWithTransaction() failed");
  return rv;
}

NS_IMETHODIMP EditorBase::CloneAttributes(Element* aDestElement,
                                          Element* aSourceElement) {
  if (NS_WARN_IF(!aDestElement) || NS_WARN_IF(!aSourceElement)) {
    return NS_ERROR_INVALID_ARG;
  }

  AutoEditActionDataSetter editActionData(*this, EditAction::eSetAttribute);
  nsresult rv = editActionData.CanHandleAndMaybeDispatchBeforeInputEvent();
  if (NS_FAILED(rv)) {
    NS_WARNING_ASSERTION(rv == NS_ERROR_EDITOR_ACTION_CANCELED,
                         "CanHandleAndMaybeDispatchBeforeInputEvent() failed");
    return EditorBase::ToGenericNSResult(rv);
  }

  CloneAttributesWithTransaction(*aDestElement, *aSourceElement);

  return NS_OK;
}

void EditorBase::CloneAttributesWithTransaction(Element& aDestElement,
                                                Element& aSourceElement) {
  AutoPlaceholderBatch treatAsOneTransaction(
      *this, ScrollSelectionIntoView::Yes, __FUNCTION__);

  // Use transaction system for undo only if destination is already in the
  // document
  Element* rootElement = GetRoot();
  if (NS_WARN_IF(!rootElement)) {
    return;
  }

  const OwningNonNull<Element> destElement(aDestElement);
  const OwningNonNull<Element> sourceElement(aSourceElement);
  bool isDestElementInBody = rootElement->Contains(destElement);

  // Clear existing attributes
  AutoTArray<OwningNonNull<nsAtom>, 16> destElementAttributes;
  if (const uint32_t attrCount = destElement->GetAttrCount()) {
    destElementAttributes.SetCapacity(attrCount);
    for (const uint32_t i : IntegerRange(attrCount)) {
      if (const nsAttrName* attrName = destElement->GetUnsafeAttrNameAt(i)) {
        MOZ_ASSERT(attrName->LocalName());
        destElementAttributes.AppendElement(*attrName->LocalName());
      }
    }
  }
  for (const OwningNonNull<nsAtom>& attr : destElementAttributes) {
    if (isDestElementInBody) {
      DebugOnly<nsresult> rvIgnored =
          RemoveAttributeWithTransaction(destElement, MOZ_KnownLive(*attr));
      NS_WARNING_ASSERTION(
          NS_SUCCEEDED(rvIgnored),
          "EditorBase::RemoveAttributeWithTransaction() failed, but ignored");
    } else {
      AutoElementAttrAPIWrapper elementWrapper(*this, destElement);
      if (NS_FAILED(elementWrapper.UnsetAttr(MOZ_KnownLive(attr), true))) {
        NS_WARNING(
            "AutoElementAttrAPIWrapper::UnsetAttr() failed, but ignored");
      } else {
        NS_WARNING_ASSERTION(
            elementWrapper.IsExpectedResult(EmptyString()),
            "Removing attribute caused other mutations, but ignored");
      }
    }
  }

  // Set just the attributes that the source element has
  AutoTArray<std::pair<OwningNonNull<nsAtom>, nsString>, 16>
      sourceElementAttributes;
  if (const uint32_t attrCount = sourceElement->GetAttrCount()) {
    sourceElementAttributes.SetCapacity(attrCount);
    for (const uint32_t i : IntegerRange(attrCount)) {
      const BorrowedAttrInfo attrInfo = sourceElement->GetAttrInfoAt(i);
      if (const nsAttrName* attrName = attrInfo.mName) {
        MOZ_ASSERT(attrName->LocalName());
        MOZ_ASSERT(attrInfo.mValue);
        nsString value;
        attrInfo.mValue->ToString(value);
        sourceElementAttributes.AppendElement(std::make_pair(
            OwningNonNull<nsAtom>(*attrName->LocalName()), std::move(value)));
      }
    }
  }
  for (const auto& attr : sourceElementAttributes) {
    if (isDestElementInBody) {
      DebugOnly<nsresult> rvIgnored = SetAttributeOrEquivalent(
          destElement, MOZ_KnownLive(attr.first), attr.second, false);
      NS_WARNING_ASSERTION(
          NS_SUCCEEDED(rvIgnored),
          "EditorBase::SetAttributeOrEquivalent() failed, but ignored");
    } else {
      // The element is not inserted in the document yet, we don't want to put
      // a transaction on the UndoStack
      DebugOnly<nsresult> rvIgnored = SetAttributeOrEquivalent(
          destElement, MOZ_KnownLive(attr.first), attr.second, true);
      NS_WARNING_ASSERTION(
          NS_SUCCEEDED(rvIgnored),
          "EditorBase::SetAttributeOrEquivalent() failed, but ignored");
    }
  }
}

nsresult EditorBase::ScrollSelectionFocusIntoView() const {
  nsISelectionController* selectionController = GetSelectionController();
  if (!selectionController) {
    return NS_OK;
  }

  DebugOnly<nsresult> rvIgnored = selectionController->ScrollSelectionIntoView(
      SelectionType::eNormal, nsISelectionController::SELECTION_FOCUS_REGION,
      AxisScrollParams(), AxisScrollParams(),
      ScrollFlags::ScrollOverflowHidden);
  NS_WARNING_ASSERTION(
      NS_SUCCEEDED(rvIgnored),
      "nsISelectionController::ScrollSelectionIntoView() failed, but ignored");
  return NS_WARN_IF(Destroyed()) ? NS_ERROR_EDITOR_DESTROYED : NS_OK;
}

EditorDOMPoint EditorBase::ComputePointToInsertText(
    const EditorDOMPoint& aPoint, InsertTextTo aInsertTextTo) const {
  if (aInsertTextTo == InsertTextTo::SpecifiedPoint) {
    return aPoint;
  }

  if (IsTextEditor()) {
    // In some cases, the node may be the anonymous div element or a padding
    // <br> element for empty last line.  Let's try to look for better insertion
    // point in the nearest text node if there is.
    return AsTextEditor()->FindBetterInsertionPoint(aPoint);
  }
  auto pointToInsert =
      aPoint.GetPointInTextNodeIfPointingAroundTextNode<EditorDOMPoint>();
  // If the candidate point is in a Text node which has only a preformatted
  // linefeed, we should not insert text into the node because it may have
  // been inserted by us and that's compatible behavior with Chrome.
  if (pointToInsert.IsInTextNode() &&
      HTMLEditUtils::TextHasOnlyOnePreformattedLinefeed(
          *pointToInsert.ContainerAs<Text>())) {
    if (pointToInsert.IsStartOfContainer()) {
      if (Text* const previousText = Text::FromNodeOrNull(
              pointToInsert.ContainerAs<Text>()->GetPreviousSibling())) {
        pointToInsert = EditorDOMPoint::AtEndOf(*previousText);
      } else {
        pointToInsert = pointToInsert.ParentPoint();
      }
    } else {
      MOZ_ASSERT(pointToInsert.IsEndOfContainer());
      if (Text* const nextText = Text::FromNodeOrNull(
              pointToInsert.ContainerAs<Text>()->GetNextSibling())) {
        pointToInsert = EditorDOMPoint(nextText, 0u);
      } else {
        pointToInsert = pointToInsert.AfterContainer();
      }
    }
  }
  if (aInsertTextTo == InsertTextTo::AlwaysCreateNewTextNode) {
    NS_WARNING_ASSERTION(!pointToInsert.IsInTextNode() ||
                             pointToInsert.IsStartOfContainer() ||
                             pointToInsert.IsEndOfContainer(),
                         "aPointToInsert is \"AlwaysCreateNewTextNode\", but "
                         "specified point middle of a `Text`");
    if (!pointToInsert.IsInTextNode()) {
      return pointToInsert;
    }
    return pointToInsert.IsStartOfContainer()
               ? EditorDOMPoint(pointToInsert.ContainerAs<Text>())
               : (pointToInsert.IsEndOfContainer()
                      ? EditorDOMPoint::After(
                            *pointToInsert.ContainerAs<Text>())
                      : pointToInsert);
  }
  if (aInsertTextTo == InsertTextTo::ExistingTextNodeIfAvailableAndNotStart) {
    return !(pointToInsert.IsInTextNode() && pointToInsert.IsStartOfContainer())
               ? pointToInsert
               : EditorDOMPoint(pointToInsert.ContainerAs<Text>());
  }
  return pointToInsert;
}

Result<InsertTextResult, nsresult> EditorBase::InsertTextWithTransaction(
    const nsAString& aStringToInsert, const EditorDOMPoint& aPointToInsert,
    InsertTextTo aInsertTextTo, InsertTextFor aPurpose) {
  MOZ_ASSERT_IF(IsTextEditor(),
                aInsertTextTo == InsertTextTo::ExistingTextNodeIfAvailable);

  if (NS_WARN_IF(!aPointToInsert.IsSet())) {
    return Err(NS_ERROR_INVALID_ARG);
  }

  MOZ_ASSERT(aPointToInsert.IsSetAndValid());

  if (!ShouldHandleIMEComposition() && aStringToInsert.IsEmpty()) {
    return InsertTextResult();
  }

  EditorDOMPoint pointToInsert =
      ComputePointToInsertText(aPointToInsert, aInsertTextTo);
  if (ShouldHandleIMEComposition() && InsertingTextForComposition(aPurpose)) {
    if (!pointToInsert.IsInTextNode()) {
      // create a text node
      RefPtr<nsTextNode> newTextNode = CreateTextNode(u""_ns);
      if (NS_WARN_IF(!newTextNode)) {
        return Err(NS_ERROR_FAILURE);
      }
      // then we insert it into the dom tree
      Result<CreateTextResult, nsresult> insertTextNodeResult =
          InsertNodeWithTransaction<Text>(*newTextNode, pointToInsert);
      if (MOZ_UNLIKELY(insertTextNodeResult.isErr())) {
        NS_WARNING("EditorBase::InsertNodeWithTransaction() failed");
        return insertTextNodeResult.propagateErr();
      }
      insertTextNodeResult.unwrap().IgnoreCaretPointSuggestion();
      pointToInsert.Set(newTextNode, 0u);
    }
    Result<InsertTextResult, nsresult> insertTextResult =
        InsertTextIntoTextNodeWithTransaction(
            aStringToInsert, pointToInsert.AsInText(), aPurpose);
    NS_WARNING_ASSERTION(
        insertTextResult.isOk(),
        "EditorBase::InsertTextIntoTextNodeWithTransaction() failed");
    return insertTextResult;
  }

  if (pointToInsert.IsInTextNode()) {
    // we are inserting text into an existing text node.
    Result<InsertTextResult, nsresult> insertTextResult =
        InsertTextIntoTextNodeWithTransaction(
            aStringToInsert, pointToInsert.AsInText(), aPurpose);
    NS_WARNING_ASSERTION(
        insertTextResult.isOk(),
        "EditorBase::InsertTextIntoTextNodeWithTransaction() failed");
    return insertTextResult;
  }

  // we are inserting text into a non-text node.  first we have to create a
  // textnode (this also populates it with the text)
  RefPtr<nsTextNode> newTextNode = CreateTextNode(aStringToInsert);
  if (NS_WARN_IF(!newTextNode)) {
    return Err(NS_ERROR_FAILURE);
  }
  // then we insert it into the dom tree
  Result<CreateTextResult, nsresult> insertTextNodeResult =
      InsertNodeWithTransaction<Text>(*newTextNode, pointToInsert);
  if (MOZ_UNLIKELY(insertTextNodeResult.isErr())) {
    NS_WARNING("EditorBase::InsertNodeWithTransaction() failed");
    return Err(insertTextNodeResult.unwrapErr());
  }
  insertTextNodeResult.unwrap().IgnoreCaretPointSuggestion();
  if (NS_WARN_IF(!newTextNode->IsInComposedDoc())) {
    return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
  }
  return InsertTextResult(EditorDOMPoint::AtEndOf(*newTextNode),
                          EditorDOMPoint::AtEndOf(*newTextNode));
}

std::tuple<EditorDOMPointInText, EditorDOMPointInText>
EditorBase::ComputeInsertedRange(const EditorDOMPointInText& aInsertedPoint,
                                 const nsAString& aInsertedString) const {
  MOZ_ASSERT(aInsertedPoint.IsSet());

  EditorDOMPointInText endOfInsertion(
      aInsertedPoint.ContainerAs<Text>(),
      std::min<uint32_t>(aInsertedPoint.Offset() + aInsertedString.Length(),
                         aInsertedPoint.ContainerAs<Text>()->TextDataLength()));
  return {aInsertedPoint, endOfInsertion};
}

Result<InsertTextResult, nsresult>
EditorBase::InsertTextIntoTextNodeWithTransaction(
    const nsAString& aStringToInsert,
    const EditorDOMPointInText& aPointToInsert, InsertTextFor aPurpose) {
  MOZ_ASSERT(IsEditActionDataAvailable());
  MOZ_ASSERT(aPointToInsert.IsSetAndValid());

  RefPtr<EditTransactionBase> transaction;
  const bool isIMETransaction =
      ShouldHandleIMEComposition() && InsertingTextForComposition(aPurpose);
  if (isIMETransaction) {
    transaction =
        CompositionTransaction::Create(*this, aStringToInsert, aPointToInsert);
  } else {
    transaction =
        InsertTextTransaction::Create(*this, aStringToInsert, aPointToInsert);
  }

  // XXX We may not need these view batches anymore.  This is handled at a
  // higher level now I believe.
  BeginUpdateViewBatch(__FUNCTION__);
  const nsresult rvDoTransaction = DoTransactionInternal(transaction);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rvDoTransaction),
                       "EditorBase::DoTransactionInternal() failed");
  EndUpdateViewBatch(__FUNCTION__);

  // Don't check whether we've been destroyed here because we need to notify
  // listeners and observers below even if we've already destroyed.

  auto pointToInsert = [&]() -> EditorDOMPointInText {
    if (!isIMETransaction) {
      return aPointToInsert;
    }
    if (NS_WARN_IF(!mComposition->GetContainerTextNode())) {
      return aPointToInsert;
    }
    return EditorDOMPointInText(mComposition->GetContainerTextNode(),
                                mComposition->ClampedStartOffsetInTextNode());
  }();

  EditorDOMPoint endOfInsertedText(
      pointToInsert.ContainerAs<Text>(),
      pointToInsert.Offset() + aStringToInsert.Length());

  if (IsHTMLEditor()) {
    auto [begin, end] = ComputeInsertedRange(pointToInsert, aStringToInsert);
    if (begin.IsSet() && end.IsSet()) {
      TopLevelEditSubActionDataRef().DidInsertText(
          *this, begin.RefOrTo<EditorRawDOMPoint>(),
          end.RefOrTo<EditorRawDOMPoint>());
    }
    if (isIMETransaction) {
      // Let's mark the text node as "modified frequently" if it interact with
      // IME since non-ASCII character may be inserted into it in most cases.
      pointToInsert.ContainerAs<Text>()->MarkAsMaybeModifiedFrequently();
    }
    // XXX Should we update endOfInsertedText here?
  }

  // let listeners know what happened
  if (!mActionListeners.IsEmpty()) {
    for (auto& listener : mActionListeners.Clone()) {
      // TODO: might need adaptation because of mutation event listeners called
      // during `DoTransactionInternal`.
      DebugOnly<nsresult> rvIgnored =
          listener->DidInsertText(pointToInsert.ContainerAs<Text>(),
                                  static_cast<int32_t>(pointToInsert.Offset()),
                                  aStringToInsert, rvDoTransaction);
      NS_WARNING_ASSERTION(
          NS_SUCCEEDED(rvIgnored),
          "nsIEditActionListener::DidInsertText() failed, but ignored");
    }
  }

  // Added some cruft here for bug 43366.  Layout was crashing because we left
  // an empty text node lying around in the document.  So I delete empty text
  // nodes caused by IME.  I have to mark the IME transaction as "fixed", which
  // means that furure IME txns won't merge with it.  This is because we don't
  // want future IME txns trying to put their text into a node that is no
  // longer in the document.  This does not break undo/redo, because all these
  // txns are wrapped in a parent PlaceHolder txn, and placeholder txns are
  // already savvy to having multiple ime txns inside them.

  // Delete empty IME text node if there is one
  const nsresult rvDeleteNode = [&]() MOZ_CAN_RUN_SCRIPT_BOUNDARY_LAMBDA {
    if (!IsHTMLEditor() || !isIMETransaction || !mComposition) {
      return NS_OK;
    }
    RefPtr<Text> textNode = mComposition->GetContainerTextNode();
    if (!textNode || textNode->TextDataLength()) [[likely]] {
      return NS_OK;
    }
    endOfInsertedText.Set(textNode);
    AutoEditorDOMPointChildInvalidator lockIndex(endOfInsertedText);
    nsresult rv = DeleteNodeWithTransaction(*textNode);
    if (!textNode->IsInComposedDoc()) [[likely]] {
      mComposition->OnTextNodeRemoved();
    }
    static_cast<CompositionTransaction*>(transaction.get())->MarkFixed();
    if (NS_FAILED(rv)) [[unlikely]] {
      NS_WARNING("EditorBase::DeleteNodeTransaction() failed");
      return rv;
    }
    if (NS_WARN_IF(!endOfInsertedText.IsSetAndValidInComposedDoc())) {
      return NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE;
    }
    return NS_OK;
  }();

  if (Destroyed()) [[unlikely]] {
    MOZ_ASSERT(rvDoTransaction == NS_ERROR_EDITOR_DESTROYED ||
                   rvDeleteNode == NS_ERROR_EDITOR_DESTROYED,
               "Which method forgot to return NS_ERROR_EDITOR_DESTROYED?");
    return Err(NS_ERROR_EDITOR_DESTROYED);
  }
  if (NS_FAILED(rvDoTransaction)) [[unlikely]] {
    return Err(rvDoTransaction);
  }
  if (NS_FAILED(rvDeleteNode)) [[unlikely]] {
    return Err(rvDeleteNode);
  }

  InsertTextTransaction* const insertTextTransaction =
      transaction->GetAsInsertTextTransaction();
  return insertTextTransaction
             ? InsertTextResult(std::move(endOfInsertedText),
                                insertTextTransaction
                                    ->SuggestPointToPutCaret<EditorDOMPoint>())
             : InsertTextResult(std::move(endOfInsertedText));
}

nsresult EditorBase::NotifyDocumentListeners(
    TDocumentListenerNotification aNotificationType) {
  switch (aNotificationType) {
    case eDocumentCreated:
      if (IsTextEditor()) {
        return NS_OK;
      }
      if (RefPtr<ComposerCommandsUpdater> composerCommandsUpdate =
              AsHTMLEditor()->mComposerCommandsUpdater) {
        composerCommandsUpdate->OnHTMLEditorCreated();
      }
      return NS_OK;

    case eDocumentToBeDestroyed: {
      RefPtr<ComposerCommandsUpdater> composerCommandsUpdate =
          IsHTMLEditor() ? AsHTMLEditor()->mComposerCommandsUpdater : nullptr;
      if (!mDocStateListeners.Length() && !composerCommandsUpdate) {
        return NS_OK;
      }
      // Needs to store all listeners before notifying ComposerCommandsUpdate
      // since notifying it might change mDocStateListeners.
      const AutoDocumentStateListenerArray listeners(
          mDocStateListeners.Clone());
      if (composerCommandsUpdate) {
        composerCommandsUpdate->OnBeforeHTMLEditorDestroyed();
      }
      for (auto& listener : listeners) {
        // MOZ_KnownLive because 'listeners' is guaranteed to
        // keep it alive.
        //
        // This can go away once
        // https://bugzilla.mozilla.org/show_bug.cgi?id=1620312 is fixed.
        nsresult rv = MOZ_KnownLive(listener)->NotifyDocumentWillBeDestroyed();
        if (NS_FAILED(rv)) {
          NS_WARNING(
              "nsIDocumentStateListener::NotifyDocumentWillBeDestroyed() "
              "failed");
          return rv;
        }
      }
      return NS_OK;
    }
    case eDocumentStateChanged: {
      bool docIsDirty;
      nsresult rv = GetDocumentModified(&docIsDirty);
      if (NS_FAILED(rv)) {
        NS_WARNING("EditorBase::GetDocumentModified() failed");
        return rv;
      }

      if (static_cast<int8_t>(docIsDirty) == mDocDirtyState) {
        return NS_OK;
      }

      mDocDirtyState = docIsDirty;

      RefPtr<ComposerCommandsUpdater> composerCommandsUpdate =
          IsHTMLEditor() ? AsHTMLEditor()->mComposerCommandsUpdater : nullptr;
      if (!mDocStateListeners.Length() && !composerCommandsUpdate) {
        return NS_OK;
      }
      // Needs to store all listeners before notifying ComposerCommandsUpdate
      // since notifying it might change mDocStateListeners.
      const AutoDocumentStateListenerArray listeners(
          mDocStateListeners.Clone());
      if (composerCommandsUpdate) {
        composerCommandsUpdate->OnHTMLEditorDirtyStateChanged(mDocDirtyState);
      }
      for (auto& listener : listeners) {
        // MOZ_KnownLive because 'listeners' is guaranteed to
        // keep it alive.
        //
        // This can go away once
        // https://bugzilla.mozilla.org/show_bug.cgi?id=1620312 is fixed.
        nsresult rv =
            MOZ_KnownLive(listener)->NotifyDocumentStateChanged(mDocDirtyState);
        if (NS_FAILED(rv)) {
          NS_WARNING(
              "nsIDocumentStateListener::NotifyDocumentStateChanged() failed");
          return rv;
        }
      }
      return NS_OK;
    }
    default:
      MOZ_ASSERT_UNREACHABLE("Unknown notification");
      return NS_ERROR_FAILURE;
  }
}

nsresult EditorBase::SetTextNodeWithoutTransaction(const nsAString& aString,
                                                   Text& aTextNode) {
  MOZ_ASSERT(IsEditActionDataAvailable());
  MOZ_ASSERT(IsTextEditor());
  MOZ_ASSERT(!IsUndoRedoEnabled());

  const uint32_t length = aTextNode.Length();

  // Let listeners know what's up
  if (!mActionListeners.IsEmpty() && length) {
    for (auto& listener : mActionListeners.Clone()) {
      DebugOnly<nsresult> rvIgnored =
          listener->WillDeleteText(MOZ_KnownLive(&aTextNode), 0, length);
      if (NS_WARN_IF(Destroyed())) {
        NS_WARNING(
            "nsIEditActionListener::WillDeleteText() failed, but ignored");
        return NS_ERROR_EDITOR_DESTROYED;
      }
    }
  }

  // We don't support undo here, so we don't really need all of the transaction
  // machinery, therefore we can run our transaction directly, breaking all of
  // the rules!
  nsresult rv = DoSetText(aTextNode, aString);
  if (NS_FAILED(rv)) [[unlikely]] {
    NS_WARNING("EditorBase::DoSetText() failed");
    return rv;
  }

  rv = CollapseSelectionTo(EditorRawDOMPoint(&aTextNode, aString.Length()));
  if (rv == NS_ERROR_EDITOR_DESTROYED) [[unlikely]] {
    NS_WARNING("EditorBase::CollapseSelection() caused destroying the editor");
    return rv;
  }
  NS_ASSERTION(NS_SUCCEEDED(rv),
               "EditorBase::CollapseSelectionTo() failed, but ignored");

  RangeUpdaterRef().SelAdjReplaceText(aTextNode, 0, length, aString.Length());

  // Let listeners know what happened
  if (!mActionListeners.IsEmpty() && !aString.IsEmpty()) {
    for (auto& listener : mActionListeners.Clone()) {
      DebugOnly<nsresult> rvIgnored =
          listener->DidInsertText(&aTextNode, 0, aString, NS_OK);
      if (NS_WARN_IF(Destroyed())) {
        return NS_ERROR_EDITOR_DESTROYED;
      }
      NS_WARNING_ASSERTION(
          NS_SUCCEEDED(rvIgnored),
          "nsIEditActionListener::DidInsertText() failed, but ignored");
    }
  }

  return NS_OK;
}

Result<CaretPoint, nsresult> EditorBase::DeleteTextWithTransaction(
    Text& aTextNode, uint32_t aOffset, uint32_t aLength) {
  MOZ_ASSERT(IsEditActionDataAvailable());

  RefPtr<DeleteTextTransaction> transaction =
      DeleteTextTransaction::MaybeCreate(*this, aTextNode, aOffset, aLength);
  if (MOZ_UNLIKELY(!transaction)) {
    NS_WARNING("DeleteTextTransaction::MaybeCreate() failed");
    return Err(NS_ERROR_FAILURE);
  }

  IgnoredErrorResult ignoredError;
  AutoEditSubActionNotifier startToHandleEditSubAction(
      *this, EditSubAction::eDeleteText, nsIEditor::ePrevious, ignoredError);
  if (NS_WARN_IF(ignoredError.ErrorCodeIs(NS_ERROR_EDITOR_DESTROYED))) {
    return Err(ignoredError.StealNSResult());
  }
  NS_WARNING_ASSERTION(
      !ignoredError.Failed(),
      "TextEditor::OnStartToHandleTopLevelEditSubAction() failed, but ignored");

  // Let listeners know what's up
  if (!mActionListeners.IsEmpty()) {
    for (auto& listener : mActionListeners.Clone()) {
      DebugOnly<nsresult> rvIgnored =
          listener->WillDeleteText(&aTextNode, aOffset, aLength);
      NS_WARNING_ASSERTION(
          NS_SUCCEEDED(rvIgnored),
          "nsIEditActionListener::WillDeleteText() failed, but ignored");
    }
  }

  nsresult rv = DoTransactionInternal(transaction);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "EditorBase::DoTransactionInternal() failed");

  if (IsHTMLEditor()) {
    TopLevelEditSubActionDataRef().DidDeleteText(
        *this, EditorRawDOMPoint(&aTextNode, aOffset));
  }

  if (NS_WARN_IF(Destroyed())) {
    return Err(NS_ERROR_EDITOR_DESTROYED);
  }
  if (NS_FAILED(rv)) {
    return Err(rv);
  }

  return CaretPoint(transaction->SuggestPointToPutCaret());
}

bool EditorBase::IsRoot(const nsINode* inNode) const {
  if (NS_WARN_IF(!inNode)) {
    return false;
  }
  nsINode* rootNode = GetRoot();
  return inNode == rootNode;
}

bool EditorBase::IsDescendantOfRoot(const nsINode* inNode) const {
  if (NS_WARN_IF(!inNode)) {
    return false;
  }
  nsIContent* root = GetRoot();
  if (NS_WARN_IF(!root)) {
    return false;
  }

  return inNode->IsInclusiveDescendantOf(root);
}

NS_IMETHODIMP EditorBase::IncrementModificationCount(int32_t inNumMods) {
  uint32_t oldModCount = mModCount;

  mModCount += inNumMods;

  if ((!oldModCount && mModCount) || (oldModCount && !mModCount)) {
    DebugOnly<nsresult> rvIgnored =
        NotifyDocumentListeners(eDocumentStateChanged);
    NS_WARNING_ASSERTION(
        NS_SUCCEEDED(rvIgnored),
        "EditorBase::NotifyDocumentListeners() failed, but ignored");
  }
  return NS_OK;
}

NS_IMETHODIMP EditorBase::GetModificationCount(int32_t* aOutModCount) {
  if (NS_WARN_IF(!aOutModCount)) {
    return NS_ERROR_INVALID_ARG;
  }
  *aOutModCount = mModCount;
  return NS_OK;
}

NS_IMETHODIMP EditorBase::ResetModificationCount() {
  bool doNotify = (mModCount != 0);

  mModCount = 0;

  if (!doNotify) {
    return NS_OK;
  }

  DebugOnly<nsresult> rvIgnored =
      NotifyDocumentListeners(eDocumentStateChanged);
  NS_WARNING_ASSERTION(
      NS_SUCCEEDED(rvIgnored),
      "EditorBase::NotifyDocumentListeners() failed, but ignored");
  return NS_OK;
}

template <typename EditorDOMPointType>
EditorDOMPointType EditorBase::GetFirstSelectionStartPoint() const {
  MOZ_ASSERT(IsEditActionDataAvailable());
  if (MOZ_UNLIKELY(!SelectionRef().RangeCount())) {
    return EditorDOMPointType();
  }

  const dom::Range* range = SelectionRef().GetRangeAt(0);
  if (MOZ_UNLIKELY(NS_WARN_IF(!range) || NS_WARN_IF(!range->IsPositioned()))) {
    return EditorDOMPointType();
  }

  return EditorDOMPointType(range->StartRef());
}

template <typename EditorDOMPointType>
EditorDOMPointType EditorBase::GetFirstSelectionEndPoint() const {
  MOZ_ASSERT(IsEditActionDataAvailable());
  if (MOZ_UNLIKELY(!SelectionRef().RangeCount())) {
    return EditorDOMPointType();
  }

  const dom::Range* range = SelectionRef().GetRangeAt(0);
  if (MOZ_UNLIKELY(NS_WARN_IF(!range) || NS_WARN_IF(!range->IsPositioned()))) {
    return EditorDOMPointType();
  }

  return EditorDOMPointType(range->EndRef());
}

// static
nsresult EditorBase::GetEndChildNode(const Selection& aSelection,
                                     nsIContent** aEndNode) {
  MOZ_ASSERT(aEndNode);

  *aEndNode = nullptr;

  if (NS_WARN_IF(!aSelection.RangeCount())) {
    return NS_ERROR_FAILURE;
  }

  const dom::Range* range = aSelection.GetRangeAt(0);
  if (NS_WARN_IF(!range)) {
    return NS_ERROR_FAILURE;
  }

  if (NS_WARN_IF(!range->IsPositioned())) {
    return NS_ERROR_FAILURE;
  }

  NS_IF_ADDREF(*aEndNode = range->GetChildAtEndOffset());
  return NS_OK;
}

nsresult EditorBase::EnsurePaddingBRElementInMultilineEditor() {
  MOZ_ASSERT(IsEditActionDataAvailable());
  MOZ_ASSERT(IsTextEditor() || AsHTMLEditor()->IsPlaintextMailComposer());
  MOZ_ASSERT(!IsSingleLineEditor());

  Element* anonymousDivOrBodyElement = GetRoot();
  if (NS_WARN_IF(!anonymousDivOrBodyElement)) {
    return NS_ERROR_FAILURE;
  }

  // Assuming EditorBase::MaybeCreatePaddingBRElementForEmptyEditor() has been
  // called first.
  // XXX This assumption is wrong.  This method may be called alone.  Actually,
  //     we see this warning in mochitest log.  So, we should fix this bug
  //     later.
  if (NS_WARN_IF(!anonymousDivOrBodyElement->GetLastChild())) {
    return NS_ERROR_FAILURE;
  }

  RefPtr<HTMLBRElement> brElement =
      HTMLBRElement::FromNode(anonymousDivOrBodyElement->GetLastChild());
  if (!brElement) {
    // TODO: Remove AutoTransactionsConserveSelection here.  It's not necessary
    //       in normal cases.  However, it may be required for nested edit
    //       actions which may be caused by legacy mutation event listeners or
    //       chrome script.
    AutoTransactionsConserveSelection dontChangeMySelection(*this);
    EditorDOMPoint endOfAnonymousDiv(
        EditorDOMPoint::AtEndOf(*anonymousDivOrBodyElement));
    Result<CreateElementResult, nsresult> insertPaddingBRElementResult =
        InsertPaddingBRElementForEmptyLastLineWithTransaction(
            endOfAnonymousDiv);
    if (MOZ_UNLIKELY(insertPaddingBRElementResult.isErr())) {
      NS_WARNING(
          "EditorBase::InsertPaddingBRElementForEmptyLastLineWithTransaction() "
          "failed");
      return insertPaddingBRElementResult.unwrapErr();
    }
    insertPaddingBRElementResult.inspect().IgnoreCaretPointSuggestion();
    return NS_OK;
  }

  // Check to see if the trailing BR is a former padding <br> element for empty
  // editor - this will have stuck around if we previously morphed a trailing
  // node into a padding <br> element.
  if (!brElement->IsPaddingForEmptyEditor()) {
    return NS_OK;
  }

  // Morph it back to a padding <br> element for empty last line.
  nsresult rv =
      UpdateBRElementType(*brElement, BRElementType::PaddingForEmptyLastLine);
  if (NS_FAILED(rv)) {
    NS_WARNING("EditorBase::UpdateBRElementType() failed");
    return rv;
  }

  return NS_OK;
}

void EditorBase::BeginUpdateViewBatch(const char* aRequesterFuncName) {
  MOZ_ASSERT(IsEditActionDataAvailable());
  MOZ_ASSERT(mUpdateCount >= 0, "bad state");

  if (!mUpdateCount) {
    // Turn off selection updates and notifications.
    SelectionRef().StartBatchChanges(aRequesterFuncName);
  }

  mUpdateCount++;
}

void EditorBase::EndUpdateViewBatch(const char* aRequesterFuncName) {
  MOZ_ASSERT(IsEditActionDataAvailable());
  MOZ_ASSERT(mUpdateCount > 0, "bad state");

  if (NS_WARN_IF(mUpdateCount <= 0)) {
    mUpdateCount = 0;
    return;
  }

  if (--mUpdateCount) {
    return;
  }

  // Turn selection updating and notifications back on.
  SelectionRef().EndBatchChanges(aRequesterFuncName);
}

TextComposition* EditorBase::GetComposition() const { return mComposition; }

template <typename EditorDOMPointType>
EditorDOMPointType EditorBase::GetFirstIMESelectionStartPoint() const {
  return mComposition
             ? EditorDOMPointType(mComposition->FirstIMESelectionStartRef())
             : EditorDOMPointType();
}

template <typename EditorDOMPointType>
EditorDOMPointType EditorBase::GetLastIMESelectionEndPoint() const {
  return mComposition
             ? EditorDOMPointType(mComposition->LastIMESelectionEndRef())
             : EditorDOMPointType();
}

bool EditorBase::IsIMEComposing() const {
  return mComposition && mComposition->IsComposing();
}

bool EditorBase::ShouldHandleIMEComposition() const {
  // When the editor is being reframed, the old value may be restored with
  // InsertText().  In this time, the text should be inserted as not a part
  // of the composition.
  return mComposition && mDidPostCreate;
}

bool EditorBase::EnsureComposition(WidgetCompositionEvent& aCompositionEvent) {
  if (mComposition) {
    return true;
  }
  // The compositionstart event must cause creating new TextComposition
  // instance at being dispatched by IMEStateManager.
  mComposition = IMEStateManager::GetTextCompositionFor(&aCompositionEvent);
  if (!mComposition) {
    // However, TextComposition may be committed before the composition
    // event comes here.
    return false;
  }
  mComposition->StartHandlingComposition(this);
  return true;
}

nsresult EditorBase::OnCompositionStart(
    WidgetCompositionEvent& aCompositionStartEvent) {
  MOZ_LOG(gTextInputLog, LogLevel::Info,
          ("%p %s::OnCompositionStart(aCompositionStartEvent={ mData=\"%s\"}), "
           "mComposition=%p",
           this, mIsHTMLEditorClass ? "HTMLEditor" : "TextEditor",
           NS_ConvertUTF16toUTF8(aCompositionStartEvent.mData).get(),
           mComposition.get()));

  if (mComposition) {
    NS_WARNING("There was a composition at receiving compositionstart event");
    return NS_OK;
  }

  // "beforeinput" event shouldn't be fired before "compositionstart".
  AutoEditActionDataSetter editActionData(*this, EditAction::eStartComposition);
  if (NS_WARN_IF(!editActionData.CanHandle())) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  EnsureComposition(aCompositionStartEvent);
  NS_WARNING_ASSERTION(mComposition, "Failed to get TextComposition instance?");
  return NS_OK;
}

nsresult EditorBase::OnCompositionChange(
    WidgetCompositionEvent& aCompositionChangeEvent) {
  MOZ_ASSERT(aCompositionChangeEvent.mMessage == eCompositionChange,
             "The event should be eCompositionChange");

  MOZ_LOG(
      gTextInputLog, LogLevel::Info,
      ("%p %s::OnCompositionChange(aCompositionChangeEvent={ mData=\"%s\", "
       "IsFollowedByCompositionEnd()=%s }), mComposition=%p",
       this, mIsHTMLEditorClass ? "HTMLEditor" : "TextEditor",
       NS_ConvertUTF16toUTF8(aCompositionChangeEvent.mData).get(),
       aCompositionChangeEvent.IsFollowedByCompositionEnd() ? "true" : "false",
       mComposition.get()));

  if (!mComposition) {
    NS_WARNING(
        "There is no composition, but receiving compositionchange event");
    return NS_ERROR_FAILURE;
  }

  AutoEditActionDataSetter editActionData(
      *this,
      // We need to distinguish whether the composition change is followed by
      // compositionend or not (i.e., wether IME has already ended the
      // composition or still has the composition) because we need to dispatch
      // `textInput` event only for the last composition change.
      aCompositionChangeEvent.IsFollowedByCompositionEnd()
          ? EditAction::eUpdateCompositionToCommit
          : EditAction::eUpdateComposition);
  if (NS_WARN_IF(!editActionData.CanHandle())) {
    return NS_ERROR_NOT_INITIALIZED;
  }
  MOZ_ASSERT(!aCompositionChangeEvent.mData.IsVoid());
  editActionData.SetData(aCompositionChangeEvent.mData);

  // If we're an `HTMLEditor` and this is second or later composition change,
  // we should set target range to the range of composition string.
  // Otherwise, set target ranges to selection ranges (will be done by
  // editActionData itself before dispatching `beforeinput` event).
  if (IsHTMLEditor() && mComposition->GetContainerTextNode()) {
    RefPtr<StaticRange> targetRange = StaticRange::Create(
        mComposition->GetContainerTextNode(),
        mComposition->ClampedStartOffsetInTextNode(),
        mComposition->GetContainerTextNode(),
        mComposition->ClampedEndOffsetInTextNode(), IgnoreErrors());
    NS_WARNING_ASSERTION(targetRange && targetRange->IsPositioned(),
                         "StaticRange::Create() failed");
    if (targetRange && targetRange->IsPositioned()) {
      editActionData.AppendTargetRange(*targetRange);
    }
  }

  // TODO: We need to use different EditAction value for beforeinput event
  //       if the event is followed by "compositionend" because corresponding
  //       "input" event will be fired from OnCompositionEnd() later with
  //       different EditAction value.
  // TODO: If Input Events Level 2 is enabled, "beforeinput" event may be
  //       actually canceled if edit action is eDeleteByComposition. In such
  //       case, we might need to keep selected text, but insert composition
  //       string before or after the selection.  However, the spec is still
  //       unstable.  We should keep handling the composition since other
  //       parts including widget may not be ready for such complicated
  //       behavior.
  nsresult rv = editActionData.MaybeDispatchBeforeInputEvent();
  if (rv != NS_ERROR_EDITOR_ACTION_CANCELED && NS_FAILED(rv)) {
    NS_WARNING("MaybeDispatchBeforeInputEvent() failed");
    return EditorBase::ToGenericNSResult(rv);
  }

  if (!EnsureComposition(aCompositionChangeEvent)) {
    NS_WARNING("EditorBase::EnsureComposition() failed");
    return NS_OK;
  }

  if (NS_WARN_IF(!GetPresShell())) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  // NOTE: TextComposition should receive selection change notification before
  //       CompositionChangeEventHandlingMarker notifies TextComposition of the
  //       end of handling compositionchange event because TextComposition may
  //       need to ignore selection changes caused by composition.  Therefore,
  //       CompositionChangeEventHandlingMarker must be destroyed after a call
  //       of NotifiyEditorObservers(eNotifyEditorObserversOfEnd) or
  //       NotifiyEditorObservers(eNotifyEditorObserversOfCancel) which notifies
  //       TextComposition of a selection change.
  MOZ_ASSERT(
      !mPlaceholderBatch,
      "UpdateIMEComposition() must be called without place holder batch");
  nsString data(aCompositionChangeEvent.mData);
  if (IsHTMLEditor()) {
    nsContentUtils::PlatformToDOMLineBreaks(data);
  }

  {
    // This needs to be destroyed before dispatching "input" event from
    // the following call of `NotifyEditorObservers`.  Therefore, we need to
    // put this in this block rather than outside of this.
    const bool wasComposing = mComposition->IsComposing();
    TextComposition::CompositionChangeEventHandlingMarker
        compositionChangeEventHandlingMarker(mComposition,
                                             &aCompositionChangeEvent);
    Maybe<AutoPlaceholderBatch> treatAsOneTransaction;
    // We don't need the placeholder transaction for EditContext, and it
    // causes issues to create one here since InsertTextAsSubAction
    // can blur the editor (in the textupdate handler) which
    // recursively calls OnCompositionChange.
    if (!GetEditActionEditContext()) {
      treatAsOneTransaction.emplace(*this, *nsGkAtoms::IMETxnName,
                                    ScrollSelectionIntoView::Yes, __FUNCTION__);
      MOZ_ASSERT(mIsInEditSubAction,
                 "AutoPlaceholderBatch should've notified the observers of "
                 "before-edit");
    }
    // XXX Why don't we get caret after the DOM mutation?
    RefPtr<nsCaret> caret = GetCaretForSelection();

    // If we're updating composition, we need to ignore normal selection
    // which may be updated by the web content.
    const auto purpose = [&]() -> InsertTextFor {
      if (!wasComposing) {
        return !aCompositionChangeEvent.IsFollowedByCompositionEnd()
                   ? InsertTextFor::CompositionStart
                   : InsertTextFor::CompositionStartAndEnd;
      }
      return !aCompositionChangeEvent.IsFollowedByCompositionEnd()
                 ? InsertTextFor::CompositionUpdate
                 : InsertTextFor::CompositionEnd;
    }();
    rv = InsertTextAsSubAction(data, purpose);
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                         "EditorBase::InsertTextAsSubAction() failed");

    if (caret) {
      caret->SetSelection(&SelectionRef());
    }
  }

  if (RefPtr editContext = editActionData.GetEditContext()) {
    RefPtr<TextRangeArray> ranges;
    uint32_t offset = 0;
    if (mComposition) {
      ranges = mComposition->GetRanges();
      offset = mComposition->ClampedStartOffsetInTextNode();
    }
    editContext->FireTextFormatUpdate(ranges, offset);
    if (NS_WARN_IF(Destroyed())) {
      return Err(NS_ERROR_EDITOR_DESTROYED);
    }
    if (editActionData.EditContextHasBeenChanged()) {
      // textformatupdate handler deactivated this EditContext
      return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
    }
  }

  // If still composing, we should fire input event via observer.
  // Note that if the composition will be committed by the following
  // compositionend event, we don't need to notify editor observes of this
  // change even if it's preferred by the pref.
  // NOTE: We must notify after the auto batch will be gone.
  if (!aCompositionChangeEvent.IsFollowedByCompositionEnd() && mComposition) {
    // If we're a TextEditor, we'll be initialized with a new anonymous subtree,
    // which can be caused by reframing from a "input" event listener.  At that
    // time, we'll move composition from current text node to the new text node
    // with using mComposition's data.  Therefore, it's important that
    // mComposition already has the latest information here.
    MOZ_ASSERT(mComposition->String() == data);
    NotifyEditorObservers(eNotifyEditorObserversOfEnd);
  }
  // NOTE: When the pref is enabled, the last `input` event which will be fired
  // after `compositionend` won't be paired with corresponding `beforeinput`
  // event.
  else if (MOZ_LIKELY(
               StaticPrefs::dom_input_events_dispatch_before_compositionend() &&
               mDispatchInputEvent && !IsEditActionAborted() &&
               CanDispatchInputEventBeforeCompositionEnd())) {
    DispatchInputEvent();
  }

  return EditorBase::ToGenericNSResult(rv);
}

void EditorBase::OnCompositionEnd(
    WidgetCompositionEvent& aCompositionEndEvent) {
  // In the usual case, this is called with an eCompositionEnd event,
  // however, TextComposition may also call it with the eCompositionCommit
  // directly (e.g. when the editor is removed during a composition).
  MOZ_ASSERT(aCompositionEndEvent.CausesDOMCompositionEndEvent());
  MOZ_LOG(gTextInputLog, LogLevel::Info,
          ("%p %s::OnCompositionEnd(aCompositionEndEvent={ mData=\"%s\"}), "
           "mComposition=%p",
           this, mIsHTMLEditorClass ? "HTMLEditor" : "TextEditor",
           NS_ConvertUTF16toUTF8(aCompositionEndEvent.mData).get(),
           mComposition.get()));

  if (!mComposition) {
    NS_WARNING("There is no composition, but receiving compositionend event");
    return;
  }

  const EditAction editAction = aCompositionEndEvent.mData.IsEmpty()
                                    ? EditAction::eCancelComposition
                                    : EditAction::eCommitComposition;
  AutoEditActionDataSetter editActionData(*this, editAction);
  // If Input Events Level 2 is enabled, EditAction::eCancelComposition is
  // mapped to EditorInputType::eDeleteCompositionText and it requires null
  // for InputEvent.data.  Therefore, only otherwise, we should set data.
  if (ToInputType(editAction) != EditorInputType::eDeleteCompositionText) {
    MOZ_ASSERT(
        ToInputType(editAction) == EditorInputType::eInsertCompositionText ||
        ToInputType(editAction) == EditorInputType::eInsertFromComposition);
    MOZ_ASSERT(!aCompositionEndEvent.mData.IsVoid());
    editActionData.SetData(aCompositionEndEvent.mData);
  }

  const RefPtr<PlaceholderTransaction> placeholderTransaction =
      [&]() -> PlaceholderTransaction* {
    if (!mTransactionManager) {
      return nullptr;
    }
    const nsCOMPtr<nsITransaction> transaction =
        mTransactionManager->PeekUndoStack();
    if (MOZ_UNLIKELY(!transaction)) {
      return nullptr;
    }
    const RefPtr<EditTransactionBase> transactionBase =
        transaction->GetAsEditTransactionBase();
    if (MOZ_UNLIKELY(!transactionBase)) {
      return nullptr;
    }
    return transactionBase->GetAsPlaceholderTransaction();
  }();
  // commit the IME transaction..we can get at it via the transaction mgr.
  // Note that this means IME won't work without an undo stack!
  if (placeholderTransaction) {
    placeholderTransaction->Commit();
  }

  // If the composition is canceled and the composition hasn't remove any
  // content, we should remove the transaction from the undo stack because
  // user "canceled" it, so, undoing the canceled composition is odd.  That
  // would appear as a noop undo transaction.
  if (editAction == EditAction::eCancelComposition && placeholderTransaction) {
    const nsTArray<OwningNonNull<EditTransactionBase>>& childTransactions =
        placeholderTransaction->ChildTransactions();
    // If the first transaction is inserting composition string, we didn't
    // replace selection with the composition string.  Then, all of the
    // operations during the composition is canceled by the user.  So, we
    // should not record it as an undo transaction.
    // FYI: If non-related action of the composition, we already closed the
    // previous transaction.
    if (!childTransactions.IsEmpty() &&
        childTransactions[0]->GetAsCompositionTransaction()) {
      nsCOMPtr<nsITransaction> transaction =
          mTransactionManager->PopUndoStack();
      MOZ_DIAGNOSTIC_ASSERT(transaction == placeholderTransaction);
    }
  }

  // Note that this just marks as that we've already handled "beforeinput" for
  // preventing assertions in FireInputEvent().  Note that corresponding
  // "beforeinput" event for the following "input" event should've already
  // been dispatched from `OnCompositionChange()`.
  DebugOnly<nsresult> rvIgnored =
      editActionData.MaybeDispatchBeforeInputEvent();
  MOZ_ASSERT(rvIgnored != NS_ERROR_EDITOR_ACTION_CANCELED,
             "Why beforeinput event was canceled in this case?");
  MOZ_ASSERT(NS_SUCCEEDED(rvIgnored),
             "MaybeDispatchBeforeInputEvent() should just mark the instance as "
             "handled it");

  // Composition string may have hidden the caret.  Therefore, we need to
  // cancel it here.
  HideCaret(false);

  // FYI: mComposition still keeps storing container text node of committed
  //      string, its offset and length.  However, they will be invalidated
  //      soon since its Destroy() will be called by IMEStateManager.
  mComposition->EndHandlingComposition(this);
  mComposition = nullptr;

  // notify editor observers of action
  // FYI: With current draft, "input" event should be fired from
  //      OnCompositionChange(), however, it requires a lot of our UI code
  //      change and does not make sense.  See spec issue:
  //      https://github.com/w3c/uievents/issues/202
  NotifyEditorObservers(eNotifyEditorObserversOfEnd);
}

bool EditorBase::CanDispatchInputEventBeforeCompositionEnd() const {
  Document* const doc = GetDocument();
  if (NS_WARN_IF(!doc)) {
    return false;
  }
  nsIPrincipal* const principal = doc->GetPrincipalForPrefBasedHacks();
  if (!principal) {
    return true;
  }
  constexpr static auto* kTextEditorPref =
      "editor.texteditor.inputevent.hack.no_dispatch_before_compositionend";
  constexpr static auto* kTextEditorAddlPref =
      "editor.texteditor.inputevent.hack.no_dispatch_before_compositionend."
      "addl";
  constexpr static auto* kHTMLEditorPref =
      "editor.htmleditor.inputevent.hack.no_dispatch_before_compositionend";
  constexpr static auto* kHTMLEditorAddlPref =
      "editor.htmleditor.inputevent.hack.no_dispatch_before_compositionend."
      "addl";
  return !principal->IsURIInPrefList(IsTextEditor() ? kTextEditorPref
                                                    : kHTMLEditorPref) &&
         !principal->IsURIInPrefList(IsTextEditor() ? kTextEditorAddlPref
                                                    : kHTMLEditorAddlPref);
}

bool EditorBase::CanDispatchInputEventAfterCompositionEnd() const {
  Document* const doc = GetDocument();
  if (NS_WARN_IF(!doc)) {
    return false;
  }
  nsIPrincipal* const principal = doc->GetPrincipalForPrefBasedHacks();
  if (!principal) {
    return true;
  }
  constexpr static auto* kTextEditorPref =
      "editor.texteditor.inputevent.hack.no_dispatch_after_compositionend";
  constexpr static auto* kTextEditorAddlPref =
      "editor.texteditor.inputevent.hack.no_dispatch_after_compositionend."
      "addl";
  constexpr static auto* kHTMLEditorPref =
      "editor.htmleditor.inputevent.hack.no_dispatch_after_compositionend";
  constexpr static auto* kHTMLEditorAddlPref =
      "editor.htmleditor.inputevent.hack.no_dispatch_after_compositionend."
      "addl";
  return !principal->IsURIInPrefList(IsTextEditor() ? kTextEditorPref
                                                    : kHTMLEditorPref) &&
         !principal->IsURIInPrefList(IsTextEditor() ? kTextEditorAddlPref
                                                    : kHTMLEditorAddlPref);
}

bool EditorBase::WillHandleMouseButtonEvent(WidgetMouseEvent& aMouseEvent) {
  MOZ_ASSERT(aMouseEvent.mMessage == eMouseDown ||
             aMouseEvent.mMessage == eMouseUp);
  if (!mEventListener) {
    return false;
  }
  OwningNonNull<EditorEventListener> editorEventListener(*mEventListener);
  return editorEventListener->WillHandleMouseButtonEvent(aMouseEvent);
}

void EditorBase::DoAfterDoTransaction(nsITransaction* aTransaction) {
  bool isTransientTransaction;
  MOZ_ALWAYS_SUCCEEDS(aTransaction->GetIsTransient(&isTransientTransaction));

  if (!isTransientTransaction) {
    // we need to deal here with the case where the user saved after some
    // edits, then undid one or more times. Then, the undo count is -ve,
    // but we can't let a do take it back to zero. So we flip it up to
    // a +ve number.
    int32_t modCount;
    DebugOnly<nsresult> rvIgnored = GetModificationCount(&modCount);
    NS_WARNING_ASSERTION(
        NS_SUCCEEDED(rvIgnored),
        "EditorBase::GetModificationCount() failed, but ignored");
    if (modCount < 0) {
      modCount = -modCount;
    }

    // don't count transient transactions
    MOZ_ALWAYS_SUCCEEDS(IncrementModificationCount(1));
  }
}

void EditorBase::DoAfterUndoTransaction() {
  // all undoable transactions are non-transient
  MOZ_ALWAYS_SUCCEEDS(IncrementModificationCount(-1));
}

void EditorBase::DoAfterRedoTransaction() {
  // all redoable transactions are non-transient
  MOZ_ALWAYS_SUCCEEDS(IncrementModificationCount(1));
}

already_AddRefed<DeleteMultipleRangesTransaction>
EditorBase::CreateTransactionForDeleteSelection(
    HowToHandleCollapsedRange aHowToHandleCollapsedRange,
    const AutoClonedRangeArray& aRangesToDelete) {
  MOZ_ASSERT(IsEditActionDataAvailable());
  MOZ_ASSERT(!aRangesToDelete.Ranges().IsEmpty());

  // Check whether the selection is collapsed and we should do nothing:
  if (NS_WARN_IF(aRangesToDelete.IsCollapsed() &&
                 aHowToHandleCollapsedRange ==
                     HowToHandleCollapsedRange::Ignore)) {
    return nullptr;
  }

  // allocate the out-param transaction
  RefPtr<DeleteMultipleRangesTransaction> transaction =
      DeleteMultipleRangesTransaction::Create();
  for (const OwningNonNull<dom::Range>& range : aRangesToDelete.Ranges()) {
    // Same with range as with selection; if it is collapsed and action
    // is eNone, do nothing.
    if (!range->Collapsed()) {
      RefPtr<DeleteRangeTransaction> deleteRangeTransaction =
          DeleteRangeTransaction::Create(*this, range);
      // XXX Oh, not checking if deleteRangeTransaction can modify the range...
      transaction->AppendChild(*deleteRangeTransaction);
      continue;
    }

    if (aHowToHandleCollapsedRange == HowToHandleCollapsedRange::Ignore) {
      continue;
    }

    // Let's extend the collapsed range to delete content around it.
    RefPtr<DeleteContentTransactionBase> deleteNodeOrTextTransaction =
        CreateTransactionForCollapsedRange(range, aHowToHandleCollapsedRange);
    // XXX When there are two or more ranges and at least one of them is
    //     not editable, deleteNodeOrTextTransaction may be nullptr.
    //     In such case, should we stop removing other ranges too?
    if (!deleteNodeOrTextTransaction) {
      NS_WARNING("EditorBase::CreateTransactionForCollapsedRange() failed");
      return nullptr;
    }
    transaction->AppendChild(*deleteNodeOrTextTransaction);
  }

  return transaction.forget();
}

// XXX: currently, this doesn't handle edge conditions because GetNext/GetPrior
// are not implemented
already_AddRefed<DeleteContentTransactionBase>
EditorBase::CreateTransactionForCollapsedRange(
    const dom::Range& aCollapsedRange,
    HowToHandleCollapsedRange aHowToHandleCollapsedRange) {
  MOZ_ASSERT(aCollapsedRange.Collapsed());
  MOZ_ASSERT(
      aHowToHandleCollapsedRange == HowToHandleCollapsedRange::ExtendBackward ||
      aHowToHandleCollapsedRange == HowToHandleCollapsedRange::ExtendForward);

  EditorRawDOMPoint point(aCollapsedRange.StartRef());
  if (NS_WARN_IF(!point.IsSet())) {
    return nullptr;
  }
  if (IsTextEditor()) {
    // There should be only one text node in the anonymous `<div>` (but may
    // be followed by a padding `<br>`).  We should adjust the point into
    // the text node (or return nullptr if there is no text to delete) for
    // avoiding finding the text node with complicated API.
    if (!point.IsInTextNode()) {
      const Element* anonymousDiv = GetRoot();
      if (NS_WARN_IF(!anonymousDiv)) {
        return nullptr;
      }
      if (!anonymousDiv->GetFirstChild() ||
          !anonymousDiv->GetFirstChild()->IsText()) {
        return nullptr;  // The value is empty.
      }
      if (point.GetContainer() == anonymousDiv) {
        if (point.IsStartOfContainer()) {
          point.Set(anonymousDiv->GetFirstChild(), 0);
        } else {
          point.SetToEndOf(anonymousDiv->GetFirstChild());
        }
      } else {
        // Must be referring a padding `<br>` element or after the text node.
        point.SetToEndOf(anonymousDiv->GetFirstChild());
      }
    }
    MOZ_ASSERT(!point.ContainerAs<Text>()->GetPreviousSibling());
    MOZ_ASSERT(!point.ContainerAs<Text>()->GetNextSibling() ||
               !point.ContainerAs<Text>()->GetNextSibling()->IsText());
    if (aHowToHandleCollapsedRange ==
            HowToHandleCollapsedRange::ExtendBackward &&
        point.IsStartOfContainer()) {
      return nullptr;
    }
    if (aHowToHandleCollapsedRange ==
            HowToHandleCollapsedRange::ExtendForward &&
        point.IsEndOfContainer()) {
      return nullptr;
    }
  }

  // XXX: if the container of point is empty, then we'll need to delete the node
  //      as well as the 1 child

  // build a transaction for deleting the appropriate data
  // XXX: this has to come from rule section
  const Element* const anonymousDivOrEditingHost =
      IsTextEditor() ? GetRoot() : AsHTMLEditor()->ComputeEditingHost();
  if (aHowToHandleCollapsedRange == HowToHandleCollapsedRange::ExtendBackward &&
      point.IsStartOfContainer()) {
    MOZ_ASSERT(IsHTMLEditor());
    if (MOZ_UNLIKELY(!point.IsInContentNode())) {
      NS_WARNING("There was no editable content before the collapsed range");
      return nullptr;
    }
    // We're backspacing from the beginning of a node.  Delete the last thing
    // of previous editable content.
    nsIContent* previousEditableContent = HTMLEditUtils::GetPreviousLeafContent(
        *point.ContainerAs<nsIContent>(),
        {LeafNodeOption::IgnoreNonEditableNode},
        IsTextEditor() ? BlockInlineCheck::UseHTMLDefaultStyle
                       : BlockInlineCheck::UseComputedDisplayOutsideStyle,
        anonymousDivOrEditingHost);
    if (MOZ_UNLIKELY(!previousEditableContent)) {
      NS_WARNING("There was no editable content before the collapsed range");
      return nullptr;
    }

    // There is an editable content, so delete its last child (if a text node,
    // delete the last char).  If it has no children, delete it.
    if (previousEditableContent->IsText()) {
      uint32_t length = previousEditableContent->Length();
      // Bail out for empty text node.
      // XXX Do we want to do something else?
      // XXX If other browsers delete empty text node, we should follow it.
      if (NS_WARN_IF(!length)) {
        NS_WARNING("Previous editable content was an empty text node");
        return nullptr;
      }
      RefPtr<DeleteTextTransaction> deleteTextTransaction =
          DeleteTextTransaction::MaybeCreateForPreviousCharacter(
              *this, *previousEditableContent->AsText(), length);
      if (!deleteTextTransaction) {
        NS_WARNING(
            "DeleteTextTransaction::MaybeCreateForPreviousCharacter() failed");
        return nullptr;
      }
      return deleteTextTransaction.forget();
    }

    if (IsHTMLEditor() &&
        NS_WARN_IF(!HTMLEditUtils::IsRemovableNode(*previousEditableContent))) {
      return nullptr;
    }
    RefPtr<DeleteNodeTransaction> deleteNodeTransaction =
        DeleteNodeTransaction::MaybeCreate(*this, *previousEditableContent);
    if (!deleteNodeTransaction) {
      NS_WARNING("DeleteNodeTransaction::MaybeCreate() failed");
      return nullptr;
    }
    return deleteNodeTransaction.forget();
  }

  if (aHowToHandleCollapsedRange == HowToHandleCollapsedRange::ExtendForward &&
      point.IsEndOfContainer()) {
    MOZ_ASSERT(IsHTMLEditor());
    if (MOZ_UNLIKELY(!point.IsInContentNode())) {
      NS_WARNING("There was no editable content after the collapsed range");
      return nullptr;
    }
    // We're deleting from the end of a node.  Delete the first thing of
    // next editable content.
    nsIContent* nextEditableContent = HTMLEditUtils::GetNextLeafContent(
        *point.ContainerAs<nsIContent>(),
        {LeafNodeOption::IgnoreNonEditableNode},
        IsTextEditor() ? BlockInlineCheck::UseHTMLDefaultStyle
                       : BlockInlineCheck::UseComputedDisplayOutsideStyle,
        anonymousDivOrEditingHost);
    if (MOZ_UNLIKELY(!nextEditableContent)) {
      NS_WARNING("There was no editable content after the collapsed range");
      return nullptr;
    }

    // There is an editable content, so delete its first child (if a text node,
    // delete the first char).  If it has no children, delete it.
    if (nextEditableContent->IsText()) {
      uint32_t length = nextEditableContent->Length();
      // Bail out for empty text node.
      // XXX Do we want to do something else?
      // XXX If other browsers delete empty text node, we should follow it.
      if (!length) {
        NS_WARNING("Next editable content was an empty text node");
        return nullptr;
      }
      RefPtr<DeleteTextTransaction> deleteTextTransaction =
          DeleteTextTransaction::MaybeCreateForNextCharacter(
              *this, *nextEditableContent->AsText(), 0);
      if (!deleteTextTransaction) {
        NS_WARNING(
            "DeleteTextTransaction::MaybeCreateForNextCharacter() failed");
        return nullptr;
      }
      return deleteTextTransaction.forget();
    }

    if (IsHTMLEditor() &&
        NS_WARN_IF(!HTMLEditUtils::IsRemovableNode(*nextEditableContent))) {
      return nullptr;
    }
    RefPtr<DeleteNodeTransaction> deleteNodeTransaction =
        DeleteNodeTransaction::MaybeCreate(*this, *nextEditableContent);
    if (!deleteNodeTransaction) {
      NS_WARNING("DeleteNodeTransaction::MaybeCreate() failed");
      return nullptr;
    }
    return deleteNodeTransaction.forget();
  }

  if (point.IsInTextNode()) {
    if (aHowToHandleCollapsedRange ==
        HowToHandleCollapsedRange::ExtendBackward) {
      RefPtr<DeleteTextTransaction> deleteTextTransaction =
          DeleteTextTransaction::MaybeCreateForPreviousCharacter(
              *this, *point.ContainerAs<Text>(), point.Offset());
      NS_WARNING_ASSERTION(
          deleteTextTransaction,
          "DeleteTextTransaction::MaybeCreateForPreviousCharacter() failed");
      return deleteTextTransaction.forget();
    }
    RefPtr<DeleteTextTransaction> deleteTextTransaction =
        DeleteTextTransaction::MaybeCreateForNextCharacter(
            *this, *point.ContainerAs<Text>(), point.Offset());
    NS_WARNING_ASSERTION(
        deleteTextTransaction,
        "DeleteTextTransaction::MaybeCreateForNextCharacter() failed");
    return deleteTextTransaction.forget();
  }

  nsIContent* editableContent = nullptr;
  if (IsHTMLEditor()) {
    editableContent =
        aHowToHandleCollapsedRange == HowToHandleCollapsedRange::ExtendBackward
            ? HTMLEditUtils::GetPreviousLeafContent(
                  point, {LeafNodeOption::IgnoreNonEditableNode},
                  BlockInlineCheck::UseComputedDisplayOutsideStyle,
                  anonymousDivOrEditingHost)
            : HTMLEditUtils::GetNextLeafContent(
                  point, {LeafNodeOption::IgnoreNonEditableNode},
                  BlockInlineCheck::UseComputedDisplayOutsideStyle,
                  anonymousDivOrEditingHost);
    if (!editableContent) {
      NS_WARNING("There was no editable content around the collapsed range");
      return nullptr;
    }
    while (editableContent && editableContent->IsCharacterData() &&
           !editableContent->Length()) {
      // Can't delete an empty text node (bug 762183)
      editableContent =
          aHowToHandleCollapsedRange ==
                  HowToHandleCollapsedRange::ExtendBackward
              ? HTMLEditUtils::GetPreviousLeafContent(
                    *editableContent, {LeafNodeOption::IgnoreNonEditableNode},
                    BlockInlineCheck::UseComputedDisplayOutsideStyle,
                    anonymousDivOrEditingHost)
              : HTMLEditUtils::GetNextLeafContent(
                    *editableContent, {LeafNodeOption::IgnoreNonEditableNode},
                    BlockInlineCheck::UseComputedDisplayOutsideStyle,
                    anonymousDivOrEditingHost);
    }
    if (!editableContent) {
      NS_WARNING(
          "There was no editable content which is not empty around the "
          "collapsed range");
      return nullptr;
    }
  } else {
    MOZ_ASSERT(point.IsInTextNode());
    editableContent = point.GetContainerAs<nsIContent>();
    if (!editableContent) {
      NS_WARNING("If there was no text node, should've been handled first");
      return nullptr;
    }
  }

  if (editableContent->IsText()) {
    if (aHowToHandleCollapsedRange ==
        HowToHandleCollapsedRange::ExtendBackward) {
      RefPtr<DeleteTextTransaction> deleteTextTransaction =
          DeleteTextTransaction::MaybeCreateForPreviousCharacter(
              *this, *editableContent->AsText(), editableContent->Length());
      NS_WARNING_ASSERTION(
          deleteTextTransaction,
          "DeleteTextTransaction::MaybeCreateForPreviousCharacter() failed");
      return deleteTextTransaction.forget();
    }

    RefPtr<DeleteTextTransaction> deleteTextTransaction =
        DeleteTextTransaction::MaybeCreateForNextCharacter(
            *this, *editableContent->AsText(), 0);
    NS_WARNING_ASSERTION(
        deleteTextTransaction,
        "DeleteTextTransaction::MaybeCreateForNextCharacter() failed");
    return deleteTextTransaction.forget();
  }

  MOZ_ASSERT(IsHTMLEditor());
  if (NS_WARN_IF(!HTMLEditUtils::IsRemovableNode(*editableContent))) {
    return nullptr;
  }
  RefPtr<DeleteNodeTransaction> deleteNodeTransaction =
      DeleteNodeTransaction::MaybeCreate(*this, *editableContent);
  NS_WARNING_ASSERTION(deleteNodeTransaction,
                       "DeleteNodeTransaction::MaybeCreate() failed");
  return deleteNodeTransaction.forget();
}

bool EditorBase::FlushPendingNotificationsIfToHandleDeletionWithFrameSelection(
    nsIEditor::EDirection aDirectionAndAmount) const {
  MOZ_ASSERT(IsEditActionDataAvailable());

  if (NS_WARN_IF(Destroyed())) {
    return false;
  }
  if (!EditorUtils::IsFrameSelectionRequiredToExtendSelection(
          aDirectionAndAmount, SelectionRef())) {
    return true;
  }
  // Although AutoClonedSelectionRangeArray::ExtendAnchorFocusRangeFor() will
  // use nsFrameSelection, if it still has dirty frame, nsFrameSelection doesn't
  // extend selection since we block script.
  if (RefPtr<PresShell> presShell = GetPresShell()) {
    presShell->FlushPendingNotifications(FlushType::Layout);
    if (NS_WARN_IF(Destroyed())) {
      return false;
    }
  }
  return true;
}

nsresult EditorBase::DeleteSelectionAsAction(
    nsIEditor::EDirection aDirectionAndAmount,
    nsIEditor::EStripWrappers aStripWrappers, nsIPrincipal* aPrincipal) {
  MOZ_ASSERT(aStripWrappers == eStrip || aStripWrappers == eNoStrip);
  // Showing this assertion is fine if this method is called by outside via
  // mutation event listener or something.  Otherwise, this is called by
  // wrong method.
  NS_ASSERTION(
      !mPlaceholderBatch,
      "Should be called only when this is the only edit action of the "
      "operation unless mutation event listener nests some operations");

  // If we're a TextEditor instance, we don't need to treat parent elements
  // so that we can ignore aStripWrappers for skipping unnecessary cost.
  if (IsTextEditor()) {
    aStripWrappers = nsIEditor::eNoStrip;
  }

  EditAction editAction = EditAction::eDeleteSelection;
  switch (aDirectionAndAmount) {
    case nsIEditor::ePrevious:
      editAction = EditAction::eDeleteBackward;
      break;
    case nsIEditor::eNext:
      editAction = EditAction::eDeleteForward;
      break;
    case nsIEditor::ePreviousWord:
      editAction = EditAction::eDeleteWordBackward;
      break;
    case nsIEditor::eNextWord:
      editAction = EditAction::eDeleteWordForward;
      break;
    case nsIEditor::eToBeginningOfLine:
      editAction = EditAction::eDeleteToBeginningOfSoftLine;
      break;
    case nsIEditor::eToEndOfLine:
      editAction = EditAction::eDeleteToEndOfSoftLine;
      break;
  }

  AutoEditActionDataSetter editActionData(*this, editAction, aPrincipal);
  if (NS_WARN_IF(!editActionData.CanHandle())) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  // If there is an existing selection when an extended delete is requested,
  // platforms that use "caret-style" caret positioning collapse the
  // selection to the  start and then create a new selection.
  // Platforms that use "selection-style" caret positioning just delete the
  // existing selection without extending it.
  if (!SelectionRef().IsCollapsed()) {
    switch (aDirectionAndAmount) {
      case eNextWord:
      case ePreviousWord:
      case eToBeginningOfLine:
      case eToEndOfLine: {
        if (mCaretStyle != 1) {
          aDirectionAndAmount = eNone;
          break;
        }
        ErrorResult error;
        SelectionRef().CollapseToStart(error);
        if (NS_WARN_IF(Destroyed())) {
          error.SuppressException();
          return EditorBase::ToGenericNSResult(NS_ERROR_EDITOR_DESTROYED);
        }
        if (error.Failed()) {
          NS_WARNING("Selection::CollapseToStart() failed");
          editActionData.Abort();
          return EditorBase::ToGenericNSResult(error.StealNSResult());
        }
        break;
      }
      default:
        break;
    }
  }

  // If Selection is still NOT collapsed, it does not important removing
  // range of the operation since we'll remove the selected content.  However,
  // information of direction (backward or forward) may be important for
  // web apps.  E.g., web apps may want to mark selected range as "deleted"
  // and move caret before or after the range.  Therefore, we should forget
  // only the range information but keep range information.  See discussion
  // of the spec issue for the detail:
  // https://github.com/w3c/input-events/issues/82
  if (!SelectionRef().IsCollapsed()) {
    switch (editAction) {
      case EditAction::eDeleteWordBackward:
      case EditAction::eDeleteToBeginningOfSoftLine:
        editActionData.UpdateEditAction(EditAction::eDeleteBackward);
        break;
      case EditAction::eDeleteWordForward:
      case EditAction::eDeleteToEndOfSoftLine:
        editActionData.UpdateEditAction(EditAction::eDeleteForward);
        break;
      default:
        break;
    }
  }

  editActionData.SetSelectionCreatedByDoubleclick(
      SelectionRef().GetFrameSelection() &&
      SelectionRef().GetFrameSelection()->IsDoubleClickSelection());

  if (!FlushPendingNotificationsIfToHandleDeletionWithFrameSelection(
          aDirectionAndAmount)) {
    NS_WARNING("Flusing pending notifications caused destroying the editor");
    editActionData.Abort();
    return EditorBase::ToGenericNSResult(NS_ERROR_EDITOR_DESTROYED);
  }

  nsresult rv =
      editActionData.MaybeDispatchBeforeInputEvent(aDirectionAndAmount);
  if (NS_FAILED(rv)) {
    NS_WARNING_ASSERTION(rv == NS_ERROR_EDITOR_ACTION_CANCELED,
                         "MaybeDispatchBeforeInputEvent() failed");
    return EditorBase::ToGenericNSResult(rv);
  }

  // delete placeholder txns merge.
  AutoPlaceholderBatch treatAsOneTransaction(*this, *nsGkAtoms::DeleteTxnName,
                                             ScrollSelectionIntoView::Yes,
                                             __FUNCTION__);
  rv = DeleteSelectionAsSubAction(aDirectionAndAmount, aStripWrappers);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "EditorBase::DeleteSelectionAsSubAction() failed");
  return EditorBase::ToGenericNSResult(rv);
}

nsresult EditorBase::DeleteSelectionAsSubAction(
    nsIEditor::EDirection aDirectionAndAmount,
    nsIEditor::EStripWrappers aStripWrappers) {
  MOZ_ASSERT(IsEditActionDataAvailable());
  // If handling edit action is for table editing, this may be called with
  // selecting an any table element by the caller, but it's not usual work of
  // this so that `MayEditActionDeleteSelection()` returns false.
  MOZ_ASSERT(MayEditActionDeleteSelection(GetEditAction()) ||
             IsEditActionTableEditing(GetEditAction()));
  MOZ_ASSERT(mPlaceholderBatch);
  MOZ_ASSERT(aStripWrappers == eStrip || aStripWrappers == eNoStrip);
  NS_ASSERTION(IsHTMLEditor() || aStripWrappers == nsIEditor::eNoStrip,
               "TextEditor does not support strip wrappers");

  if (NS_WARN_IF(!mInitSucceeded)) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  IgnoredErrorResult ignoredError;
  AutoEditSubActionNotifier startToHandleEditSubAction(
      *this, EditSubAction::eDeleteSelectedContent, aDirectionAndAmount,
      ignoredError);
  if (NS_WARN_IF(ignoredError.ErrorCodeIs(NS_ERROR_EDITOR_DESTROYED))) {
    return ignoredError.StealNSResult();
  }
  NS_WARNING_ASSERTION(
      !ignoredError.Failed(),
      "TextEditor::OnStartToHandleTopLevelEditSubAction() failed, but ignored");

  {
    Result<EditActionResult, nsresult> result =
        HandleDeleteSelection(aDirectionAndAmount, aStripWrappers);
    if (MOZ_UNLIKELY(result.isErr())) {
      // If HTMLEditor::HandleDeleteSelection() returns "no editable range"
      // error and the range is collapsed and the deletion is a preparation for
      // inserting something, we wan't to keep handling the insertion without
      // error.
      if (result.inspectErr() == NS_ERROR_EDITOR_NO_DELETABLE_RANGE &&
          GetTopLevelEditSubAction() != EditSubAction::eDeleteSelectedContent) {
        return NS_OK;
      }
      NS_WARNING(nsPrintfCString("%s::HandleDeleteSelection() failed",
                                 IsTextEditor() ? "TextEditor" : "HTMLEditor")
                     .get());
      return result.unwrapErr();
    }
    if (result.inspect().Canceled()) {
      return NS_OK;
    }
  }

  // XXX This is odd.  We just tries to remove empty text node here but we
  //     refer `Selection`.  It may be modified by mutation event listeners
  //     so that we should remove the empty text node when we make it empty.
  const auto atNewStartOfSelection =
      GetFirstSelectionStartPoint<EditorDOMPoint>();
  if (NS_WARN_IF(!atNewStartOfSelection.IsSet())) {
    // XXX And also it seems that we don't need to return error here.
    //     Why don't we just ignore?  `Selection::RemoveAllRanges()` may
    //     have been called by mutation event listeners.
    return NS_ERROR_FAILURE;
  }
  if (IsHTMLEditor() && atNewStartOfSelection.IsInTextNode() &&
      !atNewStartOfSelection.GetContainer()->Length() &&
      !GetEditActionEditContext()) {
    nsresult rv = DeleteNodeWithTransaction(
        MOZ_KnownLive(*atNewStartOfSelection.ContainerAs<Text>()));
    if (NS_FAILED(rv)) {
      NS_WARNING("EditorBase::DeleteNodeWithTransaction() failed");
      return rv;
    }
  }

  // XXX I don't think that this is necessary in anonymous `<div>` element of
  //     TextEditor since there should be at most one text node and at most
  //     one padding `<br>` element so that `<br>` element won't be before
  //     caret.
  if (!TopLevelEditSubActionDataRef().mDidExplicitlySetInterLine) {
    // We prevent the caret from sticking on the left of previous `<br>`
    // element (i.e. the end of previous line) after this deletion. Bug 92124.
    if (MOZ_UNLIKELY(NS_FAILED(SelectionRef().SetInterlinePosition(
            InterlinePosition::StartOfNextLine)))) {
      NS_WARNING(
          "Selection::SetInterlinePosition(InterlinePosition::StartOfNextLine) "
          "failed");
      return NS_ERROR_FAILURE;  // Don't need to return NS_ERROR_NOT_INITIALIZED
    }
  }

  return NS_OK;
}

nsresult EditorBase::HandleDropEvent(DragEvent* aDropEvent) {
  if (NS_WARN_IF(!aDropEvent)) {
    return NS_ERROR_INVALID_ARG;
  }

  DebugOnly<nsresult> rvIgnored = CommitComposition();
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rvIgnored),
                       "EditorBase::CommitComposition() failed, but ignored");

  AutoEditActionDataSetter editActionData(*this, EditAction::eDrop);
  // We need to initialize data or dataTransfer later.  Therefore, we cannot
  // dispatch "beforeinput" event until then.
  if (NS_WARN_IF(!editActionData.CanHandle())) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  RefPtr<DataTransfer> dataTransfer = aDropEvent->GetDataTransfer();
  if (NS_WARN_IF(!dataTransfer)) {
    return NS_ERROR_FAILURE;
  }

  RefPtr<nsIWidget> widget = GetWidget();
  nsCOMPtr<nsIDragSession> dragSession = nsContentUtils::GetDragSession(widget);
  if (NS_WARN_IF(!dragSession)) {
    return NS_ERROR_FAILURE;
  }

  nsCOMPtr<nsINode> sourceNode = dataTransfer->GetMozSourceNode();

  // If there is no source document, then the drag was from another application
  // or another process (such as an out of process subframe). The latter case is
  // not currently handled below when checking for a move/copy and deleting the
  // existing text.
  RefPtr<Document> srcdoc;
  if (sourceNode) {
    srcdoc = sourceNode->OwnerDoc();
  }

  nsCOMPtr<nsIPrincipal> sourcePrincipal;
  dragSession->GetTriggeringPrincipal(getter_AddRefs(sourcePrincipal));

  if (nsContentUtils::CheckForSubFrameDrop(
          dragSession, aDropEvent->WidgetEventPtr()->AsDragEvent())) {
    // Don't allow drags from subframe documents with different origins than
    // the drop destination.
    if (IsSafeToInsertData(sourcePrincipal) == SafeToInsertData::No) {
      return NS_OK;
    }
  }

  // Current doc is destination
  RefPtr<Document> document = GetDocument();
  if (NS_WARN_IF(!document)) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  const uint32_t numItems = dataTransfer->MozItemCount();
  if (NS_WARN_IF(!numItems)) {
    return NS_ERROR_FAILURE;  // Nothing to drop?
  }

  // We have to figure out whether to delete and relocate caret only once
  // Parent and offset are under the mouse cursor.
  int32_t dropOffset = -1;
  nsCOMPtr<nsIContent> dropParentContent =
      aDropEvent->GetRangeParentContentAndOffset(&dropOffset);
  if (dropOffset < 0) {
    NS_WARNING(
        "DropEvent::GetRangeParentContentAndOffset() returned negative offset");
    return NS_ERROR_FAILURE;
  }
  EditorDOMPoint droppedAt(dropParentContent,
                           AssertedCast<uint32_t>(dropOffset));
  if (NS_WARN_IF(!droppedAt.IsInContentNode())) {
    return NS_ERROR_FAILURE;
  }

  // Check if dropping into a selected range.  If so and the source comes from
  // same document, jump through some hoops to determine if mouse is over
  // selection (bail) and whether user wants to copy selection or delete it.
  if (sourceNode && sourceNode->IsEditable() && srcdoc == document) {
    bool isPointInSelection = nsContentUtils::IsPointInSelection(
        SelectionRef(), *droppedAt.GetContainer(), droppedAt.Offset());
    if (isPointInSelection) {
      // If source document and destination document is same and dropping
      // into one of selected ranges, we don't need to do nothing.
      // XXX If the source comes from outside of this editor, this check
      //     means that we don't allow to drop the item in the selected
      //     range.  However, the selection is hidden until the <input> or
      //     <textarea> gets focus, therefore, this looks odd.
      return NS_OK;
    }
  }

  // Delete if user doesn't want to copy when user moves selected content
  // to different place in same editor.
  // XXX Do we need the check whether it's in same document or not?
  RefPtr<EditorBase> editorToDeleteSelection;
  if (sourceNode && sourceNode->IsEditable() && srcdoc == document) {
    if ((dataTransfer->DropEffectInt() &
         nsIDragService::DRAGDROP_ACTION_MOVE) &&
        !(dataTransfer->DropEffectInt() &
          nsIDragService::DRAGDROP_ACTION_COPY)) {
      // If the source node is in native anonymous tree, it must be in
      // <input> or <textarea> element.  If so, its TextEditor can remove it.
      if (sourceNode->IsInNativeAnonymousSubtree()) {
        if (RefPtr textControlElement = TextControlElement::FromNodeOrNull(
                sourceNode
                    ->GetClosestNativeAnonymousSubtreeRootParentOrHost())) {
          editorToDeleteSelection = textControlElement->GetTextEditor();
        }
      }
      // Otherwise, must be the content is in HTMLEditor.
      else if (IsHTMLEditor()) {
        editorToDeleteSelection = this;
      } else {
        editorToDeleteSelection =
            nsContentUtils::GetHTMLEditor(srcdoc->GetPresContext());
      }
    }
    // If the found editor isn't modifiable, we should not try to delete
    // selection.
    if (editorToDeleteSelection && !editorToDeleteSelection->IsModifiable()) {
      editorToDeleteSelection = nullptr;
    }
    // If the found editor has collapsed selection, we need to delete nothing
    // in the editor.
    if (editorToDeleteSelection) {
      if (Selection* selection = editorToDeleteSelection->GetSelection()) {
        if (selection->IsCollapsed()) {
          editorToDeleteSelection = nullptr;
        }
      }
    }
  }

  // Combine any deletion and drop insertion into one transaction.
  AutoPlaceholderBatch treatAsOneTransaction(
      *this, ScrollSelectionIntoView::Yes, __FUNCTION__);

  // Don't dispatch "selectionchange" event until inserting all contents.
  SelectionBatcher selectionBatcher(SelectionRef(), __FUNCTION__);

  // Track dropped point with Range because we shouldn't insert the
  // dropped content into different position even if some event listeners
  // modify selection.  Note that Chrome's behavior is really odd.  So,
  // we don't need to worry about web-compat about this.
  IgnoredErrorResult ignoredError;
  RefPtr<dom::Range> rangeAtDropPoint =
      dom::Range::Create(droppedAt.ToRawRangeBoundary(),
                         droppedAt.ToRawRangeBoundary(), ignoredError);
  if (NS_WARN_IF(ignoredError.Failed()) ||
      NS_WARN_IF(!rangeAtDropPoint->IsPositioned())) {
    editActionData.Abort();
    return NS_ERROR_FAILURE;
  }

  // Remove selected contents first here because we need to fire a pair of
  // "beforeinput" and "input" for deletion and web apps can cancel only
  // this deletion.  Note that callee may handle insertion asynchronously.
  // Therefore, it is the best to remove selected content here.
  if (editorToDeleteSelection) {
    nsresult rv = editorToDeleteSelection->DeleteSelectionByDragAsAction(
        mDispatchInputEvent);
    if (NS_WARN_IF(Destroyed())) {
      editActionData.Abort();
      return NS_OK;
    }
    // Ignore the editor instance specific error if it's another editor.
    if (this != editorToDeleteSelection &&
        (rv == NS_ERROR_NOT_INITIALIZED || rv == NS_ERROR_EDITOR_DESTROYED)) {
      rv = NS_OK;
    }
    // Don't cancel "insertFromDrop" even if "deleteByDrag" is canceled.
    if (rv != NS_ERROR_EDITOR_ACTION_CANCELED && NS_FAILED(rv)) {
      NS_WARNING("EditorBase::DeleteSelectionByDragAsAction() failed");
      editActionData.Abort();
      return EditorBase::ToGenericNSResult(rv);
    }
    if (NS_WARN_IF(!rangeAtDropPoint->IsPositioned()) ||
        NS_WARN_IF(!rangeAtDropPoint->GetStartContainer()->IsContent())) {
      editActionData.Abort();
      return NS_ERROR_FAILURE;
    }
    droppedAt = rangeAtDropPoint->StartRef();
    MOZ_ASSERT(droppedAt.IsSetAndValid());
    MOZ_ASSERT(droppedAt.IsInContentNode());
  }

  // Before inserting dropping content, we need to move focus for compatibility
  // with Chrome and firing "beforeinput" event on new editing host.
  RefPtr<Element> focusedElement, newFocusedElement;
  if (IsTextEditor()) {
    newFocusedElement = GetExposedRoot();
    focusedElement = IsActiveInDOMWindow() ? newFocusedElement : nullptr;
  }
  // TODO: We need to add automated tests when dropping something into an
  //       editing host for contenteditable which is in a shadow DOM tree
  //       and its host which is in design mode.
  else if (!droppedAt.ContainerAs<nsIContent>()->IsInDesignMode()) {
    focusedElement = AsHTMLEditor()->ComputeEditingHost();
    if (focusedElement &&
        droppedAt.ContainerAs<nsIContent>()->IsInclusiveDescendantOf(
            focusedElement)) {
      newFocusedElement = focusedElement;
    } else {
      newFocusedElement = droppedAt.ContainerAs<nsIContent>()->GetEditingHost();
    }
  }
  // Move selection right now.  Note that this does not move focus because
  // `Selection` moves focus with selection change only when the API caller is
  // JS.  And also this does not notify selection listeners (nor
  // "selectionchange") since we created SelectionBatcher above.
  ErrorResult error;
  SelectionRef().SetStartAndEnd(droppedAt.ToRawRangeBoundary(),
                                droppedAt.ToRawRangeBoundary(), error);
  if (error.Failed()) {
    NS_WARNING("Selection::SetStartAndEnd() failed");
    editActionData.Abort();
    return error.StealNSResult();
  }
  if (NS_WARN_IF(Destroyed())) {
    editActionData.Abort();
    return NS_OK;
  }
  // Then, move focus if necessary.  This must cause dispatching "blur" event
  // and "focus" event.
  if (newFocusedElement && focusedElement != newFocusedElement) {
    RefPtr<nsFocusManager> fm = nsFocusManager::GetFocusManager();
    DebugOnly<nsresult> rvIgnored = fm->SetFocus(newFocusedElement, 0);
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rvIgnored),
                         "nsFocusManager::SetFocus() failed to set focus "
                         "to the element, but ignored");
    if (NS_WARN_IF(Destroyed())) {
      editActionData.Abort();
      return NS_OK;
    }
    // "blur" or "focus" event listener may have changed the value.
    // Let's keep using the original point.
    if (NS_WARN_IF(!rangeAtDropPoint->IsPositioned()) ||
        NS_WARN_IF(!rangeAtDropPoint->GetStartContainer()->IsContent())) {
      return NS_ERROR_FAILURE;
    }
    droppedAt = rangeAtDropPoint->StartRef();
    MOZ_ASSERT(droppedAt.IsSetAndValid());

    // If focus is changed to different element and we're handling drop in
    // contenteditable, we cannot handle it without focus.  So, we should give
    // it up.
    if (IsHTMLEditor() && !AsHTMLEditor()->IsInDesignMode() &&
        NS_WARN_IF(newFocusedElement != AsHTMLEditor()->ComputeEditingHost())) {
      editActionData.Abort();
      return NS_OK;
    }
  }

  nsresult rv = InsertDroppedDataTransferAsAction(editActionData, *dataTransfer,
                                                  droppedAt, sourcePrincipal);
  if (rv == NS_ERROR_EDITOR_DESTROYED ||
      rv == NS_ERROR_EDITOR_ACTION_CANCELED) {
    return EditorBase::ToGenericNSResult(rv);
  }
  NS_WARNING_ASSERTION(
      NS_SUCCEEDED(rv),
      "EditorBase::InsertDroppedDataTransferAsAction() failed, but ignored");

  rv = ScrollSelectionFocusIntoView();
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "EditorBase::ScrollSelectionFocusIntoView() failed");
  return rv;
}

nsresult EditorBase::DeleteSelectionByDragAsAction(bool aDispatchInputEvent) {
  // TODO: Move this method to `EditorBase`.
  AutoRestore<bool> saveDispatchInputEvent(mDispatchInputEvent);
  mDispatchInputEvent = aDispatchInputEvent;
  // Even if we're handling "deleteByDrag" in same editor as "insertFromDrop",
  // we need to recreate edit action data here because
  // `AutoEditActionDataSetter` needs to manage event state separately.
  bool requestedByAnotherEditor = GetEditAction() != EditAction::eDrop;
  AutoEditActionDataSetter editActionData(*this, EditAction::eDeleteByDrag);
  MOZ_ASSERT(!SelectionRef().IsCollapsed());
  nsresult rv = editActionData.CanHandleAndMaybeDispatchBeforeInputEvent();
  if (NS_FAILED(rv)) {
    NS_WARNING_ASSERTION(rv == NS_ERROR_EDITOR_ACTION_CANCELED,
                         "CanHandleAndMaybeDispatchBeforeInputEvent() failed");
    return rv;
  }
  // But keep using placeholder transaction for "insertFromDrop" if there is.
  Maybe<AutoPlaceholderBatch> treatAsOneTransaction;
  if (requestedByAnotherEditor) {
    treatAsOneTransaction.emplace(*this, ScrollSelectionIntoView::Yes,
                                  __FUNCTION__);
  }

  // We may need to update the source node to dispatch "dragend" below.
  // Chrome restricts the new target under the <body> here.  Therefore, we
  // should follow it here.
  // https://source.chromium.org/chromium/chromium/src/+/main:third_party/blink/renderer/core/editing/editing_utilities.cc;l=254;drc=da35f4ed6398ae287d5adc828b9546eec95f668a
  const RefPtr<Element> editingHost =
      IsHTMLEditor() ? AsHTMLEditor()->ComputeEditingHost(
                           HTMLEditor::LimitInBodyElement::Yes)
                     : nullptr;

  rv = DeleteSelectionAsSubAction(nsIEditor::eNone, IsTextEditor()
                                                        ? nsIEditor::eNoStrip
                                                        : nsIEditor::eStrip);
  if (NS_FAILED(rv)) {
    NS_WARNING("EditorBase::DeleteSelectionAsSubAction(eNone) failed");
    return rv;
  }

  if (!mDispatchInputEvent) {
    return NS_OK;
  }

  if (treatAsOneTransaction.isNothing()) {
    DispatchInputEvent();
  }

  if (NS_WARN_IF(Destroyed())) {
    return NS_ERROR_EDITOR_DESTROYED;
  }

  // If we success everything here, we may need to retarget "dragend" event
  // target for compatibility with the other browsers.  They do this only when
  // their builtin editor delete the source node from the document.  Then,
  // they retarget the source node to the editing host.
  // https://source.chromium.org/chromium/chromium/src/+/main:third_party/blink/renderer/core/page/drag_controller.cc;l=724;drc=d9ba13b8cd8ac0faed7afc3d1f7e4b67ebac2a0b
  if (editingHost) {
    RefPtr<nsIWidget> widget = GetWidget();
    if (nsCOMPtr<nsIDragSession> dragSession =
            nsContentUtils::GetDragSession(widget)) {
      dragSession->MaybeEditorDeletedSourceNode(editingHost);
    }
  }
  return NS_WARN_IF(Destroyed()) ? NS_ERROR_EDITOR_DESTROYED : NS_OK;
}

Result<CaretPoint, nsresult> EditorBase::DeleteRangeWithTransaction(
    nsIEditor::EDirection aDirectionAndAmount,
    nsIEditor::EStripWrappers aStripWrappers, dom::Range& aRangeToDelete) {
  MOZ_ASSERT(IsEditActionDataAvailable());
  MOZ_ASSERT(!Destroyed());
  MOZ_ASSERT(aStripWrappers == eStrip || aStripWrappers == eNoStrip);

  HowToHandleCollapsedRange howToHandleCollapsedRange =
      EditorBase::HowToHandleCollapsedRangeFor(aDirectionAndAmount);
  if (MOZ_UNLIKELY(aRangeToDelete.Collapsed() &&
                   howToHandleCollapsedRange ==
                       HowToHandleCollapsedRange::Ignore)) {
    return CaretPoint(EditorDOMPoint(aRangeToDelete.StartRef()));
  }

  AutoClonedRangeArray rangesToDelete(aRangeToDelete);
  Result<CaretPoint, nsresult> result = DeleteRangesWithTransaction(
      aDirectionAndAmount, aStripWrappers, rangesToDelete);
  NS_WARNING_ASSERTION(result.isOk(),
                       "EditorBase::DeleteRangesWithTransaction() failed");
  return result;
}

Result<CaretPoint, nsresult> EditorBase::DeleteRangesWithTransaction(
    nsIEditor::EDirection aDirectionAndAmount,
    nsIEditor::EStripWrappers aStripWrappers,
    AutoClonedRangeArray& aRangesToDelete) {
  MOZ_ASSERT(IsEditActionDataAvailable());
  MOZ_ASSERT(!Destroyed());
  MOZ_ASSERT(aStripWrappers == eStrip || aStripWrappers == eNoStrip);
  MOZ_ASSERT(!aRangesToDelete.Ranges().IsEmpty());

  HowToHandleCollapsedRange howToHandleCollapsedRange =
      EditorBase::HowToHandleCollapsedRangeFor(aDirectionAndAmount);
  if (NS_WARN_IF(aRangesToDelete.IsCollapsed() &&
                 howToHandleCollapsedRange ==
                     HowToHandleCollapsedRange::Ignore)) {
    NS_ASSERTION(
        false,
        "For avoiding to throw incompatible exception for `execCommand`, fix "
        "the caller");
    return Err(NS_ERROR_FAILURE);
  }

  RefPtr<DeleteMultipleRangesTransaction> deleteSelectionTransaction =
      CreateTransactionForDeleteSelection(howToHandleCollapsedRange,
                                          aRangesToDelete);
  if (MOZ_UNLIKELY(!deleteSelectionTransaction)) {
    NS_WARNING("EditorBase::CreateTransactionForDeleteSelection() failed");
    return Err(NS_ERROR_FAILURE);
  }

  // XXX This is odd, this assumes that there are no multiple collapsed
  //     ranges in `Selection`, but it's possible scenario.
  // XXX This loop looks slow, but it's rarely so because of multiple
  //     selection is not used so many times.
  nsCOMPtr<nsIContent> deleteContent;
  uint32_t deleteCharOffset = 0;
  for (const OwningNonNull<EditTransactionBase>& transactionBase :
       Reversed(deleteSelectionTransaction->ChildTransactions())) {
    if (DeleteTextTransaction* deleteTextTransaction =
            transactionBase->GetAsDeleteTextTransaction()) {
      deleteContent = deleteTextTransaction->GetTextNode();
      deleteCharOffset = deleteTextTransaction->Offset();
      break;
    }
    if (DeleteNodeTransaction* deleteNodeTransaction =
            transactionBase->GetAsDeleteNodeTransaction()) {
      deleteContent = deleteNodeTransaction->GetContent();
      break;
    }
  }

  RefPtr<CharacterData> deleteCharData =
      CharacterData::FromNodeOrNull(deleteContent);
  IgnoredErrorResult ignoredError;
  AutoEditSubActionNotifier startToHandleEditSubAction(
      *this, EditSubAction::eDeleteSelectedContent, aDirectionAndAmount,
      ignoredError);
  if (NS_WARN_IF(ignoredError.ErrorCodeIs(NS_ERROR_EDITOR_DESTROYED))) {
    return Err(ignoredError.StealNSResult());
  }
  NS_WARNING_ASSERTION(
      !ignoredError.Failed(),
      "TextEditor::OnStartToHandleTopLevelEditSubAction() failed, but ignored");

  if (IsHTMLEditor()) {
    if (!deleteContent) {
      // XXX We may remove multiple ranges in the following.  Therefore,
      //     this must have a bug since we only add the first range into
      //     the changed range.
      TopLevelEditSubActionDataRef().WillDeleteRange(
          *this, aRangesToDelete.GetFirstRangeStartPoint<EditorRawDOMPoint>(),
          aRangesToDelete.GetFirstRangeEndPoint<EditorRawDOMPoint>());
    } else if (!deleteCharData) {
      TopLevelEditSubActionDataRef().WillDeleteContent(*this, *deleteContent);
    }
  }

  // Notify nsIEditActionListener::WillDelete[Selection|Text]
  if (!mActionListeners.IsEmpty()) {
    if (!deleteContent) {
      MOZ_ASSERT(!aRangesToDelete.Ranges().IsEmpty());
      AutoTArray<RefPtr<dom::Range>, 8> rangesToDelete(
          aRangesToDelete.CloneRanges<RefPtr>());
      AutoActionListenerArray listeners(mActionListeners.Clone());
      for (auto& listener : listeners) {
        DebugOnly<nsresult> rvIgnored =
            listener->WillDeleteRanges(rangesToDelete);
        NS_WARNING_ASSERTION(
            NS_SUCCEEDED(rvIgnored),
            "nsIEditActionListener::WillDeleteRanges() failed, but ignored");
        MOZ_DIAGNOSTIC_ASSERT(!Destroyed(),
                              "nsIEditActionListener::WillDeleteRanges() "
                              "must not destroy the editor");
      }
    } else if (deleteCharData) {
      AutoActionListenerArray listeners(mActionListeners.Clone());
      for (auto& listener : listeners) {
        // XXX Why don't we notify listeners of actual length?
        DebugOnly<nsresult> rvIgnored =
            listener->WillDeleteText(deleteCharData, deleteCharOffset, 1);
        NS_WARNING_ASSERTION(
            NS_SUCCEEDED(rvIgnored),
            "nsIEditActionListener::WillDeleteText() failed, but ignored");
        MOZ_DIAGNOSTIC_ASSERT(!Destroyed(),
                              "nsIEditActionListener::WillDeleteText() must "
                              "not destroy the editor");
      }
    }
  }

  // Delete the specified amount
  nsresult rv = DoTransactionInternal(deleteSelectionTransaction);
  // I'm not sure whether we should keep notifying edit action listeners or
  // stop doing it.  For now, just keep traditional behavior.
  bool destroyedByTransaction = Destroyed();
  NS_WARNING_ASSERTION(destroyedByTransaction || NS_SUCCEEDED(rv),
                       "EditorBase::DoTransactionInternal() failed");

  if (IsHTMLEditor() && deleteCharData) {
    MOZ_ASSERT(deleteContent);
    TopLevelEditSubActionDataRef().DidDeleteText(
        *this, EditorRawDOMPoint(deleteContent));
  }

  if (mTextServicesDocument && NS_SUCCEEDED(rv) && deleteContent &&
      !deleteCharData) {
    RefPtr<TextServicesDocument> textServicesDocument = mTextServicesDocument;
    textServicesDocument->DidDeleteContent(*deleteContent);
    MOZ_ASSERT(
        destroyedByTransaction || !Destroyed(),
        "TextServicesDocument::DidDeleteContent() must not destroy the editor");
  }

  if (!mActionListeners.IsEmpty() && deleteContent && !deleteCharData) {
    for (auto& listener : mActionListeners.Clone()) {
      DebugOnly<nsresult> rvIgnored =
          listener->DidDeleteNode(deleteContent, rv);
      NS_WARNING_ASSERTION(
          NS_SUCCEEDED(rvIgnored),
          "nsIEditActionListener::DidDeleteNode() failed, but ignored");
      MOZ_DIAGNOSTIC_ASSERT(
          destroyedByTransaction || !Destroyed(),
          "nsIEditActionListener::DidDeleteNode() must not destroy the editor");
    }
  }

  if (NS_WARN_IF(destroyedByTransaction)) {
    return Err(NS_ERROR_EDITOR_DESTROYED);
  }
  if (NS_FAILED(rv)) {
    return Err(rv);
  }

  return CaretPoint(deleteSelectionTransaction->SuggestPointToPutCaret());
}

already_AddRefed<Element> EditorBase::CreateHTMLContent(
    const nsAtom* aTag) const {
  MOZ_ASSERT(aTag);

  RefPtr<Document> document = GetDocument();
  if (NS_WARN_IF(!document)) {
    return nullptr;
  }

  // XXX Wallpaper over editor bug (editor tries to create elements with an
  //     empty nodename).
  if (aTag == nsGkAtoms::_empty) {
    NS_ERROR(
        "Don't pass an empty tag to EditorBase::CreateHTMLContent, "
        "check caller.");
    return nullptr;
  }

  return document->CreateElem(nsDependentAtomString(aTag), nullptr,
                              kNameSpaceID_XHTML);
}

already_AddRefed<nsTextNode> EditorBase::CreateTextNode(
    const nsAString& aData) const {
  MOZ_ASSERT(IsEditActionDataAvailable());

  Document* document = GetDocument();
  if (NS_WARN_IF(!document)) {
    return nullptr;
  }
  RefPtr<nsTextNode> text = document->CreateEmptyTextNode();
  text->MarkAsMaybeModifiedFrequently();
  if (IsPasswordEditor()) {
    text->MarkAsMaybeMasked();
  }
  // Don't notify; this node is still being created.
  DebugOnly<nsresult> rvIgnored = text->SetText(aData, false);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rvIgnored),
                       "Text::SetText() failed, but ignored");
  return text.forget();
}

NS_IMETHODIMP EditorBase::SetAttributeOrEquivalent(Element* aElement,
                                                   const nsAString& aAttribute,
                                                   const nsAString& aValue,
                                                   bool aSuppressTransaction) {
  if (NS_WARN_IF(!aElement)) {
    return NS_ERROR_NULL_POINTER;
  }

  AutoEditActionDataSetter editActionData(*this, EditAction::eSetAttribute);
  nsresult rv = editActionData.CanHandleAndMaybeDispatchBeforeInputEvent();
  if (NS_FAILED(rv)) {
    NS_WARNING_ASSERTION(rv == NS_ERROR_EDITOR_ACTION_CANCELED,
                         "CanHandleAndMaybeDispatchBeforeInputEvent() failed");
    return EditorBase::ToGenericNSResult(rv);
  }

  RefPtr<nsAtom> attribute = NS_Atomize(aAttribute);
  rv = SetAttributeOrEquivalent(aElement, attribute, aValue,
                                aSuppressTransaction);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "EditorBase::SetAttributeOrEquivalent() failed");
  return EditorBase::ToGenericNSResult(rv);
}

NS_IMETHODIMP EditorBase::RemoveAttributeOrEquivalent(
    Element* aElement, const nsAString& aAttribute, bool aSuppressTransaction) {
  if (NS_WARN_IF(!aElement)) {
    return NS_ERROR_NULL_POINTER;
  }

  AutoEditActionDataSetter editActionData(*this, EditAction::eRemoveAttribute);
  nsresult rv = editActionData.CanHandleAndMaybeDispatchBeforeInputEvent();
  if (NS_FAILED(rv)) {
    NS_WARNING_ASSERTION(rv == NS_ERROR_EDITOR_ACTION_CANCELED,
                         "CanHandleAndMaybeDispatchBeforeInputEvent() failed");
    return EditorBase::ToGenericNSResult(rv);
  }

  RefPtr<nsAtom> attribute = NS_Atomize(aAttribute);
  rv = RemoveAttributeOrEquivalent(aElement, attribute, aSuppressTransaction);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "EditorBase::RemoveAttributeOrEquivalent() failed");
  return EditorBase::ToGenericNSResult(rv);
}

void EditorBase::HandleKeyPressEventInReadOnlyMode(
    WidgetKeyboardEvent& aKeyboardEvent) const {
  MOZ_ASSERT(IsReadonly());
  MOZ_ASSERT(aKeyboardEvent.mMessage == eKeyPress);

  switch (aKeyboardEvent.mKeyCode) {
    case NS_VK_BACK:
      // If it's a `Backspace` key, let's consume it because it may be mapped
      // to "Back" of the history navigation.  So, it's possible that user
      // tries to delete a character with `Backspace` even in the read-only
      // editor.
      aKeyboardEvent.PreventDefault();
      break;
  }
  // XXX How about space key (page up and page down in browser navigation)?
}

nsresult EditorBase::HandleKeyPressEvent(WidgetKeyboardEvent* aKeyboardEvent) {
  MOZ_ASSERT(!IsReadonly());
  MOZ_ASSERT(aKeyboardEvent);
  MOZ_ASSERT(aKeyboardEvent->mMessage == eKeyPress);

  // NOTE: When you change this method, you should also change:
  //   * editor/libeditor/tests/test_texteditor_keyevent_handling.html
  //   * editor/libeditor/tests/test_htmleditor_keyevent_handling.html
  //
  // And also when you add new key handling, you need to change the subclass's
  // HandleKeyPressEvent()'s switch statement.

  switch (aKeyboardEvent->mKeyCode) {
    case NS_VK_META:
    case NS_VK_WIN:
    case NS_VK_SHIFT:
    case NS_VK_CONTROL:
    case NS_VK_ALT:
      MOZ_ASSERT_UNREACHABLE(
          "eKeyPress event shouldn't be fired for modifier keys");
      return NS_ERROR_UNEXPECTED;

    case NS_VK_BACK: {
      if (aKeyboardEvent->IsControl() || aKeyboardEvent->IsAlt() ||
          aKeyboardEvent->IsMeta()) {
        return NS_OK;
      }
      DebugOnly<nsresult> rvIgnored =
          DeleteSelectionAsAction(nsIEditor::ePrevious, nsIEditor::eStrip);
      aKeyboardEvent->PreventDefault();
      NS_WARNING_ASSERTION(
          NS_SUCCEEDED(rvIgnored),
          "EditorBase::DeleteSelectionAsAction() failed, but ignored");
      return NS_OK;
    }
    case NS_VK_DELETE: {
      // on certain platforms (such as windows) the shift key
      // modifies what delete does (cmd_cut in this case).
      // bailing here to allow the keybindings to do the cut.
      if (aKeyboardEvent->IsShift() || aKeyboardEvent->IsControl() ||
          aKeyboardEvent->IsAlt() || aKeyboardEvent->IsMeta()) {
        return NS_OK;
      }
      DebugOnly<nsresult> rvIgnored =
          DeleteSelectionAsAction(nsIEditor::eNext, nsIEditor::eStrip);
      aKeyboardEvent->PreventDefault();
      NS_WARNING_ASSERTION(
          NS_SUCCEEDED(rvIgnored),
          "EditorBase::DeleteSelectionAsAction() failed, but ignored");
      return NS_OK;
    }
  }
  return NS_OK;
}

nsresult EditorBase::OnInputText(const nsAString& aStringToInsert) {
  AutoEditActionDataSetter editActionData(*this, EditAction::eInsertText);
  MOZ_ASSERT(!aStringToInsert.IsVoid());

  MOZ_LOG(gTextInputLog, LogLevel::Info,
          ("%p %s::OnInputText(aStringToInsert=\"%s\")", this,
           mIsHTMLEditorClass ? "HTMLEditor" : "TextEditor",
           NS_ConvertUTF16toUTF8(aStringToInsert).get()));

  editActionData.SetData(aStringToInsert);
  // FYI: For conforming to current UI Events spec, we should dispatch
  //      "beforeinput" event before "keypress" event, but here is in a
  //      "keypress" event listener.  However, the other browsers dispatch
  //      "beforeinput" event after "keypress" event.  Therefore, it makes
  //      sense to follow the other browsers.  Spec issue:
  //      https://github.com/w3c/uievents/issues/220
  nsresult rv = editActionData.CanHandleAndMaybeDispatchBeforeInputEvent();
  if (NS_FAILED(rv)) {
    NS_WARNING_ASSERTION(rv == NS_ERROR_EDITOR_ACTION_CANCELED,
                         "CanHandleAndMaybeDispatchBeforeInputEvent() failed");
    return EditorBase::ToGenericNSResult(rv);
  }

  AutoPlaceholderBatch treatAsOneTransaction(*this, *nsGkAtoms::TypingTxnName,
                                             ScrollSelectionIntoView::Yes,
                                             __FUNCTION__);
  rv = InsertTextAsSubAction(aStringToInsert, InsertTextFor::NormalText);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "EditorBase::InsertTextAsSubAction() failed");
  return EditorBase::ToGenericNSResult(rv);
}

nsresult EditorBase::ReplaceTextAsAction(
    const nsAString& aString, dom::Range* aReplaceRange,
    AllowBeforeInputEventCancelable aAllowBeforeInputEventCancelable,
    PreventSetSelection aPreventSetSelection, nsIPrincipal* aPrincipal) {
  MOZ_ASSERT(aString.FindChar(nsCRT::CR) == kNotFound);
  MOZ_ASSERT_IF(!aReplaceRange, IsTextEditor());

  AutoEditActionDataSetter editActionData(*this, EditAction::eReplaceText,
                                          aPrincipal);
  if (NS_WARN_IF(!editActionData.CanHandle())) {
    return NS_ERROR_NOT_INITIALIZED;
  }
  if (aAllowBeforeInputEventCancelable == AllowBeforeInputEventCancelable::No) {
    editActionData.MakeBeforeInputEventNonCancelable();
  }

  RefPtr<dom::Range> targetRange = [&]() -> already_AddRefed<dom::Range> {
    if (aReplaceRange) {
      RefPtr<dom::Range> range = dom::Range::Create(
          aReplaceRange->GetStartContainer(), aReplaceRange->StartOffset(),
          aReplaceRange->GetEndContainer(), aReplaceRange->EndOffset(),
          IgnoreErrors());
      NS_WARNING_ASSERTION(range && range->IsPositioned(),
                           "Range::Create() failed");
      return range.forget();
    }
    nsIContent* const rootContentToSelectAll =
        IsTextEditor()
            ? AsTextEditor()->GetTextNode()
            : static_cast<nsIContent*>(AsHTMLEditor()->ComputeEditingHost());
    if (NS_WARN_IF(!rootContentToSelectAll)) {
      return nullptr;
    }
    RefPtr<dom::Range> range =
        dom::Range::Create(rootContentToSelectAll, 0, rootContentToSelectAll,
                           rootContentToSelectAll->Length(), IgnoreErrors());
    NS_WARNING_ASSERTION(range && range->IsPositioned(),
                         "Range::Create() failed");
    return range.forget();
  }();
  if (NS_WARN_IF(!targetRange) || NS_WARN_IF(!targetRange->IsPositioned())) {
    return NS_ERROR_FAILURE;
  }
  if (IsTextEditor()) {
    editActionData.SetData(aString);
  } else {
    editActionData.InitializeDataTransfer(aString);
    RefPtr<StaticRange> staticTargetRange = StaticRange::Create(
        targetRange->StartRef(), targetRange->EndRef(), IgnoreErrors());
    MOZ_ASSERT(staticTargetRange);
    MOZ_ASSERT(staticTargetRange->IsPositioned());
    editActionData.AppendTargetRange(std::move(staticTargetRange));
  }

  AutoSelectionRestorer restorer(
      aPreventSetSelection == PreventSetSelection::Yes ? this : nullptr);
  nsresult rv = NS_OK;
  auto raii = MakeScopeExit([&] {
    if (aPreventSetSelection == PreventSetSelection::Yes && NS_FAILED(rv)) {
      restorer.Abort();
    }
  });

  // Before dispatching eEditorBeforeInput, we should set `Selection` as the
  // target range.  Then, we can expose the target range with
  // .selectionStart and .selectionEnd, etc even on TextEditor too.
  if (SelectionRef().RangeCount() != 1u ||
      !targetRange->HasEqualBoundaries(*SelectionRef().GetRangeAt(0u))) {
    IgnoredErrorResult error;
    SelectionRef().RemoveAllRanges(error);
    if (MOZ_UNLIKELY(error.Failed())) {
      NS_WARNING("Selection::RemoveAllRanges() failed");
      rv = error.StealNSResult();  // rv is used by `raii`.
      return rv;
    }
    SelectionRef().AddRangeAndSelectFramesAndNotifyListeners(*targetRange,
                                                             error);
    if (MOZ_UNLIKELY(error.Failed())) {
      NS_WARNING(
          "Selection::AddRangeAndSelectFramesAndNotifyListeners() failed");
      rv = error.StealNSResult();  // rv is used by `raii`.
      return rv;
    }
  }

  rv = editActionData.MaybeDispatchBeforeInputEvent();
  if (NS_FAILED(rv)) {
    NS_WARNING_ASSERTION(rv == NS_ERROR_EDITOR_ACTION_CANCELED,
                         "MaybeDispatchBeforeInputEvent() failed");
    return EditorBase::ToGenericNSResult(rv);
  }

  // If a `beforeinput` event listener changed the `Selection`, we should should
  // not restore the original one because restoring Selection may confuse the
  // web app.
  if (SelectionRef().RangeCount() != 1u ||
      !targetRange->HasEqualBoundaries(*SelectionRef().GetRangeAt(0u))) {
    restorer.Abort();
  }

  if (editActionData.GetEditContext()) {
    // For EditContext, we only dispatch beforeinput - we don't
    // try to do the replacement.
    // https://w3c.github.io/edit-context/#example-how-beforeinput-can-be-used-to-catch-insertreplacementtext-to-apply-spelling-changes
    return NS_OK;
  }

  AutoPlaceholderBatch treatAsOneTransaction(
      *this, ScrollSelectionIntoView::Yes, __FUNCTION__);

  // This should emulates inserting text for better undo/redo behavior.
  IgnoredErrorResult ignoredError;
  AutoEditSubActionNotifier startToHandleEditSubAction(
      *this, EditSubAction::eInsertText, nsIEditor::eNext, ignoredError);
  if (NS_WARN_IF(ignoredError.ErrorCodeIs(NS_ERROR_EDITOR_DESTROYED))) {
    rv = NS_ERROR_EDITOR_DESTROYED;  // rv is used by `raii`.
    return EditorBase::ToGenericNSResult(rv);
  }
  NS_WARNING_ASSERTION(
      !ignoredError.Failed(),
      "TextEditor::OnStartToHandleTopLevelEditSubAction() failed, but ignored");

  if (!aReplaceRange) {
    // Use fast path if we're `TextEditor` because it may be in a hot path.
    if (IsTextEditor()) {
      restorer.Abort();  // XXX Is this intended?
      nsresult rv = MOZ_KnownLive(AsTextEditor())->SetTextAsSubAction(aString);
      NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                           "TextEditor::SetTextAsSubAction() failed");
      return EditorBase::ToGenericNSResult(rv);
    }

    MOZ_ASSERT_UNREACHABLE("Setting value of `HTMLEditor` isn't supported");
    rv = NS_ERROR_FAILURE;  // rv is used by `raii`.
    return EditorBase::ToGenericNSResult(rv);
  }

  if (aString.IsEmpty() && aReplaceRange->Collapsed()) {
    restorer.Abort();  // XXX Is this intended?

    NS_WARNING("Setting value was empty and replaced range was empty");
    return NS_OK;
  }

  rv = ReplaceSelectionAsSubAction(aString);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "EditorBase::ReplaceSelectionAsSubAction() failed");

  return EditorBase::ToGenericNSResult(rv);
}

nsresult EditorBase::ReplaceSelectionAsSubAction(const nsAString& aString) {
  if (aString.IsEmpty()) {
    nsresult rv = DeleteSelectionAsSubAction(
        nsIEditor::eNone,
        IsTextEditor() ? nsIEditor::eNoStrip : nsIEditor::eStrip);
    NS_WARNING_ASSERTION(
        NS_SUCCEEDED(rv),
        "EditorBase::DeleteSelectionAsSubAction(eNone) failed");
    return rv;
  }

  nsresult rv = InsertTextAsSubAction(aString, InsertTextFor::NormalText);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "EditorBase::InsertTextAsSubAction() failed");
  return rv;
}

nsresult EditorBase::HandleInlineSpellCheck(
    const EditorDOMPoint& aPreviouslySelectedStart,
    const AbstractRange* aRange) {
  MOZ_ASSERT(IsEditActionDataAvailable());

  if (!mInlineSpellChecker) {
    return NS_OK;
  }
  nsresult rv = mInlineSpellChecker->SpellCheckAfterEditorChange(
      GetTopLevelEditSubAction(), SelectionRef(),
      aPreviouslySelectedStart.GetContainer(),
      aPreviouslySelectedStart.Offset(),
      aRange ? aRange->GetStartContainer() : nullptr,
      aRange ? aRange->StartOffset() : 0,
      aRange ? aRange->GetEndContainer() : nullptr,
      aRange ? aRange->EndOffset() : 0);
  NS_WARNING_ASSERTION(
      NS_SUCCEEDED(rv),
      "mozInlineSpellChecker::SpellCheckAfterEditorChange() failed");
  return rv;
}

Element* EditorBase::FindSelectionRoot(const nsINode& /*aNode*/) const {
  return GetRoot();
}

void EditorBase::InitializeSelectionAncestorLimit(
    Element& aAncestorLimit) const {
  MOZ_ASSERT(IsEditActionDataAvailable());

  MOZ_KnownLive(SelectionRef()).SetAncestorLimiter(&aAncestorLimit);
}

nsresult EditorBase::InitializeSelection(
    const nsINode& aOriginalEventTargetNode) {
  MOZ_ASSERT(IsEditActionDataAvailable());

  const RefPtr<Element> selectionRootContent =
      FindSelectionRoot(aOriginalEventTargetNode);
  if (!selectionRootContent) {
    return NS_OK;
  }

  nsCOMPtr<nsISelectionController> selectionController =
      GetSelectionController();
  if (NS_WARN_IF(!selectionController)) {
    return NS_ERROR_FAILURE;
  }

  // Init the caret
  RefPtr<nsCaret> caret = GetCaretForSelection();
  if (NS_WARN_IF(!caret)) {
    return NS_ERROR_FAILURE;
  }
  caret->SetSelection(&SelectionRef());
  DebugOnly<nsresult> rvIgnored =
      selectionController->SetCaretReadOnly(IsReadonly());
  NS_WARNING_ASSERTION(
      NS_SUCCEEDED(rvIgnored),
      "nsISelectionController::SetCaretReadOnly() failed, but ignored");
  const bool isCanvasEditContext =
      selectionRootContent->HasFlag(ELEMENT_HAS_EDIT_CONTEXT) &&
      selectionRootContent->IsHTMLElement(nsGkAtoms::canvas);
  // <canvas>-based EditContext shouldn't enable the caret, even in
  // caret browsing mode, since it's the web app's responsibility to
  // render it, and the arrow keys should be interacting with the text
  // rendered to the canvas.
  rvIgnored = selectionController->SetCaretEnabled(!isCanvasEditContext);
  NS_WARNING_ASSERTION(
      NS_SUCCEEDED(rvIgnored),
      "nsISelectionController::SetCaretEnabled() failed, but ignored");
  // Init selection
  rvIgnored =
      selectionController->SetSelectionFlags(nsISelectionDisplay::DISPLAY_ALL);
  NS_WARNING_ASSERTION(
      NS_SUCCEEDED(rvIgnored),
      "nsISelectionController::SetSelectionFlags() failed, but ignored");

  selectionController->SelectionWillTakeFocus();

  // If the computed selection root isn't root content, we should set it
  // as selection ancestor limit.  However, if that is root element, it means
  // there is not limitation of the selection, then, we must set nullptr.
  // NOTE: If we set a root element to the ancestor limit, some selection
  // methods don't work fine.
  if (selectionRootContent->GetParent()) {
    InitializeSelectionAncestorLimit(*selectionRootContent);
  } else {
    SelectionRef().SetAncestorLimiter(nullptr);
  }

  // If there is composition in a text control when this method is called, we
  // may need to restore IME selection because if the text control is reframed,
  // this already forgot IME selection and the transaction.
  // Note that if this is an HTMLEditor, updating composition makes the new
  // composition string appear around IME or normal selection.  Therefore,
  // we don't need to do nothing here.
  if (IsTextEditor() && mComposition && mComposition->IsMovingToNewTextNode()) {
    // We need to look for the new text node from current selection.
    // XXX If selection is changed during reframe, this doesn't work well!
    const auto atStartOfFirstRange =
        EditorBase::GetFirstSelectionStartPoint<EditorRawDOMPoint>();
    EditorRawDOMPoint betterInsertionPoint =
        AsTextEditor()->FindBetterInsertionPoint(atStartOfFirstRange);
    RefPtr<Text> textNode = betterInsertionPoint.GetContainerAs<Text>();
    MOZ_ASSERT(textNode,
               "There must be text node if composition string is not empty");
    if (textNode) {
      MOZ_ASSERT(textNode->Length() >=
                     mComposition->EndOffsetMaybeInFollowingTextNode(),
                 "The text node must be different from the old text node");
      RefPtr<TextRangeArray> ranges = mComposition->GetRanges();
      DebugOnly<nsresult> rvIgnored = CompositionTransaction::SetIMESelection(
          *this, textNode, mComposition->StartOffsetMaybeInFollowingTextNode(),
          mComposition->LengthMaybeInFollowingTextNode(), ranges);
      NS_WARNING_ASSERTION(
          NS_SUCCEEDED(rvIgnored),
          "CompositionTransaction::SetIMESelection() failed, but ignored");
      mComposition->OnUpdateCompositionInEditor(
          mComposition->String(), *textNode,
          mComposition->StartOffsetMaybeInFollowingTextNode());
    }
  }

  return NS_OK;
}

nsresult EditorBase::FinalizeSelection() {
  nsCOMPtr<nsISelectionController> selectionController =
      GetSelectionController();
  if (NS_WARN_IF(!selectionController)) {
    return NS_ERROR_FAILURE;
  }

  AutoEditActionDataSetter editActionData(*this, EditAction::eNotEditing);
  if (NS_WARN_IF(!editActionData.CanHandle())) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  SelectionRef().SetAncestorLimiter(nullptr);

  if (NS_WARN_IF(!GetPresShell())) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  if (RefPtr<nsCaret> caret = GetCaretForSelection()) {
    DebugOnly<nsresult> rvIgnored = selectionController->SetCaretEnabled(false);
    NS_WARNING_ASSERTION(
        NS_SUCCEEDED(rvIgnored),
        "nsISelectionController::SetCaretEnabled(false) failed, but ignored");
  }

  RefPtr<nsFocusManager> focusManager = nsFocusManager::GetFocusManager();
  if (NS_WARN_IF(!focusManager)) {
    return NS_ERROR_NOT_INITIALIZED;
  }
  // TODO: Running script from here makes harder to handle blur events.  We
  //       should do this asynchronously.
  focusManager->UpdateCaretForCaretBrowsingMode();
  if (Element* rootElement = GetExposedRoot()) {
    if (rootElement->OwnerDoc()->GetUnretargetedFocusedContent() !=
        rootElement) {
      selectionController->SelectionWillLoseFocus();
    } else {
      // We leave this selection as the focused one. When the focus returns, it
      // either returns to us (nothing to do), or it returns to something else,
      // and nsDocumentViewerFocusListener::HandleEvent fixes it up.
    }
  }
  return NS_OK;
}

Element* EditorBase::GetExposedRoot() const {
  Element* rootElement = GetRoot();
  if (!rootElement || !rootElement->IsInNativeAnonymousSubtree()) {
    return rootElement;
  }
  return Element::FromNodeOrNull(
      rootElement->GetClosestNativeAnonymousSubtreeRootParentOrHost());
}

nsresult EditorBase::DetermineCurrentDirection() {
  // Get the current root direction from its frame
  Element* rootElement = GetExposedRoot();
  if (NS_WARN_IF(!rootElement)) {
    return NS_ERROR_FAILURE;
  }

  // If we don't have an explicit direction, determine our direction
  // from the content's direction
  if (!IsRightToLeft() && !IsLeftToRight()) {
    nsIFrame* frameForRootElement = rootElement->GetPrimaryFrame();
    if (NS_WARN_IF(!frameForRootElement)) {
      return NS_ERROR_FAILURE;
    }

    // Set the flag here, to enable us to use the same code path below.
    // It will be flipped before returning from the function.
    if (frameForRootElement->StyleVisibility()->mDirection ==
        StyleDirection::Rtl) {
      mFlags |= nsIEditor::eEditorRightToLeft;
    } else {
      mFlags |= nsIEditor::eEditorLeftToRight;
    }
  }

  return NS_OK;
}

nsresult EditorBase::ToggleTextDirectionAsAction(nsIPrincipal* aPrincipal) {
  AutoEditActionDataSetter editActionData(*this, EditAction::eSetTextDirection,
                                          aPrincipal);
  if (NS_WARN_IF(!editActionData.CanHandle())) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  nsresult rv = DetermineCurrentDirection();
  if (NS_FAILED(rv)) {
    NS_WARNING("EditorBase::DetermineCurrentDirection() failed");
    return EditorBase::ToGenericNSResult(rv);
  }

  MOZ_ASSERT(IsRightToLeft() || IsLeftToRight());
  // Note that we need to consider new direction before dispatching
  // "beforeinput" event since "beforeinput" event listener may change it
  // but not canceled.
  TextDirection newDirection =
      IsRightToLeft() ? TextDirection::eLTR : TextDirection::eRTL;
  editActionData.SetData(IsRightToLeft() ? u"ltr"_ns : u"rtl"_ns);

  // FYI: Oddly, Chrome does not dispatch beforeinput event in this case but
  //      dispatches input event.
  rv = editActionData.MaybeDispatchBeforeInputEvent();
  if (NS_FAILED(rv)) {
    NS_WARNING_ASSERTION(rv == NS_ERROR_EDITOR_ACTION_CANCELED,
                         "MaybeDispatchBeforeInputEvent() failed");
    return EditorBase::ToGenericNSResult(rv);
  }

  rv = SetTextDirectionTo(newDirection);
  if (NS_FAILED(rv)) {
    NS_WARNING("EditorBase::SetTextDirectionTo() failed");
    return EditorBase::ToGenericNSResult(rv);
  }

  editActionData.MarkAsHandled();

  // XXX When we don't change the text direction, do we really need to
  //     dispatch input event?
  DispatchInputEvent();

  return NS_OK;
}

void EditorBase::SwitchTextDirectionTo(TextDirection aTextDirection) {
  MOZ_ASSERT(aTextDirection == TextDirection::eLTR ||
             aTextDirection == TextDirection::eRTL);

  AutoEditActionDataSetter editActionData(*this, EditAction::eSetTextDirection);
  if (NS_WARN_IF(!editActionData.CanHandle())) {
    return;
  }

  nsresult rv = DetermineCurrentDirection();
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return;
  }

  editActionData.SetData(aTextDirection == TextDirection::eLTR ? u"ltr"_ns
                                                               : u"rtl"_ns);

  // FYI: Oddly, Chrome does not dispatch beforeinput event in this case but
  //      dispatches input event.
  rv = editActionData.MaybeDispatchBeforeInputEvent();
  if (NS_FAILED(rv)) {
    NS_WARNING_ASSERTION(rv == NS_ERROR_EDITOR_ACTION_CANCELED,
                         "MaybeDispatchBeforeInputEvent() failed");
    return;
  }

  if ((aTextDirection == TextDirection::eLTR && IsRightToLeft()) ||
      (aTextDirection == TextDirection::eRTL && IsLeftToRight())) {
    // Do it only when the direction is still different from the original
    // new direction.  Note that "beforeinput" event listener may have already
    // changed the direction here, but they may not cancel the event.
    nsresult rv = SetTextDirectionTo(aTextDirection);
    if (NS_FAILED(rv)) {
      NS_WARNING("EditorBase::SetTextDirectionTo() failed");
      return;
    }
  }

  editActionData.MarkAsHandled();

  // XXX When we don't change the text direction, do we really need to
  //     dispatch input event?
  DispatchInputEvent();
}

nsresult EditorBase::SetTextDirectionTo(TextDirection aTextDirection) {
  const RefPtr<Element> editingHostOrTextControlElement =
      IsHTMLEditor() ? AsHTMLEditor()->ComputeEditingHost(
                           HTMLEditor::LimitInBodyElement::No)
                     : GetExposedRoot();
  if (!editingHostOrTextControlElement) {  // Don't warn, HTMLEditor may have no
                                           // active editing host
    return NS_OK;
  }

  if (aTextDirection == TextDirection::eLTR) {
    NS_ASSERTION(!IsLeftToRight(), "Unexpected mutually exclusive flag");
    mFlags &= ~nsIEditor::eEditorRightToLeft;
    mFlags |= nsIEditor::eEditorLeftToRight;
    nsresult rv =
        AutoElementAttrAPIWrapper(*this, *editingHostOrTextControlElement)
            .SetAttr(nsGkAtoms::dir, u"ltr"_ns, true);
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                         "AutoElementAttrAPIWrapper::SetAttr() failed");
    return rv;
  }

  if (aTextDirection == TextDirection::eRTL) {
    NS_ASSERTION(!IsRightToLeft(), "Unexpected mutually exclusive flag");
    mFlags |= nsIEditor::eEditorRightToLeft;
    mFlags &= ~nsIEditor::eEditorLeftToRight;
    nsresult rv =
        AutoElementAttrAPIWrapper(*this, *editingHostOrTextControlElement)
            .SetAttr(nsGkAtoms::dir, u"rtl"_ns, true);
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                         "AutoElementAttrAPIWrapper::SetAttr() failed");
    return rv;
  }

  return NS_OK;
}

Element* EditorBase::GetFocusedElement() const {
  EventTarget* eventTarget = GetDOMEventTarget();
  if (!eventTarget) {
    return nullptr;
  }

  Element* const focusedElement = nsFocusManager::GetFocusedElementStatic();
  MOZ_ASSERT((focusedElement == eventTarget) ==
             SameCOMIdentity(focusedElement, eventTarget));

  return (focusedElement == eventTarget) ? focusedElement : nullptr;
}

bool EditorBase::IsActiveInDOMWindow() const {
  EventTarget* piTarget = GetDOMEventTarget();
  if (!piTarget) {
    return false;
  }

  nsFocusManager* focusManager = nsFocusManager::GetFocusManager();
  if (NS_WARN_IF(!focusManager)) {
    return false;  // Do we need to check the singleton instance??
  }

  Document* document = GetDocument();
  if (NS_WARN_IF(!document)) {
    return false;
  }
  nsPIDOMWindowOuter* ourWindow = document->GetWindow();
  nsCOMPtr<nsPIDOMWindowOuter> win;
  nsIContent* content = nsFocusManager::GetFocusedDescendant(
      ourWindow, nsFocusManager::eOnlyCurrentWindow, getter_AddRefs(win));
  return SameCOMIdentity(content, piTarget);
}

bool EditorBase::IsAcceptableInputEvent(WidgetGUIEvent* aGUIEvent) const {
  // If the event is trusted, the event should always cause input.
  if (NS_WARN_IF(!aGUIEvent)) {
    return false;
  }

  // If this is dispatched by using cordinates but this editor doesn't have
  // focus, we shouldn't handle it.
  if (aGUIEvent->IsUsingCoordinates() && !GetFocusedElement()) {
    return false;
  }

  // If a composition event isn't dispatched via widget, we need to ignore them
  // since they cannot be managed by TextComposition. E.g., the event was
  // created by chrome JS.
  // Note that if we allow to handle such events, editor may be confused by
  // strange event order.
  bool needsWidget = false;
  switch (aGUIEvent->mMessage) {
    case eUnidentifiedEvent:
      // If events are not created with proper event interface, their message
      // are initialized with eUnidentifiedEvent.  Let's ignore such event.
      return false;
    case eCompositionStart:
    case eCompositionEnd:
    case eCompositionUpdate:
    case eCompositionChange:
    case eCompositionCommitAsIs:
      // Don't allow composition events whose internal event are not
      // WidgetCompositionEvent.
      if (!aGUIEvent->AsCompositionEvent()) {
        return false;
      }
      needsWidget = true;
      break;
    default:
      break;
  }
  if (needsWidget && !aGUIEvent->mWidget) {
    return false;
  }

  // Accept all trusted events.
  if (aGUIEvent->IsTrusted()) {
    return true;
  }

  // Ignore untrusted mouse event.
  // XXX Why are we handling other untrusted input events?
  if (aGUIEvent->AsMouseEventBase()) {
    return false;
  }

  // Otherwise, we shouldn't handle any input events when we're not an active
  // element of the DOM window.
  return IsActiveInDOMWindow();
}

nsresult EditorBase::FlushPendingSpellCheck() {
  // If the spell check skip flag is still enabled from creation time,
  // disable it because focused editors are allowed to spell check.
  if (!ShouldSkipSpellCheck()) {
    return NS_OK;
  }
  MOZ_ASSERT(!IsHTMLEditor(), "HTMLEditor should not has pending spell checks");
  nsresult rv = RemoveFlags(nsIEditor::eEditorSkipSpellCheck);
  if (NS_WARN_IF(Destroyed())) {
    return NS_ERROR_EDITOR_DESTROYED;
  }
  NS_WARNING_ASSERTION(
      NS_SUCCEEDED(rv),
      "EditorBase::RemoveFlags(nsIEditor::eEditorSkipSpellCheck) failed");
  return rv;
}

bool EditorBase::CanKeepHandlingFocusEvent(
    const nsINode& aOriginalEventTargetNode) const {
  if (MOZ_UNLIKELY(!IsListeningToEvents() || Destroyed())) {
    return false;
  }

  // If the event target is document mode, we only need to handle the focus
  // event when the document is still in designMode.  Otherwise, the
  // mode has been disabled by somebody while we're handling the focus event.
  if (aOriginalEventTargetNode.IsDocument()) {
    return IsHTMLEditor() && aOriginalEventTargetNode.IsInDesignMode();
  }
  MOZ_ASSERT(aOriginalEventTargetNode.IsContent());

  // If nobody has focus, the focus event target has been blurred by somebody
  // else.  So the editor shouldn't initialize itself to start to handle
  // anything.
  const Element* const focusedElement =
      nsFocusManager::GetFocusedElementStatic();
  if (!focusedElement) {
    return false;
  }
  // If focused element is not editable, don't focus the HTML editor.
  if (IsHTMLEditor() && !focusedElement->IsEditable()) {
    return false;
  }

  // If there's an HTMLEditor registered in the target document and we
  // are not that HTMLEditor (for cases like nested documents), let
  // that HTMLEditor to handle the focus event.
  if (IsHTMLEditor()) {
    const HTMLEditor* precedentHTMLEditor =
        aOriginalEventTargetNode.OwnerDoc()->GetHTMLEditor();

    if (precedentHTMLEditor && precedentHTMLEditor != this) {
      return false;
    }
  }

  const nsIContent* exposedTargetContent =
      aOriginalEventTargetNode.AsContent()
          ->FindFirstNonChromeOnlyAccessContent();
  const nsIContent* exposedFocusedContent =
      focusedElement->FindFirstNonChromeOnlyAccessContent();
  return exposedTargetContent && exposedFocusedContent &&
         exposedTargetContent == exposedFocusedContent;
}

nsresult EditorBase::OnFocus(const nsINode& aOriginalEventTargetNode) {
  MOZ_ASSERT(IsEditActionDataAvailable());

  InitializeSelection(aOriginalEventTargetNode);
  MOZ_ASSERT(CanKeepHandlingFocusEvent(aOriginalEventTargetNode),
             "Selection listeners shouldn't change the focus");
  return NS_OK;
}

void EditorBase::PostHandleFocusEvent(const nsINode& aFocusEventTargetNode) {
  if (!CanKeepHandlingFocusEvent(aFocusEventTargetNode)) [[unlikely]] {
    return;
  }

  mSpellCheckerDictionaryUpdated = false;
  if (mInlineSpellChecker && CanEnableSpellCheck()) {
    DebugOnly<nsresult> rvIgnored =
        mInlineSpellChecker->UpdateCurrentDictionary();
    NS_WARNING_ASSERTION(
        NS_SUCCEEDED(rvIgnored),
        "mozInlineSpellChecker::UpdateCurrentDictionary() failed, but ignored");
    mSpellCheckerDictionaryUpdated = true;
  }
  if (!CanKeepHandlingFocusEvent(aFocusEventTargetNode)) [[unlikely]] {
    return;
  }

  const RefPtr<Element> focusedElement = GetFocusedElement();
  const RefPtr<nsPresContext> presContext =
      focusedElement ? focusedElement->GetPresContext(
                           Element::PresContextFor::eForComposedDoc)
                     : GetPresContext();
  if (NS_WARN_IF(!presContext)) {
    return;
  }
  IMEStateManager::OnFocusInEditor(*presContext, focusedElement, *this);
}

void EditorBase::HideCaret(bool aHide) {
  if (mHidingCaret == aHide) {
    return;
  }

  RefPtr<nsCaret> caret = GetCaretForSelection();
  if (NS_WARN_IF(!caret)) {
    return;
  }

  mHidingCaret = aHide;
  if (aHide) {
    caret->AddForceHide();
  } else {
    caret->RemoveForceHide();
  }
}

NS_IMETHODIMP EditorBase::Unmask(uint32_t aStart, int64_t aEnd,
                                 uint32_t aTimeout, uint8_t aArgc) {
  if (NS_WARN_IF(!IsPasswordEditor())) {
    return NS_ERROR_NOT_AVAILABLE;
  }
  if (NS_WARN_IF(aArgc >= 1 && aStart == UINT32_MAX) ||
      NS_WARN_IF(aArgc >= 2 && aEnd == 0) ||
      NS_WARN_IF(aArgc >= 2 && aEnd > 0 && aStart >= aEnd)) {
    return NS_ERROR_INVALID_ARG;
  }

  AutoEditActionDataSetter editActionData(*this, EditAction::eHidePassword);
  if (NS_WARN_IF(!editActionData.CanHandle())) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  uint32_t start = aArgc < 1 ? 0 : aStart;
  uint32_t length = aArgc < 2 || aEnd < 0 ? UINT32_MAX : aEnd - start;
  uint32_t timeout = aArgc < 3 ? 0 : aTimeout;
  nsresult rv = MOZ_KnownLive(AsTextEditor())
                    ->SetUnmaskRangeAndNotify(start, length, timeout);
  if (NS_FAILED(rv)) {
    NS_WARNING("TextEditor::SetUnmaskRangeAndNotify() failed");
    return EditorBase::ToGenericNSResult(rv);
  }

  // Flush pending layout right now since the caller may access us before
  // doing it.
  if (RefPtr<PresShell> presShell = GetPresShell()) {
    presShell->FlushPendingNotifications(FlushType::Layout);
  }

  return NS_OK;
}

NS_IMETHODIMP EditorBase::Mask() {
  if (NS_WARN_IF(!IsPasswordEditor())) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  AutoEditActionDataSetter editActionData(*this, EditAction::eHidePassword);
  if (NS_WARN_IF(!editActionData.CanHandle())) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  nsresult rv = MOZ_KnownLive(AsTextEditor())->MaskAllCharactersAndNotify();
  if (NS_FAILED(rv)) {
    NS_WARNING("TextEditor::MaskAllCharactersAndNotify() failed");
    return EditorBase::ToGenericNSResult(rv);
  }

  // Flush pending layout right now since the caller may access us before
  // doing it.
  if (RefPtr<PresShell> presShell = GetPresShell()) {
    presShell->FlushPendingNotifications(FlushType::Layout);
  }

  return NS_OK;
}

NS_IMETHODIMP EditorBase::GetUnmaskedStart(uint32_t* aResult) {
  if (NS_WARN_IF(!IsPasswordEditor())) {
    *aResult = 0;
    return NS_ERROR_NOT_AVAILABLE;
  }
  *aResult =
      AsTextEditor()->IsAllMasked() ? 0 : AsTextEditor()->UnmaskedStart();
  return NS_OK;
}

NS_IMETHODIMP EditorBase::GetUnmaskedEnd(uint32_t* aResult) {
  if (NS_WARN_IF(!IsPasswordEditor())) {
    *aResult = 0;
    return NS_ERROR_NOT_AVAILABLE;
  }
  *aResult = AsTextEditor()->IsAllMasked() ? 0 : AsTextEditor()->UnmaskedEnd();
  return NS_OK;
}

NS_IMETHODIMP EditorBase::GetAutoMaskingEnabled(bool* aResult) {
  if (NS_WARN_IF(!IsPasswordEditor())) {
    *aResult = false;
    return NS_ERROR_NOT_AVAILABLE;
  }
  *aResult = AsTextEditor()->IsMaskingPassword();
  return NS_OK;
}

NS_IMETHODIMP EditorBase::GetPasswordMask(nsAString& aPasswordMask) {
  aPasswordMask.Assign(TextEditor::PasswordMask());
  return NS_OK;
}

template <typename PT, typename CT>
EditorBase::AutoCaretBidiLevelManager::AutoCaretBidiLevelManager(
    const EditorBase& aEditorBase, nsIEditor::EDirection aDirectionAndAmount,
    const EditorDOMPointBase<PT, CT>& aPointAtCaret) {
  if (EditContext* editContext = aEditorBase.GetEditActionEditContext()) {
    InitForEditContext(aEditorBase, aDirectionAndAmount, *editContext);
  } else {
    Init(aEditorBase, aDirectionAndAmount, aPointAtCaret);
  }
}

void EditorBase::AutoCaretBidiLevelManager::InitForEditContext(
    const EditorBase& aEditorBase, nsIEditor::EDirection aDirectionAndAmount,
    const EditContext&) {
  MOZ_ASSERT(aEditorBase.GetEditActionEditContext());
  // Get bidi level from selection for EditContext. Since we only
  // do deletion as a top-level subaction for EditContext,
  // this should be okay.
  auto pointAtCaret =
      aEditorBase.GetFirstSelectionStartPoint<EditorRawDOMPoint>();
  Init(aEditorBase, aDirectionAndAmount, pointAtCaret);
}

template <typename PT, typename CT>
void EditorBase::AutoCaretBidiLevelManager::Init(
    const EditorBase& aEditorBase, nsIEditor::EDirection aDirectionAndAmount,
    const EditorDOMPointBase<PT, CT>& aPointAtCaret) {
  MOZ_ASSERT(aEditorBase.IsEditActionDataAvailable());
  nsPresContext* presContext = aEditorBase.GetPresContext();
  if (NS_WARN_IF(!presContext)) {
    mFailed = true;
    return;
  }

  if (!presContext->BidiEnabled()) {
    return;  // Perform the deletion
  }

  if (!aPointAtCaret.IsInContentNode()) {
    mFailed = true;
    return;
  }

  // XXX Not sure whether this requires strong reference here.
  RefPtr<nsFrameSelection> frameSelection =
      aEditorBase.GetEditableFrameSelection();
  if (NS_WARN_IF(!frameSelection)) {
    mFailed = true;
    return;
  }

  nsPrevNextBidiLevels levels = frameSelection->GetPrevNextBidiLevels(
      aPointAtCaret.template ContainerAs<nsIContent>(), aPointAtCaret.Offset(),
      true);

  mozilla::intl::BidiEmbeddingLevel levelBefore = levels.mLevelBefore;
  mozilla::intl::BidiEmbeddingLevel levelAfter = levels.mLevelAfter;

  mozilla::intl::BidiEmbeddingLevel currentCaretLevel =
      frameSelection->GetCaretBidiLevel();

  mozilla::intl::BidiEmbeddingLevel levelOfDeletion;
  levelOfDeletion = (nsIEditor::eNext == aDirectionAndAmount ||
                     nsIEditor::eNextWord == aDirectionAndAmount)
                        ? levelAfter
                        : levelBefore;

  if (currentCaretLevel == levelOfDeletion) {
    return;  // Perform the deletion
  }

  // Set the bidi level of the caret to that of the
  // character that will be (or would have been) deleted
  mNewCaretBidiLevel = Some(levelOfDeletion);
  mCanceled =
      !StaticPrefs::bidi_edit_delete_immediately() && levelBefore != levelAfter;
}

void EditorBase::AutoCaretBidiLevelManager::MaybeUpdateCaretBidiLevel(
    const EditorBase& aEditorBase) const {
  MOZ_ASSERT(!mFailed);
  if (mNewCaretBidiLevel.isNothing()) {
    return;
  }
  RefPtr<nsFrameSelection> frameSelection =
      aEditorBase.GetEditableFrameSelection();
  MOZ_ASSERT(frameSelection);
  frameSelection->SetCaretBidiLevelAndMaybeSchedulePaint(
      mNewCaretBidiLevel.value());
}

void EditorBase::UndefineCaretBidiLevel() const {
  MOZ_ASSERT(IsEditActionDataAvailable());

  /**
   * After inserting text the caret Bidi level must be set to the level of the
   * inserted text.This is difficult, because we cannot know what the level is
   * until after the Bidi algorithm is applied to the whole paragraph.
   *
   * So we set the caret Bidi level to UNDEFINED here, and the caret code will
   * set it correctly later
   */
  nsFrameSelection* frameSelection = GetEditableFrameSelection();
  if (frameSelection) {
    frameSelection->UndefineCaretBidiLevel();
  }
}

NS_IMETHODIMP EditorBase::GetTextLength(uint32_t* /*aCount*/) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP EditorBase::GetNewlineHandling(int32_t* aNewlineHandling) {
  if (NS_WARN_IF(!aNewlineHandling)) {
    return NS_ERROR_INVALID_ARG;
  }
  *aNewlineHandling = mNewlineHandling;
  return NS_OK;
}

NS_IMETHODIMP EditorBase::SetNewlineHandling(int32_t aNewlineHandling) {
  switch (aNewlineHandling) {
    case nsIEditor::eNewlinesPasteIntact:
    case nsIEditor::eNewlinesPasteToFirst:
    case nsIEditor::eNewlinesReplaceWithSpaces:
    case nsIEditor::eNewlinesStrip:
    case nsIEditor::eNewlinesReplaceWithCommas:
    case nsIEditor::eNewlinesStripSurroundingWhitespace:
      mNewlineHandling = aNewlineHandling;
      return NS_OK;
    default:
      NS_ERROR("SetNewlineHandling() is called with wrong value");
      return NS_ERROR_INVALID_ARG;
  }
}

bool EditorBase::IsSelectionRangeContainerNotContent() const {
  MOZ_ASSERT(IsEditActionDataAvailable());

  // TODO: Make all callers use !AutoClonedRangeArray::IsInContent() instead.
  const uint32_t rangeCount = SelectionRef().RangeCount();
  for (const uint32_t i : IntegerRange(rangeCount)) {
    MOZ_ASSERT(SelectionRef().RangeCount() == rangeCount);
    const dom::Range* range = SelectionRef().GetRangeAt(i);
    MOZ_ASSERT(range);
    if (MOZ_UNLIKELY(!range) || MOZ_UNLIKELY(!range->GetStartContainer()) ||
        MOZ_UNLIKELY(!range->GetStartContainer()->IsContent()) ||
        MOZ_UNLIKELY(!range->GetEndContainer()) ||
        MOZ_UNLIKELY(!range->GetEndContainer()->IsContent())) {
      return true;
    }
  }
  return false;
}

NS_IMETHODIMP EditorBase::InsertText(const nsAString& aStringToInsert) {
  nsresult rv = InsertTextAsAction(aStringToInsert);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "EditorBase::InsertTextAsAction() failed");
  return rv;
}

nsresult EditorBase::InsertTextAsAction(const nsAString& aStringToInsert,
                                        nsIPrincipal* aPrincipal) {
  // Showing this assertion is fine if this method is called by outside via
  // mutation event listener or something.  Otherwise, this is called by
  // wrong method.
  NS_ASSERTION(!mPlaceholderBatch,
               "Should be called only when this is the only edit action of the "
               "operation "
               "unless mutation event listener nests some operations");

  AutoEditActionDataSetter editActionData(*this, EditAction::eInsertText,
                                          aPrincipal);
  // Note that we don't need to replace native line breaks with XP line breaks
  // here because Chrome does not do it.
  MOZ_ASSERT(!aStringToInsert.IsVoid());
  editActionData.SetData(aStringToInsert);
  nsresult rv = editActionData.CanHandleAndMaybeDispatchBeforeInputEvent();
  if (NS_FAILED(rv)) {
    NS_WARNING_ASSERTION(rv == NS_ERROR_EDITOR_ACTION_CANCELED,
                         "CanHandleAndMaybeDispatchBeforeInputEvent() failed");
    return EditorBase::ToGenericNSResult(rv);
  }

  nsString stringToInsert(aStringToInsert);
  if (IsTextEditor()) {
    nsContentUtils::PlatformToDOMLineBreaks(stringToInsert);
  }
  AutoPlaceholderBatch treatAsOneTransaction(
      *this, ScrollSelectionIntoView::Yes, __FUNCTION__);
  rv = InsertTextAsSubAction(stringToInsert, InsertTextFor::NormalText);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "EditorBase::InsertTextAsSubAction() failed");
  return EditorBase::ToGenericNSResult(rv);
}

nsresult EditorBase::InsertTextAsSubAction(const nsAString& aStringToInsert,
                                           InsertTextFor aPurpose) {
  MOZ_ASSERT(IsEditActionDataAvailable());
  // For EditContext, we don't need a placeholder batch since
  // there is no undo/redo.
  MOZ_ASSERT(mPlaceholderBatch || GetEditActionEditContext());
  MOZ_ASSERT(IsHTMLEditor() ||
             aStringToInsert.FindChar(nsCRT::CR) == kNotFound);
  MOZ_ASSERT_IF(aPurpose == InsertTextFor::CompositionStart ||
                    aPurpose == InsertTextFor::CompositionUpdate ||
                    aPurpose == InsertTextFor::CompositionEnd ||
                    aPurpose == InsertTextFor::CompositionStartAndEnd,
                mComposition);

  if (NS_WARN_IF(!mInitSucceeded)) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  if (NS_WARN_IF(Destroyed())) {
    return NS_ERROR_EDITOR_DESTROYED;
  }

  EditSubAction editSubAction = ShouldHandleIMEComposition()
                                    ? EditSubAction::eInsertTextComingFromIME
                                    : EditSubAction::eInsertText;

  IgnoredErrorResult ignoredError;
  AutoEditSubActionNotifier startToHandleEditSubAction(
      *this, editSubAction, nsIEditor::eNext, ignoredError);
  if (NS_WARN_IF(ignoredError.ErrorCodeIs(NS_ERROR_EDITOR_DESTROYED))) {
    return ignoredError.StealNSResult();
  }
  NS_WARNING_ASSERTION(
      !ignoredError.Failed(),
      "TextEditor::OnStartToHandleTopLevelEditSubAction() failed, but ignored");

  Result<EditActionResult, nsresult> result =
      HandleInsertText(aStringToInsert, aPurpose);
  if (MOZ_UNLIKELY(result.isErr())) {
    NS_WARNING("EditorBase::HandleInsertText() failed");
    return result.unwrapErr();
  }
  return NS_OK;
}

NS_IMETHODIMP EditorBase::InsertLineBreak() { return NS_ERROR_NOT_IMPLEMENTED; }

/*****************************************************************************
 * mozilla::EditorBase::AutoEditActionDataSetter
 *****************************************************************************/

EditorBase::AutoEditActionDataSetter::AutoEditActionDataSetter(
    const EditorBase& aEditorBase, EditAction aEditAction,
    nsIPrincipal* aPrincipal /* = nullptr */)
    : mEditorBase(const_cast<EditorBase&>(aEditorBase)),
      mPrincipal(aPrincipal),
      mParentData(aEditorBase.mEditActionData),
      mData(VoidString()),
      mRawEditAction(aEditAction),
      mEditorWasDestroyedDuringHandlingEditAction(
          mParentData &&
          mParentData->mEditorWasDestroyedDuringHandlingEditAction),
      mEditorWasReinitialized(mParentData &&
                              mParentData->mEditorWasReinitialized) {
  // If we're nested edit action, copies necessary data from the parent.
  if (mParentData) {
    mSelection = mParentData->mSelection;
    MOZ_ASSERT(!mSelection ||
               (mSelection->GetType() == SelectionType::eNormal));
    mTextNode = mParentData->mTextNode;

    // If we're not editing something, we should inherit the parent's edit
    // action. This may occur if creator or its callee use public methods which
    // just returns something.
    if (IsEditActionInOrderToEditSomething(aEditAction)) {
      mEditAction = aEditAction;
    } else {
      mEditAction = mParentData->mEditAction;
      // If we inherit an edit action whose handler needs to dispatch a
      // clipboard event, we should inherit the clipboard dispatching state
      // too because this nest occurs by a clipboard event listener or
      // a beforeinput/mutation event listener is important for checking
      // whether we've already called `MaybeDispatchBeforeInputEvent()`
      // property in some points.  If the former case, not yet dispatching
      // beforeinput event is okay (not fine).
      mHasTriedToDispatchClipboardEvent =
          mParentData->mHasTriedToDispatchClipboardEvent;
    }
    mTopLevelEditSubAction = mParentData->mTopLevelEditSubAction;

    // Parent's mTopLevelEditSubActionData should be referred instead so that
    // we don't need to set mTopLevelEditSubActionData.mSelectedRange nor
    // mTopLevelEditActionData.mChangedRange here.

    mDirectionOfTopLevelEditSubAction =
        mParentData->mDirectionOfTopLevelEditSubAction;
  } else {
    mSelection = mEditorBase.GetSelection();
    if (NS_WARN_IF(!mSelection)) {
      return;
    }
    // Although we shouldn't have had the cached Text yet because we're the
    // topmost instance of AutoEditActionDataSetter and we'll register this to
    // aEditorBase below.  However, for clarifying, let's explicitly ignore the
    // cached Text.  Additionally, this may be called for initializing
    // aEditorBase too.  Therefore, we need to avoid the assertions in
    // GetTextNode() so that we need to check whether the editor is initialized.
    mTextNode = mEditorBase.IsTextEditor() && mEditorBase.mInitSucceeded
                    ? mEditorBase.AsTextEditor()->GetTextNode(
                          TextEditor::IgnoreTextNodeCache::Yes)
                    : nullptr;

    MOZ_ASSERT(mSelection->GetType() == SelectionType::eNormal);

    mEditAction = aEditAction;
    mDirectionOfTopLevelEditSubAction = eNone;
    if (mEditorBase.IsHTMLEditor()) {
      mTopLevelEditSubActionData.mSelectedRange =
          mEditorBase.AsHTMLEditor()
              ->GetSelectedRangeItemForTopLevelEditSubAction();
      mTopLevelEditSubActionData.mChangedRange =
          mEditorBase.AsHTMLEditor()->GetChangedRangeForTopLevelEditSubAction();
      mTopLevelEditSubActionData.mCachedPendingStyles.emplace();
    }
  }

  if (aEditAction != EditAction::eNone &&
      aEditAction != EditAction::eNotEditing &&
      aEditAction != EditAction::eInitializing) {
    mEditContext = aEditorBase.ComputeEditContext();
  }

  mEditorBase.mEditActionData = this;

  if (aEditorBase.IsHTMLEditor() &&
      MOZ_LOG_TEST(gHTMLEditorEditActionStartLog, LogLevel::Info) &&
      aEditAction != EditAction::eNone &&
      aEditAction != EditAction::eNotEditing &&
      aEditAction != EditAction::eInitializing) {
    const HTMLEditor& htmlEditor = *aEditorBase.AsHTMLEditor();
    Element* const editingHost =
        htmlEditor.ComputeEditingHost(HTMLEditor::LimitInBodyElement::No);
    nsAutoString innerHTML;
    if (editingHost) {
      editingHost->GetInnerHTML(innerHTML, IgnoreErrors());
      innerHTML.ReplaceSubstring(u"\n", u"\\n");
      innerHTML.ReplaceSubstring(u"\r", u"\\r");
      innerHTML.ReplaceSubstring(u"\t", u"\\t");
      innerHTML.ReplaceSubstring(u"\f", u"\\f");
      innerHTML.ReplaceSubstring(u"\u00A0", u"&nbsp;");
    }
    MOZ_ASSERT(mSelection);
    MOZ_LOG(
        gHTMLEditorEditActionStartLog, LogLevel::Info,
        ("%s\nediting host: %s\ninnerHTML: \"%s\"\nselection range "
         "count: %u",
         ToString(aEditAction).c_str(), ToString(RefPtr{editingHost}).c_str(),
         NS_ConvertUTF16toUTF8(innerHTML).get(), mSelection->RangeCount()));
    for (const uint32_t index : IntegerRange(mSelection->RangeCount())) {
      dom::Range* const range = mSelection->GetRangeAt(index);
      MOZ_ASSERT(range);
      EditorRawDOMRange editorRange(*range);
      MOZ_LOG(gHTMLEditorEditActionStartLog, LogLevel::Info,
              ("getRangeAt(%u): %s", index, ToString(editorRange).c_str()));
    }
  }
}

EditorBase::AutoEditActionDataSetter::~AutoEditActionDataSetter() {
  MOZ_ASSERT(mHasCanHandleChecked);

  if (!mSelection || NS_WARN_IF(mEditorBase.mEditActionData != this)) {
    return;
  }
  mEditorBase.mEditActionData = mParentData;

  MOZ_ASSERT(
      !mTopLevelEditSubActionData.mSelectedRange ||
          (!mTopLevelEditSubActionData.mSelectedRange->mStartContainer &&
           !mTopLevelEditSubActionData.mSelectedRange->mEndContainer),
      "mTopLevelEditSubActionData.mSelectedRange should've been cleared");
}

void EditorBase::AutoEditActionDataSetter::OnEditorInitialized() {
  if (mEditorWasDestroyedDuringHandlingEditAction) {
    mEditorWasReinitialized = true;
  }
  if (mEditorBase.IsTextEditor()) {
    mTextNode = mEditorBase.AsTextEditor()->GetTextNode(
        TextEditor::IgnoreTextNodeCache::Yes);
  }
  if (mParentData) {
    mParentData->OnEditorInitialized();
  }
}

void EditorBase::AutoEditActionDataSetter::UpdateSelectionCache(
    Selection& aSelection) {
  MOZ_ASSERT(aSelection.GetType() == SelectionType::eNormal);

  if (mSelection == &aSelection) {
    return;
  }

  AutoEditActionDataSetter& topLevelEditActionData =
      [&]() -> AutoEditActionDataSetter& {
    for (AutoEditActionDataSetter* editActionData = this;;
         editActionData = editActionData->mParentData) {
      if (!editActionData->mParentData) {
        return *editActionData;
      }
    }
    MOZ_ASSERT_UNREACHABLE("You do something wrong");
  }();

  RefPtr<Selection> previousSelection = mSelection;

  // Keep grabbing the old selection in the top level edit action data until the
  // all owners end handling it.
  if (previousSelection) {
    topLevelEditActionData.mRetiredSelections.AppendElement(*previousSelection);
  }

  // If the old selection is in batch, we should end the batch which
  // `EditorBase::BeginUpdateViewBatch` started.
  if (mEditorBase.mUpdateCount && previousSelection) {
    previousSelection->EndBatchChanges(__FUNCTION__);
  }

  mSelection = &aSelection;
  for (AutoEditActionDataSetter* parentActionData = mParentData;
       parentActionData; parentActionData = parentActionData->mParentData) {
    if (!parentActionData->mSelection) {
      continue;
    }
    // Skip scanning mRetiredSelections if we've already handled the selection
    // previous time.
    if (parentActionData->mSelection != previousSelection) {
      if (!topLevelEditActionData.mRetiredSelections.Contains(
              OwningNonNull<Selection>(*parentActionData->mSelection))) {
        topLevelEditActionData.mRetiredSelections.AppendElement(
            *parentActionData->mSelection);
      }
      previousSelection = parentActionData->mSelection;
    }
    parentActionData->mSelection = &aSelection;
  }

  // Restart the batching in the new selection.
  if (mEditorBase.mUpdateCount) {
    aSelection.StartBatchChanges(__FUNCTION__);
  }
}

LimitersAndCaretData
EditorBase::AutoEditActionDataSetter::SelectionLimitersAndCaretData() const {
  if (nsFrameSelection* const frameSelection =
          SelectionRef().GetFrameSelection()) {
    return LimitersAndCaretData{*frameSelection};
  }
  return {};
}

void EditorBase::AutoEditActionDataSetter::UpdateEditContext() {
  MOZ_ASSERT(!mHasTriedToDispatchBeforeInputEvent,
             "It's too late to update EditContext since this may have "
             "already dispatched a beforeinput event");
  mEditContext = mEditorBase.ComputeEditContext();
}

void EditorBase::AutoEditActionDataSetter::SetColorData(
    const nsAString& aData) {
  MOZ_ASSERT(!HasTriedToDispatchBeforeInputEvent(),
             "It's too late to set data since this may have already dispatched "
             "a beforeinput event");

  if (aData.IsEmpty()) {
    // When removing color/background-color, let's use empty string.
    mData.Truncate();
    MOZ_ASSERT(!mData.IsVoid());
    return;
  }

  DebugOnly<bool> validColorValue = HTMLEditUtils::GetNormalizedCSSColorValue(
      aData, HTMLEditUtils::ZeroAlphaColor::RGBAValue, mData);
  MOZ_ASSERT_IF(validColorValue, !mData.IsVoid());
}

void EditorBase::AutoEditActionDataSetter::InitializeDataTransfer(
    DataTransfer* aDataTransfer) {
  MOZ_ASSERT(aDataTransfer);
  MOZ_ASSERT(aDataTransfer->IsReadOnly());
  MOZ_ASSERT(!HasTriedToDispatchBeforeInputEvent(),
             "It's too late to set dataTransfer since this may have already "
             "dispatched a beforeinput event");

  mDataTransfer = aDataTransfer;
}

void EditorBase::AutoEditActionDataSetter::InitializeDataTransfer(
    nsITransferable* aTransferable) {
  MOZ_ASSERT(aTransferable);
  MOZ_ASSERT(!HasTriedToDispatchBeforeInputEvent(),
             "It's too late to set dataTransfer since this may have already "
             "dispatched a beforeinput event");

  Document* document = mEditorBase.GetDocument();
  nsIGlobalObject* scopeObject =
      document ? document->GetScopeObject() : nullptr;
  mDataTransfer = new DataTransfer(scopeObject, eEditorInput, aTransferable);
}

void EditorBase::AutoEditActionDataSetter::InitializeDataTransfer(
    const nsAString& aString) {
  MOZ_ASSERT(!HasTriedToDispatchBeforeInputEvent(),
             "It's too late to set dataTransfer since this may have already "
             "dispatched a beforeinput event");
  Document* document = mEditorBase.GetDocument();
  nsIGlobalObject* scopeObject =
      document ? document->GetScopeObject() : nullptr;
  mDataTransfer = new DataTransfer(scopeObject, eEditorInput, aString);
}

void EditorBase::AutoEditActionDataSetter::InitializeDataTransferWithClipboard(
    SettingDataTransfer aSettingDataTransfer, DataTransfer* aDataTransfer,
    nsIClipboard::ClipboardType aClipboardType) {
  MOZ_ASSERT(!HasTriedToDispatchBeforeInputEvent(),
             "It's too late to set dataTransfer since this may have already "
             "dispatched a beforeinput event");

  Document* document = mEditorBase.GetDocument();
  nsIGlobalObject* scopeObject =
      document ? document->GetScopeObject() : nullptr;
  // mDataTransfer will be used for eEditorInput event, but we can keep
  // using ePaste and ePasteNoFormatting here.  If we need to use eEditorInput,
  // we need to create eEditorInputNoFormatting or something...
  EventMessage message =
      (aSettingDataTransfer == SettingDataTransfer::eWithFormat)
          ? ePaste
          : ePasteNoFormatting;
  if (aDataTransfer) {
    // The DataTransfer being passed in will be used in a paste event, which
    // means it will be cleared after that event is done firing. We don't want
    // that for "input" and "beforeinput" events, so make a copy of its data.
    aDataTransfer->Clone(scopeObject, message,
                         /* aUserCancelled = */ false,
                         /* aIsCrossDomainSubFrameDrop = */ false,
                         getter_AddRefs(mDataTransfer));
  } else {
    mDataTransfer = MakeRefPtr<DataTransfer>(
        scopeObject, message, true /* is external */, Some(aClipboardType));
  }
}

void EditorBase::AutoEditActionDataSetter::AppendTargetRange(
    StaticRange& aTargetRange) {
  mTargetRanges.AppendElement(aTargetRange);
}

void EditorBase::AutoEditActionDataSetter::AppendTargetRange(
    RefPtr<StaticRange>&& aTargetRange) {
  mTargetRanges.AppendElement(std::move(aTargetRange));
}

bool EditorBase::AutoEditActionDataSetter::IsBeforeInputEventEnabled() const {
  // Don't dispatch "beforeinput" event when the editor user makes us stop
  // dispatching input event.
  if (mEditorBase.IsSuppressingDispatchingInputEvent()) {
    return false;
  }
  return EditorBase::TreatAsUserInput(mPrincipal);
}

// static
bool EditorBase::TreatAsUserInput(nsIPrincipal* aPrincipal) {
  // If aPrincipal it not nullptr, it means that the caller is handling an edit
  // action which is requested by JS.  If it's not chrome script, we shouldn't
  // dispatch "beforeinput" event.
  if (aPrincipal && !aPrincipal->IsSystemPrincipal()) {
    // But if it's content script of an addon, `execCommand` calls are a
    // part of browser's default action from point of view of web apps.
    // Therefore, we should dispatch `beforeinput` event.
    // https://github.com/w3c/input-events/issues/91
    if (!aPrincipal->GetIsAddonOrExpandedAddonPrincipal()) {
      return false;
    }
  }

  return true;
}

nsresult EditorBase::AutoEditActionDataSetter::MaybeFlushPendingNotifications()
    const {
  MOZ_ASSERT(CanHandle());
  if (!MayEditActionRequireLayout(mRawEditAction)) {
    return NS_SUCCESS_DOM_NO_OPERATION;
  }
  OwningNonNull<EditorBase> editorBase = mEditorBase;
  RefPtr<PresShell> presShell = editorBase->GetPresShell();
  if (MOZ_UNLIKELY(NS_WARN_IF(!presShell))) {
    return NS_ERROR_NOT_AVAILABLE;
  }
  presShell->FlushPendingNotifications(FlushType::Layout);
  if (MOZ_UNLIKELY(NS_WARN_IF(editorBase->Destroyed()))) {
    return NS_ERROR_EDITOR_DESTROYED;
  }
  return NS_OK;
}

void EditorBase::AutoEditActionDataSetter::MarkEditActionCanceled() {
  mBeforeInputEventCanceled = true;
}

nsresult EditorBase::AutoEditActionDataSetter::MaybeDispatchBeforeInputEvent(
    nsIEditor::EDirection aDeleteDirectionAndAmount /* = nsIEditor::eNone */) {
  MOZ_ASSERT(!HasTriedToDispatchBeforeInputEvent(),
             "We've already handled beforeinput event");
  MOZ_ASSERT(CanHandle());
  MOZ_ASSERT_IF(IsBeforeInputEventEnabled(),
                ShouldAlreadyHaveHandledBeforeInputEventDispatching());
  MOZ_ASSERT_IF(!MayEditActionDeleteAroundCollapsedSelection(mEditAction),
                aDeleteDirectionAndAmount == nsIEditor::eNone);

  mHasTriedToDispatchBeforeInputEvent = true;

  if (!IsBeforeInputEventEnabled()) {
    return NS_OK;
  }

  if (mEditorBase.IsHTMLEditor()) {
    mEditorBase.AsHTMLEditor()->mLastCollapsibleWhiteSpaceAppendedTextNode =
        nullptr;
  }

  // If we're called from OnCompositionEnd(), we shouldn't dispatch
  // "beforeinput" event since the preceding OnCompositionChange() call has
  // already dispatched "beforeinput" event for this.
  if (mEditAction == EditAction::eCommitComposition ||
      mEditAction == EditAction::eCancelComposition) {
    return NS_OK;
  }

  RefPtr<Element> targetElement = mEditorBase.GetInputEventTargetElement();
  if (!targetElement) {
    // If selection is not in editable element and it is outside of any
    // editing hosts, there may be no target element to dispatch `beforeinput`
    // event.  In this case, the caller shouldn't keep handling the edit
    // action since web apps cannot override it with `beforeinput` event
    // listener, but for backward compatibility, we should return a special
    // success code instead of error.
    MOZ_LOG(gEventLog, LogLevel::Error,
            ("%p %s: Failed dispatching \"beforeinput\" event due to no target",
             &mEditorBase,
             mEditorBase.mIsHTMLEditorClass ? "HTMLEditor" : "TextEditor"));
    return NS_OK;
  }
  OwningNonNull<EditorBase> editorBase = mEditorBase;
  EditorInputType inputType = ToInputType(mEditAction);
  if (targetElement->HasFlag(ELEMENT_HAS_EDIT_CONTEXT) &&
      (targetElement->IsHTMLElement(nsGkAtoms::canvas) ||
       mEditAction == EditAction::eUpdateComposition ||
       mEditAction == EditAction::eUpdateCompositionToCommit)) {
    // targetRanges should be empty for <canvas>-based EditContext.
    // Also probably for composition in DOM-based EditContext
    // as well: https://github.com/w3c/edit-context/issues/133
    mTargetRanges.Clear();
  } else if (editorBase->IsHTMLEditor() && mTargetRanges.IsEmpty()) {
    // If the edit action will delete selected ranges, compute the range
    // strictly.
    if (MayEditActionDeleteAroundCollapsedSelection(mEditAction) ||
        (!editorBase->SelectionRef().IsCollapsed() &&
         MayEditActionDeleteSelection(mEditAction))) {
      if (!editorBase
               ->FlushPendingNotificationsIfToHandleDeletionWithFrameSelection(
                   aDeleteDirectionAndAmount)) {
        NS_WARNING(
            "Flusing pending notifications caused destroying the editor");
        return NS_ERROR_EDITOR_DESTROYED;
      }

      AutoClonedSelectionRangeArray rangesToDelete(editorBase->SelectionRef());
      if (!rangesToDelete.Ranges().IsEmpty()) {
        nsresult rv = MOZ_KnownLive(editorBase->AsHTMLEditor())
                          ->ComputeTargetRanges(aDeleteDirectionAndAmount,
                                                rangesToDelete);
        if (rv == NS_ERROR_EDITOR_DESTROYED) {
          NS_WARNING("HTMLEditor::ComputeTargetRanges() destroyed the editor");
          return NS_ERROR_EDITOR_DESTROYED;
        }
        if (rv == NS_ERROR_EDITOR_NO_EDITABLE_RANGE) {
          // For now, keep dispatching `beforeinput` event even if no selection
          // range can be editable.
          rv = NS_OK;
        }
        NS_WARNING_ASSERTION(
            NS_SUCCEEDED(rv),
            "HTMLEditor::ComputeTargetRanges() failed, but ignored");
        for (auto& range : rangesToDelete.Ranges()) {
          RefPtr<StaticRange> staticRange =
              StaticRange::Create(range, IgnoreErrors());
          if (NS_WARN_IF(!staticRange)) {
            continue;
          }
          AppendTargetRange(*staticRange);
        }
      }
    }
    // Otherwise, just set target ranges to selection ranges.
    else if (MayHaveTargetRangesOnHTMLEditor(inputType)) {
      if (uint32_t rangeCount = editorBase->SelectionRef().RangeCount()) {
        mTargetRanges.SetCapacity(rangeCount);
        for (const uint32_t i : IntegerRange(rangeCount)) {
          MOZ_ASSERT(editorBase->SelectionRef().RangeCount() == rangeCount);
          const dom::Range* range = editorBase->SelectionRef().GetRangeAt(i);
          MOZ_ASSERT(range);
          MOZ_ASSERT(range->IsPositioned());
          if (MOZ_UNLIKELY(NS_WARN_IF(!range)) ||
              MOZ_UNLIKELY(NS_WARN_IF(!range->IsPositioned()))) {
            continue;
          }
          // Now, we need to fix the offset of target range because it may
          // be referred after modifying the DOM tree and range boundaries
          // of `range` may have not computed offset yet.
          RefPtr<StaticRange> targetRange = StaticRange::Create(
              range->GetStartContainer(), range->StartOffset(),
              range->GetEndContainer(), range->EndOffset(), IgnoreErrors());
          if (NS_WARN_IF(!targetRange) ||
              NS_WARN_IF(!targetRange->IsPositioned())) {
            continue;
          }
          mTargetRanges.AppendElement(std::move(targetRange));
        }
      }
    }
  }
  nsEventStatus status = nsEventStatus_eIgnore;
  InputEventOptions::NeverCancelable neverCancelable =
      mMakeBeforeInputEventNonCancelable
          ? InputEventOptions::NeverCancelable::Yes
          : InputEventOptions::NeverCancelable::No;
  WillDispatchInputEvent();
  MOZ_LOG(gEventLog, LogLevel::Info,
          ("%p %s: Dispatching \"beforeinput\" event: { inputType=\"%s\" }...",
           editorBase.get(),
           editorBase->mIsHTMLEditorClass ? "HTMLEditor" : "TextEditor",
           ToString(ToInputType(GetEditAction())).c_str()));
  nsresult rv = nsContentUtils::DispatchInputEvent(
      targetElement, eEditorBeforeInput, inputType, editorBase,
      mDataTransfer
          ? InputEventOptions(mDataTransfer, std::move(mTargetRanges),
                              neverCancelable)
          : InputEventOptions(mData, std::move(mTargetRanges), neverCancelable),
      &status);
  MOZ_LOG(gEventLog, LogLevel::Info,
          ("%p %s: Dispatched \"beforeinput\" event: { inputType=\"%s\" }, "
           "defaultPrevented=%s",
           editorBase.get(),
           editorBase->mIsHTMLEditorClass ? "HTMLEditor" : "TextEditor",
           ToString(ToInputType(GetEditAction())).c_str(),
           status == nsEventStatus_eConsumeNoDefault ? "true" : "false"));
  DidDispatchInputEvent();
  if (NS_WARN_IF(mEditorBase.Destroyed())) {
    return NS_ERROR_EDITOR_DESTROYED;
  }
  if (NS_FAILED(rv)) {
    NS_WARNING("nsContentUtils::DispatchInputEvent() failed");
    return rv;
  }
  if (status == nsEventStatus_eConsumeNoDefault) {
    MarkEditActionCanceled();
    return NS_ERROR_EDITOR_ACTION_CANCELED;
  }

  nsCOMPtr<nsIWidget> widget = editorBase->GetWidget();
  if (!StaticPrefs::dom_events_textevent_enabled() ||
      !targetElement->IsInComposedDoc() || !widget) {
    return NS_OK;
  }
  nsString textInputData;
  RefPtr<DataTransfer> textInputDataTransfer;
  switch (inputType) {
    case EditorInputType::eInsertCompositionText:
      // If the composition is still being composed, we should not dispatch
      // textInput event, but we need to dispatch it for the last composition
      // change because web apps should know the inserting commit string as
      // same as input from keyboard.
      if (mEditAction == EditAction::eUpdateComposition) {
        return NS_OK;
      }
      [[fallthrough]];
    case EditorInputType::eInsertText:
      textInputData = mData;
      break;
    case EditorInputType::eInsertFromDrop:
    case EditorInputType::eInsertFromPaste:
    case EditorInputType::eInsertFromPasteAsQuotation:
      if (mDataTransfer) {
        textInputDataTransfer = mDataTransfer;
      } else {
        textInputData = mData;
      }
      break;
    case EditorInputType::eInsertLineBreak:
    case EditorInputType::eInsertParagraph:
      // Don't dispatch `textInput` on <input> because Chrome does not do it.
      // On the other hand, we need to dispatch it on <textarea> and
      // contenteditable.
      if (mEditorBase.IsTextEditor() && mEditorBase.IsSingleLineEditor()) {
        return NS_OK;
      }
      textInputData.Assign(u'\n');
      break;
    default:
      return NS_OK;
  }

  InternalLegacyTextEvent textEvent(true, eLegacyTextInput, widget);
  textEvent.mData = std::move(textInputData);
  textEvent.mDataTransfer = std::move(textInputDataTransfer);
  textEvent.mInputType = inputType;
  // Make it always cancelable even though we ignore it when inserting or
  // deleting composition.  This is compatible with Chrome.
  // However, if and only if it's unsafe, let's set it not cancelable because of
  // asynchronous dispatching.
  textEvent.mFlags.mCancelable = nsContentUtils::IsSafeToRunScript();

  status = nsEventStatus_eIgnore;
  rv = AsyncEventDispatcher::RunDOMEventWhenSafe(*targetElement, textEvent,
                                                 &status);
  if (NS_WARN_IF(mEditorBase.Destroyed())) {
    return NS_ERROR_EDITOR_DESTROYED;
  }
  if (NS_FAILED(rv)) {
    NS_WARNING("AsyncEventDispatcher::RunDOMEventWhenSafe() failed");
    return rv;
  }
  if (status == nsEventStatus_eConsumeNoDefault) {
    MarkEditActionCanceled();
    return NS_ERROR_EDITOR_ACTION_CANCELED;
  }
  return NS_OK;
}

/*****************************************************************************
 * mozilla::EditorBase::TopLevelEditSubActionData
 *****************************************************************************/

nsresult EditorBase::TopLevelEditSubActionData::AddNodeToChangedRange(
    const HTMLEditor& aHTMLEditor, nsINode& aNode) {
  EditorRawDOMPoint startPoint(&aNode);
  EditorRawDOMPoint endPoint(&aNode);
  DebugOnly<bool> advanced = endPoint.AdvanceOffset();
  NS_WARNING_ASSERTION(advanced, "Failed to set endPoint to next to aNode");
  nsresult rv = AddRangeToChangedRange(aHTMLEditor, startPoint, endPoint);
  NS_WARNING_ASSERTION(
      NS_SUCCEEDED(rv),
      "TopLevelEditSubActionData::AddRangeToChangedRange() failed");
  return rv;
}

nsresult EditorBase::TopLevelEditSubActionData::AddPointToChangedRange(
    const HTMLEditor& aHTMLEditor, const EditorRawDOMPoint& aPoint) {
  nsresult rv = AddRangeToChangedRange(aHTMLEditor, aPoint, aPoint);
  NS_WARNING_ASSERTION(
      NS_SUCCEEDED(rv),
      "TopLevelEditSubActionData::AddRangeToChangedRange() failed");
  return rv;
}

nsresult EditorBase::TopLevelEditSubActionData::AddRangeToChangedRange(
    const HTMLEditor& aHTMLEditor, const EditorRawDOMPoint& aStart,
    const EditorRawDOMPoint& aEnd) {
  if (NS_WARN_IF(!aStart.IsSet()) || NS_WARN_IF(!aEnd.IsSet())) {
    return NS_ERROR_INVALID_ARG;
  }

  if (!aHTMLEditor.IsDescendantOfRoot(aStart.GetContainer()) ||
      (aStart.GetContainer() != aEnd.GetContainer() &&
       !aHTMLEditor.IsDescendantOfRoot(aEnd.GetContainer()))) {
    return NS_OK;
  }

  // If mChangedRange hasn't been set, we can just set it to `aStart` and
  // `aEnd`.
  if (!mChangedRange->IsPositioned()) {
    nsresult rv = mChangedRange->SetStartAndEnd(aStart.ToRawRangeBoundary(),
                                                aEnd.ToRawRangeBoundary());
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rv), "Range::SetStartAndEnd() failed");
    return rv;
  }

  // XXX Should use TreeKind::DOM? Editable state won't cross shadow DOM
  // boundaries.
  Maybe<int32_t> relation =
      mChangedRange->StartRef().IsSet()
          ? nsContentUtils::ComparePoints<TreeKind::ShadowIncludingDOM>(
                mChangedRange->StartRef(), aStart.ToRawRangeBoundary())
          : Some(1);
  if (NS_WARN_IF(!relation)) {
    return NS_ERROR_FAILURE;
  }

  // If aStart is before start of mChangedRange, reset the start.
  if (*relation > 0) {
    ErrorResult error;
    mChangedRange->SetStart(aStart.ToRawRangeBoundary(), error);
    if (error.Failed()) {
      NS_WARNING("Range::SetStart() failed");
      return error.StealNSResult();
    }
  }

  relation = mChangedRange->EndRef().IsSet()
                 ? nsContentUtils::ComparePoints<TreeKind::ShadowIncludingDOM>(
                       mChangedRange->EndRef(), aEnd.ToRawRangeBoundary())
                 : Some(1);
  if (NS_WARN_IF(!relation)) {
    return NS_ERROR_FAILURE;
  }

  // If aEnd is after end of mChangedRange, reset the end.
  if (*relation < 0) {
    ErrorResult error;
    mChangedRange->SetEnd(aEnd.ToRawRangeBoundary(), error);
    if (error.Failed()) {
      NS_WARNING("Range::SetEnd() failed");
      return error.StealNSResult();
    }
  }

  return NS_OK;
}

void EditorBase::TopLevelEditSubActionData::DidCreateElement(
    EditorBase& aEditorBase, Element& aNewElement) {
  MOZ_ASSERT(aEditorBase.AsHTMLEditor());

  if (!aEditorBase.mInitSucceeded || aEditorBase.Destroyed()) {
    return;  // We have not been initialized yet or already been destroyed.
  }

  if (!aEditorBase.EditSubActionDataRef().mAdjustChangedRangeFromListener) {
    return;  // Temporarily disabled by edit sub-action handler.
  }

  DebugOnly<nsresult> rvIgnored =
      AddNodeToChangedRange(*aEditorBase.AsHTMLEditor(), aNewElement);
  NS_WARNING_ASSERTION(
      NS_SUCCEEDED(rvIgnored),
      "TopLevelEditSubActionData::AddNodeToChangedRange() failed, but ignored");
}

void EditorBase::TopLevelEditSubActionData::DidInsertContent(
    EditorBase& aEditorBase, nsIContent& aNewContent) {
  MOZ_ASSERT(aEditorBase.AsHTMLEditor());

  if (!aEditorBase.mInitSucceeded || aEditorBase.Destroyed()) {
    return;  // We have not been initialized yet or already been destroyed.
  }

  if (!aEditorBase.EditSubActionDataRef().mAdjustChangedRangeFromListener) {
    return;  // Temporarily disabled by edit sub-action handler.
  }

  DebugOnly<nsresult> rvIgnored =
      AddNodeToChangedRange(*aEditorBase.AsHTMLEditor(), aNewContent);
  NS_WARNING_ASSERTION(
      NS_SUCCEEDED(rvIgnored),
      "TopLevelEditSubActionData::AddNodeToChangedRange() failed, but ignored");
}

void EditorBase::TopLevelEditSubActionData::WillDeleteContent(
    EditorBase& aEditorBase, nsIContent& aRemovingContent) {
  MOZ_ASSERT(aEditorBase.AsHTMLEditor());

  if (!aEditorBase.mInitSucceeded || aEditorBase.Destroyed()) {
    return;  // We have not been initialized yet or already been destroyed.
  }

  if (!aEditorBase.EditSubActionDataRef().mAdjustChangedRangeFromListener) {
    return;  // Temporarily disabled by edit sub-action handler.
  }

  DebugOnly<nsresult> rvIgnored =
      AddNodeToChangedRange(*aEditorBase.AsHTMLEditor(), aRemovingContent);
  NS_WARNING_ASSERTION(
      NS_SUCCEEDED(rvIgnored),
      "TopLevelEditSubActionData::AddNodeToChangedRange() failed, but ignored");
}

void EditorBase::TopLevelEditSubActionData::DidSplitContent(
    EditorBase& aEditorBase, nsIContent& aSplitContent,
    nsIContent& aNewContent) {
  MOZ_ASSERT(aEditorBase.AsHTMLEditor());

  if (!aEditorBase.mInitSucceeded || aEditorBase.Destroyed()) {
    return;  // We have not been initialized yet or already been destroyed.
  }

  if (!aEditorBase.EditSubActionDataRef().mAdjustChangedRangeFromListener) {
    return;  // Temporarily disabled by edit sub-action handler.
  }

  DebugOnly<nsresult> rvIgnored = AddRangeToChangedRange(
      *aEditorBase.AsHTMLEditor(), EditorRawDOMPoint::AtEndOf(aSplitContent),
      EditorRawDOMPoint::AtEndOf(aNewContent));
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rvIgnored),
                       "TopLevelEditSubActionData::AddRangeToChangedRange() "
                       "failed, but ignored");
}

void EditorBase::TopLevelEditSubActionData::DidJoinContents(
    EditorBase& aEditorBase, const EditorRawDOMPoint& aJoinedPoint) {
  MOZ_ASSERT(aEditorBase.AsHTMLEditor());

  if (!aEditorBase.mInitSucceeded || aEditorBase.Destroyed()) {
    return;  // We have not been initialized yet or already been destroyed.
  }

  if (!aEditorBase.EditSubActionDataRef().mAdjustChangedRangeFromListener) {
    return;  // Temporarily disabled by edit sub-action handler.
  }

  DebugOnly<nsresult> rvIgnored =
      AddPointToChangedRange(*aEditorBase.AsHTMLEditor(), aJoinedPoint);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rvIgnored),
                       "TopLevelEditSubActionData::AddPointToChangedRange() "
                       "failed, but ignored");
}

void EditorBase::TopLevelEditSubActionData::DidInsertText(
    EditorBase& aEditorBase, const EditorRawDOMPoint& aInsertionBegin,
    const EditorRawDOMPoint& aInsertionEnd) {
  MOZ_ASSERT(aEditorBase.AsHTMLEditor());

  if (!aEditorBase.mInitSucceeded || aEditorBase.Destroyed()) {
    return;  // We have not been initialized yet or already been destroyed.
  }

  if (!aEditorBase.EditSubActionDataRef().mAdjustChangedRangeFromListener) {
    return;  // Temporarily disabled by edit sub-action handler.
  }

  DebugOnly<nsresult> rvIgnored = AddRangeToChangedRange(
      *aEditorBase.AsHTMLEditor(), aInsertionBegin, aInsertionEnd);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rvIgnored),
                       "TopLevelEditSubActionData::AddRangeToChangedRange() "
                       "failed, but ignored");
}

void EditorBase::TopLevelEditSubActionData::DidDeleteText(
    EditorBase& aEditorBase, const EditorRawDOMPoint& aStartInTextNode) {
  MOZ_ASSERT(aEditorBase.AsHTMLEditor());

  if (!aEditorBase.mInitSucceeded || aEditorBase.Destroyed()) {
    return;  // We have not been initialized yet or already been destroyed.
  }

  if (!aEditorBase.EditSubActionDataRef().mAdjustChangedRangeFromListener) {
    return;  // Temporarily disabled by edit sub-action handler.
  }

  DebugOnly<nsresult> rvIgnored =
      AddPointToChangedRange(*aEditorBase.AsHTMLEditor(), aStartInTextNode);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rvIgnored),
                       "TopLevelEditSubActionData::AddPointToChangedRange() "
                       "failed, but ignored");
}

void EditorBase::TopLevelEditSubActionData::WillDeleteRange(
    EditorBase& aEditorBase, const EditorRawDOMPoint& aStart,
    const EditorRawDOMPoint& aEnd) {
  MOZ_ASSERT(aEditorBase.AsHTMLEditor());
  MOZ_ASSERT(aStart.IsSet());
  MOZ_ASSERT(aEnd.IsSet());

  if (!aEditorBase.mInitSucceeded || aEditorBase.Destroyed()) {
    return;  // We have not been initialized yet or already been destroyed.
  }

  if (!aEditorBase.EditSubActionDataRef().mAdjustChangedRangeFromListener) {
    return;  // Temporarily disabled by edit sub-action handler.
  }

  // XXX Looks like that this is wrong.  We delete multiple selection ranges
  //     once, but this adds only first range into the changed range.
  //     Anyway, we should take the range as an argument.
  DebugOnly<nsresult> rvIgnored =
      AddRangeToChangedRange(*aEditorBase.AsHTMLEditor(), aStart, aEnd);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rvIgnored),
                       "TopLevelEditSubActionData::AddRangeToChangedRange() "
                       "failed, but ignored");
}

nsPIDOMWindowOuter* EditorBase::GetWindow() const {
  return mDocument ? mDocument->GetWindow() : nullptr;
}

nsPIDOMWindowInner* EditorBase::GetInnerWindow() const {
  return mDocument ? mDocument->GetInnerWindow() : nullptr;
}

PresShell* EditorBase::GetPresShell() const {
  return mDocument ? mDocument->GetPresShell() : nullptr;
}

nsPresContext* EditorBase::GetPresContext() const {
  PresShell* presShell = GetPresShell();
  return presShell ? presShell->GetPresContext() : nullptr;
}

already_AddRefed<nsCaret> EditorBase::GetCaretForSelection() const {
  PresShell* presShell = GetPresShell();
  if (NS_WARN_IF(!presShell)) {
    return nullptr;
  }
  return presShell->GetOriginalCaret();
}

nsISelectionController* EditorBase::GetSelectionController() const {
  if (mSelectionController) {
    return mSelectionController;
  }
  if (!mDocument) {
    return nullptr;
  }
  return mDocument->GetPresShell();
}

bool EditorBase::ArePreservingSelection() const {
  return IsEditActionDataAvailable() && SavedSelectionRef().RangeCount();
}

void EditorBase::PreserveSelectionAcrossActions() {
  MOZ_ASSERT(IsEditActionDataAvailable());

  SavedSelectionRef().SaveSelection(SelectionRef());
  RangeUpdaterRef().RegisterSelectionState(SavedSelectionRef());
}

nsresult EditorBase::RestorePreservedSelection() {
  MOZ_ASSERT(IsEditActionDataAvailable());

  if (!SavedSelectionRef().RangeCount()) {
    // XXX Returning error when it does not store is odd because no selection
    //     ranges is not illegal case in general.
    return NS_ERROR_FAILURE;
  }
  DebugOnly<nsresult> rvIgnored =
      SavedSelectionRef().RestoreSelection(SelectionRef());
  NS_WARNING_ASSERTION(
      NS_SUCCEEDED(rvIgnored),
      "SelectionState::RestoreSelection() failed, but ignored");
  StopPreservingSelection();
  return NS_OK;
}

void EditorBase::StopPreservingSelection() {
  MOZ_ASSERT(IsEditActionDataAvailable());

  RangeUpdaterRef().DropSelectionState(SavedSelectionRef());
  SavedSelectionRef().RemoveAllRanges();
}

nsresult EditorBase::GetDataFromDataTransferOrClipboard(
    DataTransfer* aDataTransfer, nsITransferable* aTransferable,
    nsIClipboard::ClipboardType aClipboardType) const {
  MOZ_ASSERT(aTransferable);
  if (aDataTransfer) {
    MOZ_ASSERT(aDataTransfer->ClipboardType() == Some(aClipboardType));
    bool readFromClipboard = true;
    nsresult rv = [aDataTransfer, aTransferable,
                   &readFromClipboard]() -> nsresult {
      nsIClipboardDataSnapshot* snapshot =
          aDataTransfer->GetClipboardDataSnapshot();
      MOZ_ASSERT(snapshot);
      bool snapshotIsValid = false;
      snapshot->GetValid(&snapshotIsValid);
      // By default, if we have a valid snapshot, we should only read from that
      // and not fall back to the clipboard to avoid bypassing a BLOCK result
      // from Content Analysis (bug 1936204). There are some exceptions which
      // are dealt with later, however.
      readFromClipboard = !snapshotIsValid;
      if (!snapshotIsValid) {
        NS_WARNING(
            "DataTransfer::GetClipboardDataSnapshot() is not valid, falling "
            "back "
            "to clipboard");
        return NS_ERROR_FAILURE;
      }
      AutoTArray<nsCString, 10> transferableFlavors;
      nsresult rv =
          aTransferable->FlavorsTransferableCanImport(transferableFlavors);
      if (NS_FAILED(rv)) {
        NS_WARNING("nsITransferable::FlavorsTransferableCanImport() failed");
        return rv;
      }
      if (transferableFlavors.Length() == 1) {
        // avoid creating unneeded temporary transferables
        rv = snapshot->GetDataSync(aTransferable);
        if (NS_FAILED(rv)) {
          NS_WARNING("nsIClipboardDataSnapshot::GetDataSync() failed");
        }
        // If this fails, it may be because the snapshot was invalid and we
        // didn't detect it until now, so fall back to reading the clipboard.
        readFromClipboard = rv == NS_ERROR_NOT_AVAILABLE;
        return rv;
      }
      AutoTArray<nsCString, 5> snapshotFlavors;
      rv = snapshot->GetFlavorList(snapshotFlavors);
      if (NS_FAILED(rv)) {
        NS_WARNING("nsIClipboardDataSnapshot::GetFlavorList() failed");
        return rv;
      }
      for (const auto& transferableFlavor : transferableFlavors) {
        if (snapshotFlavors.Contains(transferableFlavor)) {
          AutoTArray<nsCString, 1> singleTypeArray{transferableFlavor};
          auto singleTransferableToCheck =
              ContentParent::CreateClipboardTransferable(singleTypeArray);
          if (singleTransferableToCheck.isErr()) {
            NS_WARNING("Failed to CreateClipboardTransferable()");
            return singleTransferableToCheck.unwrapErr();
          }
          nsCOMPtr<nsITransferable> singleTransferable =
              singleTransferableToCheck.unwrap();
          rv = snapshot->GetDataSync(singleTransferable);
          if (NS_FAILED(rv)) {
            NS_WARNING("nsIClipboardDataSnapshot::GetDataSync() failed");
            // If this fails, it may be because the snapshot was invalid and we
            // didn't detect it until now, so fall back to reading the
            // clipboard.
            readFromClipboard = rv == NS_ERROR_NOT_AVAILABLE;
            return rv;
          }
          nsCOMPtr<nsISupports> data;
          rv = singleTransferable->GetTransferData(transferableFlavor.get(),
                                                   getter_AddRefs(data));
          if (NS_FAILED(rv)) {
            NS_WARNING("nsITransferable::GetTransferData() failed");
            return rv;
          }
          rv = aTransferable->SetTransferData(transferableFlavor.get(), data);
          if (NS_FAILED(rv)) {
            NS_WARNING("nsITransferable::SetTransferData() failed");
            return rv;
          }
          return NS_OK;
        }
      }
      // The snapshot doesn't have any relevant data. Leave aTransferable empty
      // but return NS_OK since the operation did succeed (there just isn't any
      // data) and some tests expect this.
      return NS_OK;
    }();
    // If the operation failed, only fall back to the clipboard if indicated.
    if (NS_SUCCEEDED(rv) || !readFromClipboard) {
      return rv;
    }
  }

  // Get Clipboard Service
  nsresult rv;
  nsCOMPtr<nsIClipboard> clipboard =
      do_GetService("@mozilla.org/widget/clipboard;1", &rv);
  if (NS_FAILED(rv)) {
    NS_WARNING("Failed to get nsIClipboard service");
    return rv;
  }

  auto* windowContext = GetDocument()->GetWindowContext();
  if (!windowContext) {
    NS_WARNING("No window context");
    return NS_ERROR_FAILURE;
  }
  rv = clipboard->GetData(aTransferable, aClipboardType, windowContext);
  if (NS_FAILED(rv)) {
    NS_WARNING("nsIClipboard::GetData() failed");
    return rv;
  }
  return NS_OK;
}

LimitersAndCaretData EditorBase::SelectionLimitersAndCaretData() const {
  MOZ_ASSERT(mEditActionData);
  return mEditActionData->SelectionLimitersAndCaretData();
}

}  // namespace mozilla
