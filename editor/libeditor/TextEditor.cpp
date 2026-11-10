/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "TextEditor.h"

#include <algorithm>

#include "EditAction.h"
#include "EditAggregateTransaction.h"
#include "EditorDOMPoint.h"
#include "EditorUtils.h"
#include "HTMLEditor.h"
#include "InternetCiter.h"
#include "PlaceholderTransaction.h"
#include "gfxFontUtils.h"

#include "mozilla/dom/DocumentInlines.h"
#include "mozilla/Assertions.h"
#include "mozilla/ContentIterator.h"
#include "mozilla/IMEStateManager.h"
#include "mozilla/Logging.h"
#include "mozilla/LookAndFeel.h"
#include "mozilla/mozalloc.h"
#include "mozilla/Preferences.h"
#include "mozilla/PresShell.h"
#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/StaticPrefs_editor.h"
#include "mozilla/TextComposition.h"
#include "mozilla/TextEvents.h"
#include "mozilla/TextServicesDocument.h"
#include "mozilla/Try.h"
#include "mozilla/dom/CharacterDataBuffer.h"
#include "mozilla/dom/Event.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/Selection.h"
#include "mozilla/dom/StaticRange.h"

#include "nsAString.h"
#include "nsCRT.h"
#include "nsCaret.h"
#include "nsCharTraits.h"
#include "nsComponentManagerUtils.h"
#include "mozilla/dom/ContentList.h"
#include "mozilla/Utf16.h"
#include "nsDebug.h"
#include "nsDependentSubstring.h"
#include "nsError.h"
#include "nsFocusManager.h"
#include "nsGkAtoms.h"
#include "nsIContent.h"
#include "nsINode.h"
#include "nsIPrincipal.h"
#include "nsISelectionController.h"
#include "nsISupportsPrimitives.h"
#include "nsITransferable.h"
#include "nsIWeakReferenceUtils.h"
#include "nsNameSpaceManager.h"
#include "nsLiteralString.h"
#include "nsPresContext.h"
#include "nsReadableUtils.h"
#include "nsServiceManagerUtils.h"
#include "nsString.h"
#include "nsStringFwd.h"
#include "nsTextNode.h"
#include "nsUnicharUtils.h"
#include "nsXPCOM.h"

class nsIOutputStream;
class nsISupports;

namespace mozilla {

// This logs the important things for the lifecycle of the TextEditor.
LazyLogModule gTextEditorLog("TextEditor");

static void LogOrWarn(const TextEditor* aTextEditor, LazyLogModule& aLog,
                      LogLevel aLogLevel, const char* aStr) {
#ifdef DEBUG
  if (MOZ_LOG_TEST(aLog, aLogLevel)) {
    MOZ_LOG(aLog, aLogLevel, ("%p: %s", aTextEditor, aStr));
  } else {
    NS_WARNING(aStr);
  }
#else
  MOZ_LOG(aLog, aLogLevel, ("%p: %s", aTextEditor, aStr));
#endif
}

using namespace dom;

template EditorDOMPoint TextEditor::FindBetterInsertionPoint(
    const EditorDOMPoint& aPoint) const;
template EditorRawDOMPoint TextEditor::FindBetterInsertionPoint(
    const EditorRawDOMPoint& aPoint) const;

TextEditor::TextEditor() : EditorBase(EditorBase::EditorType::Text) {
  // printf("Size of TextEditor: %zu\n", sizeof(TextEditor));
  static_assert(
      sizeof(TextEditor) <= 512,
      "TextEditor instance should be allocatable in the quantum class bins");
  MOZ_LOG(gTextEditorLog, LogLevel::Info,
          ("%p: New instance is created", this));
}

TextEditor::~TextEditor() {
  // Remove event listeners. Note that if we had an HTML editor,
  //  it installed its own instead of these
  RemoveEventListeners();

  MOZ_LOG(gTextEditorLog, LogLevel::Info, ("%p: Deleted", this));
}

NS_IMPL_CYCLE_COLLECTION_CLASS(TextEditor)

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN_INHERITED(TextEditor, EditorBase)
  if (tmp->mPasswordMaskData) {
    tmp->mPasswordMaskData->CancelTimer(PasswordMaskData::ReleaseTimer::No);
    NS_IMPL_CYCLE_COLLECTION_UNLINK(mPasswordMaskData->mTimer)
  }
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN_INHERITED(TextEditor, EditorBase)
  if (tmp->mPasswordMaskData) {
    NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mPasswordMaskData->mTimer)
  }
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_ADDREF_INHERITED(TextEditor, EditorBase)
NS_IMPL_RELEASE_INHERITED(TextEditor, EditorBase)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(TextEditor)
  NS_INTERFACE_MAP_ENTRY(nsITimerCallback)
  NS_INTERFACE_MAP_ENTRY(nsINamed)
NS_INTERFACE_MAP_END_INHERITING(EditorBase)

NS_IMETHODIMP TextEditor::EndOfDocument() {
  AutoEditActionDataSetter editActionData(*this, EditAction::eNotEditing);
  if (NS_WARN_IF(!editActionData.CanHandle())) {
    return NS_ERROR_NOT_INITIALIZED;
  }
  nsresult rv = CollapseSelectionToEndOfTextNode();
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "TextEditor::CollapseSelectionToEndOfTextNode() failed");
  // This is low level API for embedders and chrome script so that we can return
  // raw error code here.
  return rv;
}

nsresult TextEditor::CollapseSelectionToEndOfTextNode() {
  MOZ_ASSERT(IsEditActionDataAvailable());

  Element* anonymousDivElement = GetRoot();
  if (NS_WARN_IF(!anonymousDivElement)) {
    return NS_ERROR_NULL_POINTER;
  }

  RefPtr<Text> textNode =
      Text::FromNodeOrNull(anonymousDivElement->GetFirstChild());
  MOZ_ASSERT(textNode);
  nsresult rv = CollapseSelectionToEndOf(*textNode);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "EditorBase::CollapseSelectionToEndOf() failed");
  return rv;
}

nsresult TextEditor::Init(Document& aDocument, Element& aAnonymousDivElement,
                          nsISelectionController& aSelectionController,
                          uint32_t aFlags,
                          UniquePtr<PasswordMaskData>&& aPasswordMaskData) {
  MOZ_ASSERT(!mInitSucceeded,
             "TextEditor::Init() called again without calling PreDestroy()?");
  MOZ_ASSERT(!(aFlags & nsIEditor::eEditorPasswordMask) == !aPasswordMaskData);

  MOZ_LOG(gTextEditorLog, LogLevel::Info,
          ("%p: Init(aDocument=%p, aAnonymousDivElement=%s, "
           "aSelectionController=%p, aPasswordMaskData=%p)",
           this, &aDocument, ToString(RefPtr{&aAnonymousDivElement}).c_str(),
           &aSelectionController, aPasswordMaskData.get()));

  mPasswordMaskData = std::move(aPasswordMaskData);

  // Init the base editor
  nsresult rv = InitInternal(aDocument, &aAnonymousDivElement,
                             aSelectionController, aFlags);
  if (NS_FAILED(rv)) {
    LogOrWarn(this, gTextEditorLog, LogLevel::Error,
              "EditorBase::InitInternal() failed");
    return rv;
  }

  AutoEditActionDataSetter editActionData(*this, EditAction::eInitializing);
  if (MOZ_UNLIKELY(!editActionData.CanHandle())) {
    LogOrWarn(this, gTextEditorLog, LogLevel::Error,
              "AutoEditActionDataSetter::CanHandle() failed");
    return NS_ERROR_FAILURE;
  }

  // We set the initialized state here rather than at the end of the function,
  // since InitEditorContentAndSelection() can perform some transactions
  // and can warn if mInitSucceeded is still false.
  MOZ_ASSERT(!mInitSucceeded, "TextEditor::Init() shouldn't be nested");
  mInitSucceeded = true;
  editActionData.OnEditorInitialized();

  rv = InitEditorContentAndSelection();
  if (NS_FAILED(rv)) {
    LogOrWarn(this, gTextEditorLog, LogLevel::Error,
              "TextEditor::InitEditorContentAndSelection() failed");
    // XXX Shouldn't we expose `NS_ERROR_EDITOR_DESTROYED` even though this
    //     is a public method?
    mInitSucceeded = false;
    editActionData.OnEditorDestroy();
    return EditorBase::ToGenericNSResult(rv);
  }

  // Throw away the old transaction manager if this is not the first time that
  // we're initializing the editor.
  ClearUndoRedo();
  EnableUndoRedo();
  return NS_OK;
}

nsresult TextEditor::InitEditorContentAndSelection() {
  MOZ_ASSERT(IsEditActionDataAvailable());

  MOZ_TRY(EnsureEmptyTextFirstChild());

  // If the selection hasn't been set up yet, set it up collapsed to the end of
  // our editable content.
  if (!SelectionRef().RangeCount()) {
    nsresult rv = CollapseSelectionToEndOfTextNode();
    if (NS_FAILED(rv)) {
      LogOrWarn(this, gTextEditorLog, LogLevel::Error,
                "EditorBase::CollapseSelectionToEndOfTextNode() failed");
      return rv;
    }
  }

  if (!IsSingleLineEditor()) {
    nsresult rv = EnsurePaddingBRElementInMultilineEditor();
    if (NS_FAILED(rv)) {
      LogOrWarn(this, gTextEditorLog, LogLevel::Error,
                "EditorBase::EnsurePaddingBRElementInMultilineEditor() failed");
      return rv;
    }
  }

  return NS_OK;
}

nsresult TextEditor::PostCreate() {
  MOZ_LOG(gTextEditorLog, LogLevel::Info,
          ("%p: PostCreate(), mDidPostCreate=%s", this,
           TrueOrFalse(mDidPostCreate)));

  AutoEditActionDataSetter editActionData(*this, EditAction::eNotEditing);
  if (MOZ_UNLIKELY(!editActionData.CanHandle())) {
    LogOrWarn(this, gTextEditorLog, LogLevel::Error,
              "AutoEditActionDataSetter::CanHandle() failed");
    return NS_ERROR_NOT_INITIALIZED;
  }

  nsresult rv = PostCreateInternal();

  // Restore unmasked range if there is.
  if (IsPasswordEditor() && !IsAllMasked()) {
    DebugOnly<nsresult> rvIgnored =
        SetUnmaskRangeAndNotify(UnmaskedStart(), UnmaskedLength());
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rvIgnored),
                         "TextEditor::SetUnmaskRangeAndNotify() failed to "
                         "restore unmasked range, but ignored");
  }

  if (NS_FAILED(rv)) {
    LogOrWarn(this, gTextEditorLog, LogLevel::Error,
              "EditorBase::PostCreateInternal() failed");
    return rv;
  }

  return NS_OK;
}

UniquePtr<PasswordMaskData> TextEditor::PreDestroy() {
  MOZ_LOG(gTextEditorLog, LogLevel::Info,
          ("%p: PreDestroy() mDidPreDestroy=%s", this,
           TrueOrFalse(mDidPreDestroy)));

  if (mDidPreDestroy) {
    return nullptr;
  }

  UniquePtr<PasswordMaskData> passwordMaskData = std::move(mPasswordMaskData);
  if (passwordMaskData) {
    // Disable auto-masking timer since nobody can catch the notification
    // from the timer and canceling the unmasking.
    passwordMaskData->CancelTimer(PasswordMaskData::ReleaseTimer::Yes);
    // Similary, keeping preventing echoing password temporarily across
    // TextEditor instances is hard.  So, we should forget it.
    passwordMaskData->mEchoingPasswordPrevented = false;
  }

  PreDestroyInternal();

  return passwordMaskData;
}

Result<widget::IMEState, nsresult> TextEditor::GetPreferredIMEState() const {
  using IMEState = widget::IMEState;
  using IMEEnabled = widget::IMEEnabled;

  if (IsReadonly()) {
    return IMEState{IMEEnabled::Disabled, IMEState::DONT_CHANGE_OPEN_STATE};
  }

  Element* const textControlElement = GetExposedRoot();
  if (NS_WARN_IF(!textControlElement)) {
    return Err(NS_ERROR_FAILURE);
  }
  MOZ_ASSERT(textControlElement->IsTextControlElement());

  nsIFrame* const textControlFrame = textControlElement->GetPrimaryFrame();
  if (NS_WARN_IF(!textControlFrame)) {
    return Err(NS_ERROR_FAILURE);
  }

  switch (textControlFrame->StyleUIReset()->mIMEMode) {
    case StyleImeMode::Auto:
    default:
      return IMEState{
          IsPasswordEditor() ? IMEEnabled::Password : IMEEnabled::Enabled,
          IMEState::DONT_CHANGE_OPEN_STATE};
    case StyleImeMode::Disabled:
      // we should use password state for |ime-mode: disabled;|.
      return IMEState{IMEEnabled::Password, IMEState::DONT_CHANGE_OPEN_STATE};
    case StyleImeMode::Active:
      return IMEState{IMEEnabled::Enabled, IMEState::OPEN};
    case StyleImeMode::Inactive:
      return IMEState{IMEEnabled::Enabled, IMEState::CLOSED};
    case StyleImeMode::Normal:
      return IMEState{IMEEnabled::Enabled, IMEState::DONT_CHANGE_OPEN_STATE};
  }
}

nsresult TextEditor::HandleKeyPressEvent(WidgetKeyboardEvent* aKeyboardEvent) {
  // NOTE: When you change this method, you should also change:
  //   * editor/libeditor/tests/test_texteditor_keyevent_handling.html
  //   * editor/libeditor/tests/test_htmleditor_keyevent_handling.html
  //
  // And also when you add new key handling, you need to change the subclass's
  // HandleKeyPressEvent()'s switch statement.

  if (NS_WARN_IF(!aKeyboardEvent)) {
    return NS_ERROR_UNEXPECTED;
  }

  if (IsReadonly()) {
    HandleKeyPressEventInReadOnlyMode(*aKeyboardEvent);
    return NS_OK;
  }

  MOZ_ASSERT(aKeyboardEvent->mMessage == eKeyPress,
             "HandleKeyPressEvent gets non-keypress event");

  switch (aKeyboardEvent->mKeyCode) {
    case NS_VK_META:
    case NS_VK_WIN:
    case NS_VK_SHIFT:
    case NS_VK_CONTROL:
    case NS_VK_ALT:
      // FYI: This shouldn't occur since modifier key shouldn't cause eKeyPress
      //      event.
      aKeyboardEvent->PreventDefault();
      return NS_OK;

    case NS_VK_BACK:
    case NS_VK_DELETE:
    case NS_VK_TAB: {
      nsresult rv = EditorBase::HandleKeyPressEvent(aKeyboardEvent);
      NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                           "EditorBase::HandleKeyPressEvent() failed");
      return rv;
    }
    case NS_VK_RETURN: {
      if (!aKeyboardEvent->IsInputtingLineBreak()) {
        return NS_OK;
      }
      if (!IsSingleLineEditor()) {
        aKeyboardEvent->PreventDefault();
      }
      // We need to dispatch "beforeinput" event at least even if we're a
      // single line text editor.
      nsresult rv = InsertLineBreakAsAction();
      NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                           "TextEditor::InsertLineBreakAsAction() failed");
      return rv;
    }
  }

  if (!aKeyboardEvent->IsInputtingText()) {
    // we don't PreventDefault() here or keybindings like control-x won't work
    return NS_OK;
  }
  aKeyboardEvent->PreventDefault();
  // If we dispatch 2 keypress events for a surrogate pair and we set only
  // first `.key` value to the surrogate pair, the preceding one has it and the
  // other has empty string.  In this case, we should handle only the first one
  // with the key value.
  if (!StaticPrefs::dom_event_keypress_dispatch_once_per_surrogate_pair() &&
      !StaticPrefs::dom_event_keypress_key_allow_lone_surrogate() &&
      aKeyboardEvent->mKeyValue.IsEmpty() &&
      mozilla::IsSurrogate(aKeyboardEvent->mCharCode)) {
    return NS_OK;
  }
  // Our widget shouldn't set `\r` to `mKeyValue`, but it may be synthesized
  // keyboard event and its value may be `\r`.  In such case, we should treat
  // it as `\n` for the backward compatibility because we stopped converting
  // `\r` and `\r\n` to `\n` at getting `HTMLInputElement.value` and
  // `HTMLTextAreaElement.value` for the performance (i.e., we don't need to
  // take care in `HTMLEditor`).
  nsAutoString str(aKeyboardEvent->mKeyValue);
  if (str.IsEmpty()) {
    MOZ_ASSERT(aKeyboardEvent->mCharCode <= 0xFFFF,
               "Non-BMP character needs special handling");
    str.Assign(aKeyboardEvent->mCharCode == nsCRT::CR
                   ? static_cast<char16_t>(nsCRT::LF)
                   : static_cast<char16_t>(aKeyboardEvent->mCharCode));
  } else {
    MOZ_ASSERT(str.Find(u"\r\n"_ns) == kNotFound,
               "This assumes that typed text does not include CRLF");
    str.ReplaceChar('\r', '\n');
  }
  nsresult rv = OnInputText(str);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv), "EditorBase::OnInputText() failed");
  return rv;
}

NS_IMETHODIMP TextEditor::InsertLineBreak() {
  AutoEditActionDataSetter editActionData(*this, EditAction::eInsertLineBreak);
  nsresult rv = editActionData.CanHandleAndMaybeDispatchBeforeInputEvent();
  if (NS_FAILED(rv)) {
    NS_WARNING_ASSERTION(rv == NS_ERROR_EDITOR_ACTION_CANCELED,
                         "CanHandleAndMaybeDispatchBeforeInputEvent() failed");
    return EditorBase::ToGenericNSResult(rv);
  }

  if (NS_WARN_IF(IsSingleLineEditor())) {
    return NS_ERROR_FAILURE;
  }

  AutoPlaceholderBatch treatAsOneTransaction(
      *this, ScrollSelectionIntoView::Yes, __FUNCTION__);
  rv = InsertLineBreakAsSubAction();
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "TextEditor::InsertLineBreakAsSubAction() failed");
  return EditorBase::ToGenericNSResult(rv);
}

nsresult TextEditor::ComputeTextValue(nsAString& aString) const {
  // This is a public method so that anytime this may be called, e.g., before
  // initialized or after destroyed.  However, GetTextNode() is an internal API
  // for friend classes and internal use.  Therefore, it asserts the conditions
  // whether the anonymous subtree is available.  Therefore, this method needs
  // to get the Text with the anonymous <div>.
  const Element* const anonymousDivElement = GetRoot();
  if (MOZ_UNLIKELY(!anonymousDivElement)) {
    // If we're temporarily destroyed while we're handling something, we can get
    // the value from the cached text node which was maybe modified by us.  This
    // helps TextControlState and TextInputListener to notify the text control
    // element of value changes, etc, at UnbindFromFrame().
    const Text* const cachedTextNode = GetCachedTextNode();
    if (NS_WARN_IF(!cachedTextNode)) {
      return NS_ERROR_NOT_INITIALIZED;
    }
    cachedTextNode->GetData(aString);
    return NS_OK;
  }

  const auto* const text =
      Text::FromNodeOrNull(anonymousDivElement->GetFirstChild());
  if (MOZ_UNLIKELY(!text)) {
    MOZ_ASSERT_UNREACHABLE("how?");
    return NS_ERROR_UNEXPECTED;
  }

  text->GetData(aString);
  return NS_OK;
}

nsresult TextEditor::InsertLineBreakAsAction(nsIPrincipal* aPrincipal) {
  AutoEditActionDataSetter editActionData(*this, EditAction::eInsertLineBreak,
                                          aPrincipal);
  nsresult rv = editActionData.CanHandleAndMaybeDispatchBeforeInputEvent();
  if (NS_FAILED(rv)) {
    NS_WARNING_ASSERTION(rv == NS_ERROR_EDITOR_ACTION_CANCELED,
                         "CanHandleAndMaybeDispatchBeforeInputEvent() failed");
    return EditorBase::ToGenericNSResult(rv);
  }

  if (IsSingleLineEditor()) {
    return NS_OK;
  }

  // XXX This may be called by execCommand() with "insertParagraph".
  //     In such case, naming the transaction "TypingTxnName" is odd.
  AutoPlaceholderBatch treatAsOneTransaction(*this, *nsGkAtoms::TypingTxnName,
                                             ScrollSelectionIntoView::Yes,
                                             __FUNCTION__);
  rv = InsertLineBreakAsSubAction();
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "EditorBase::InsertLineBreakAsSubAction() failed");
  return EditorBase::ToGenericNSResult(rv);
}

nsresult TextEditor::SetTextAsAction(
    const nsAString& aString,
    AllowBeforeInputEventCancelable aAllowBeforeInputEventCancelable,
    nsIPrincipal* aPrincipal) {
  MOZ_ASSERT(aString.FindChar(nsCRT::CR) == kNotFound);

  AutoEditActionDataSetter editActionData(*this, EditAction::eSetText,
                                          aPrincipal);
  if (aAllowBeforeInputEventCancelable == AllowBeforeInputEventCancelable::No) {
    editActionData.MakeBeforeInputEventNonCancelable();
  }
  nsresult rv = editActionData.CanHandleAndMaybeDispatchBeforeInputEvent();
  if (NS_FAILED(rv)) {
    NS_WARNING_ASSERTION(rv == NS_ERROR_EDITOR_ACTION_CANCELED,
                         "CanHandleAndMaybeDispatchBeforeInputEvent() failed");
    return EditorBase::ToGenericNSResult(rv);
  }

  AutoPlaceholderBatch treatAsOneTransaction(
      *this, ScrollSelectionIntoView::Yes, __FUNCTION__);
  rv = SetTextAsSubAction(aString);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "TextEditor::SetTextAsSubAction() failed");
  return EditorBase::ToGenericNSResult(rv);
}

nsresult TextEditor::SetTextAsSubAction(const nsAString& aString) {
  MOZ_ASSERT(IsEditActionDataAvailable());
  MOZ_ASSERT(mPlaceholderBatch);

  if (NS_WARN_IF(!mInitSucceeded)) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  IgnoredErrorResult ignoredError;
  AutoEditSubActionNotifier startToHandleEditSubAction(
      *this, EditSubAction::eSetText, nsIEditor::eNext, ignoredError);
  if (NS_WARN_IF(ignoredError.ErrorCodeIs(NS_ERROR_EDITOR_DESTROYED))) {
    return ignoredError.StealNSResult();
  }
  NS_WARNING_ASSERTION(
      !ignoredError.Failed(),
      "TextEditor::OnStartToHandleTopLevelEditSubAction() failed, but ignored");

  if (!IsIMEComposing() && !IsUndoRedoEnabled() &&
      GetEditAction() != EditAction::eReplaceText && mMaxTextLength < 0) {
    Result<EditActionResult, nsresult> result =
        SetTextWithoutTransaction(aString);
    if (MOZ_UNLIKELY(result.isErr())) {
      NS_WARNING("TextEditor::SetTextWithoutTransaction() failed");
      return result.unwrapErr();
    }
    if (!result.inspect().Ignored()) {
      return NS_OK;
    }
  }

  {
    // Note that do not notify selectionchange caused by selecting all text
    // because it's preparation of our delete implementation so web apps
    // shouldn't receive such selectionchange before the first mutation.
    AutoUpdateViewBatch preventSelectionChangeEvent(*this, __FUNCTION__);

    // XXX We should make ReplaceSelectionAsSubAction() take range.  Then,
    //     we can saving the expensive cost of modifying `Selection` here.
    if (NS_SUCCEEDED(SelectEntireDocument())) {
      DebugOnly<nsresult> rvIgnored = ReplaceSelectionAsSubAction(aString);
      NS_WARNING_ASSERTION(
          NS_SUCCEEDED(rvIgnored),
          "EditorBase::ReplaceSelectionAsSubAction() failed, but ignored");
    }
  }

  // Destroying AutoUpdateViewBatch may cause destroying us.
  return NS_WARN_IF(Destroyed()) ? NS_ERROR_EDITOR_DESTROYED : NS_OK;
}

already_AddRefed<Element> TextEditor::GetInputEventTargetElement() const {
  RefPtr<Element> target = Element::FromEventTargetOrNull(mEventTarget);
  return target.forget();
}

bool TextEditor::IsEmpty() const {
  // This is a public method so that anytime this may be called, e.g., before
  // initialized or after destroyed.  However, GetTextNode() is an internal API
  // for friend classes and internal use.  Therefore, it asserts the conditions
  // whether the anonymous subtree is available.  Therefore, this method needs
  // to get the Text with the anonymous <div>.
  if (MOZ_UNLIKELY(!mInitSucceeded)) {
    // If we're temporarily destroyed while we're handling something, we can get
    // the value from the cached text node which was maybe modified by us.  This
    // helps TextControlState and TextInputListener to notify the text control
    // element of value changes, etc, at UnbindFromFrame().
    const Text* const cachedTextNode = GetCachedTextNode();
    return NS_WARN_IF(!cachedTextNode) || !cachedTextNode->TextDataLength();
  }
  const Text* const textNode = GetTextNode();
  return !textNode || !textNode->TextDataLength();
}

NS_IMETHODIMP TextEditor::GetTextLength(uint32_t* aCount) {
  MOZ_ASSERT(aCount);
  // This is a public method so that anytime this may be called, e.g., before
  // initialized or after destroyed.  However, GetTextNode() is an internal API
  // for friend classes and internal use.  Therefore, it asserts the conditions
  // whether the anonymous subtree is available.  Therefore, this method needs
  // to get the Text with the anonymous <div>.
  if (MOZ_UNLIKELY(!mInitSucceeded)) {
    // If we're temporarily destroyed while we're handling something, we can get
    // the value from the cached text node which was maybe modified by us.
    const Text* const textNode = GetCachedTextNode();
    if (NS_WARN_IF(!textNode)) {
      return NS_ERROR_FAILURE;
    }
    *aCount = textNode->TextDataLength();
    return NS_OK;
  }

  const Text* const textNode = GetTextNode();
  *aCount = textNode ? textNode->TextDataLength() : 0u;
  return NS_OK;
}

bool TextEditor::IsCopyToClipboardAllowedInternal() const {
  MOZ_ASSERT(IsEditActionDataAvailable());
  if (!EditorBase::IsCopyToClipboardAllowedInternal()) {
    return false;
  }

  if (!IsSingleLineEditor() || !IsPasswordEditor() ||
      NS_WARN_IF(!mPasswordMaskData)) {
    return true;
  }

  // The reveal button makes the value visible to the user, so copying it
  // leaks nothing they cannot already read off the screen.
  if (const Element* const textControlElement = GetExposedRoot()) {
    if (textControlElement->State().HasState(ElementState::REVEALED)) {
      return true;
    }
  }

  // If we're a password editor, we should allow selected text to be copied
  // to the clipboard only when selection range is in unmasked range.
  if (IsAllMasked() || IsMaskingPassword() || !UnmaskedLength()) {
    return false;
  }

  // If there are 2 or more ranges, we don't allow to copy/cut for now since
  // we need to check whether all ranges are in unmasked range or not.
  // Anyway, such operation in password field does not make sense.
  if (SelectionRef().RangeCount() > 1) {
    return false;
  }

  uint32_t selectionStart = 0, selectionEnd = 0;
  nsContentUtils::GetSelectionInTextControl(&SelectionRef(), mRootElement,
                                            selectionStart, selectionEnd);
  return UnmaskedStart() <= selectionStart && UnmaskedEnd() >= selectionEnd;
}

nsresult TextEditor::HandlePasteAsQuotation(
    AutoEditActionDataSetter& aEditActionData,
    nsIClipboard::ClipboardType aClipboardType, DataTransfer* aDataTransfer) {
  MOZ_ASSERT(aClipboardType == nsIClipboard::kGlobalClipboard ||
             aClipboardType == nsIClipboard::kSelectionClipboard);
  if (NS_WARN_IF(!GetDocument())) {
    return NS_OK;
  }

  // XXX Why don't we dispatch ePaste event here?

  // Get the nsITransferable interface for getting the data from the clipboard
  Result<nsCOMPtr<nsITransferable>, nsresult> maybeTransferable =
      EditorUtils::CreateTransferableForPlainText(*GetDocument());
  if (maybeTransferable.isErr()) {
    NS_WARNING("EditorUtils::CreateTransferableForPlainText() failed");
    return maybeTransferable.unwrapErr();
  }
  nsCOMPtr<nsITransferable> trans(maybeTransferable.unwrap());
  if (!trans) {
    NS_WARNING(
        "EditorUtils::CreateTransferableForPlainText() returned nullptr, but "
        "ignored");
    return NS_OK;
  }

  // Get the Data from the clipboard
  nsresult rv =
      GetDataFromDataTransferOrClipboard(aDataTransfer, trans, aClipboardType);

  // Now we ask the transferable for the data
  // it still owns the data, we just have a pointer to it.
  // If it can't support a "text" output of the data the call will fail
  nsCOMPtr<nsISupports> genericDataObj;
  nsAutoCString flavor;
  rv = trans->GetAnyTransferData(flavor, getter_AddRefs(genericDataObj));
  if (NS_FAILED(rv)) {
    NS_WARNING("nsITransferable::GetAnyTransferData() failed");
    return rv;
  }

  if (!flavor.EqualsLiteral(kTextMime) &&
      !flavor.EqualsLiteral(kMozTextInternal) &&
      !flavor.EqualsLiteral(kURLDataMime)) {
    return NS_OK;
  }

  nsCOMPtr<nsISupportsString> text = do_QueryInterface(genericDataObj);
  if (!text) {
    return NS_OK;
  }

  nsString stuffToPaste;
  DebugOnly<nsresult> rvIgnored = text->GetData(stuffToPaste);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rvIgnored),
                       "nsISupportsString::GetData() failed, but ignored");
  if (stuffToPaste.IsEmpty()) {
    return NS_OK;
  }

  aEditActionData.SetData(stuffToPaste);
  if (!stuffToPaste.IsEmpty()) {
    nsContentUtils::PlatformToDOMLineBreaks(stuffToPaste);
  }
  rv = aEditActionData.MaybeDispatchBeforeInputEvent();
  if (NS_FAILED(rv)) {
    NS_WARNING_ASSERTION(rv == NS_ERROR_EDITOR_ACTION_CANCELED,
                         "MaybeDispatchBeforeInputEvent() failed");
    return rv;
  }

  AutoPlaceholderBatch treatAsOneTransaction(
      *this, ScrollSelectionIntoView::Yes, __FUNCTION__);
  rv = InsertWithQuotationsAsSubAction(stuffToPaste);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "TextEditor::InsertWithQuotationsAsSubAction() failed");
  return rv;
}

nsresult TextEditor::InsertWithQuotationsAsSubAction(
    const nsAString& aQuotedText) {
  MOZ_ASSERT(IsEditActionDataAvailable());

  if (IsReadonly()) {
    return NS_OK;
  }

  // Let the citer quote it for us:
  nsString quotedStuff;
  InternetCiter::GetCiteString(aQuotedText, quotedStuff);

  // It's best to put a blank line after the quoted text so that mails
  // written without thinking won't be so ugly.
  if (!aQuotedText.IsEmpty() && (aQuotedText.Last() != char16_t('\n'))) {
    quotedStuff.Append(char16_t('\n'));
  }

  IgnoredErrorResult ignoredError;
  AutoEditSubActionNotifier startToHandleEditSubAction(
      *this, EditSubAction::eInsertText, nsIEditor::eNext, ignoredError);
  if (NS_WARN_IF(ignoredError.ErrorCodeIs(NS_ERROR_EDITOR_DESTROYED))) {
    return ignoredError.StealNSResult();
  }
  NS_WARNING_ASSERTION(
      !ignoredError.Failed(),
      "TextEditor::OnStartToHandleTopLevelEditSubAction() failed, but ignored");

  // XXX Do we need to support paste-as-quotation in password editor (and
  //     also in single line editor)?
  MaybeDoAutoPasswordMasking();

  nsresult rv = InsertTextAsSubAction(quotedStuff, InsertTextFor::NormalText);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "EditorBase::InsertTextAsSubAction() failed");
  return rv;
}

nsresult TextEditor::SelectEntireDocument() {
  MOZ_ASSERT(IsEditActionDataAvailable());

  if (NS_WARN_IF(!mInitSucceeded)) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  RefPtr<Element> anonymousDivElement = GetRoot();
  if (NS_WARN_IF(!anonymousDivElement)) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  RefPtr<Text> text =
      Text::FromNodeOrNull(anonymousDivElement->GetFirstChild());
  MOZ_ASSERT(text);

  MOZ_TRY(SelectionRef().SetStartAndEndInLimiter(
      *text, 0, *text, text->TextDataLength(), eDirNext,
      nsISelectionListener::SELECTALL_REASON));

  return NS_OK;
}

EventTarget* TextEditor::GetDOMEventTarget() const { return mEventTarget; }

void TextEditor::ReinitializeSelection(Element& aElement) {
  MOZ_LOG(gTextEditorLog, LogLevel::Info,
          ("%p: ReinitializeSelection(aElement=%s)", this,
           ToString(RefPtr{&aElement}).c_str()));

  if (MOZ_UNLIKELY(Destroyed())) {
    LogOrWarn(this, gTextEditorLog, LogLevel::Error, "Destroyed() failed");
    return;
  }

  AutoEditActionDataSetter editActionData(*this, EditAction::eNotEditing);
  if (MOZ_UNLIKELY(!editActionData.CanHandle())) {
    LogOrWarn(this, gTextEditorLog, LogLevel::Error,
              "AutoEditActionDataSetter::CanHandle() failed");
    return;
  }

  // We don't need to flush pending notifications here and we don't need to
  // handle spellcheck at first focus.  Therefore, we don't need to call
  // `TextEditor::OnFocus` here.
  EditorBase::OnFocus(aElement);

  // If previous focused editor turn on spellcheck and this editor doesn't
  // turn on it, spellcheck state is mismatched.  So we need to re-sync it.
  SyncRealTimeSpell();
}

nsresult TextEditor::OnFocus(const nsINode& aOriginalEventTargetNode) {
  MOZ_LOG(gTextEditorLog, LogLevel::Info,
          ("%p: OnFocus(aOriginalEventTargetNode=%s)", this,
           ToString(RefPtr{&aOriginalEventTargetNode}).c_str()));

  AutoEditActionDataSetter editActionData(*this, EditAction::eNotEditing);
  if (MOZ_UNLIKELY(!editActionData.CanHandle())) {
    LogOrWarn(this, gTextEditorLog, LogLevel::Error,
              "AutoEditActionDataSetter::CanHandle() failed");
    return NS_ERROR_FAILURE;
  }
  return EditorBase::OnFocus(aOriginalEventTargetNode);
}

void TextEditor::PostHandleFocusEvent(const nsINode& aFocusEventTargetNode) {
  MOZ_LOG(gTextEditorLog, LogLevel::Info,
          ("%p: PostHandleFocusEvent(aFocusEvent={ "
           "GetOriginalEventTarget()=%s }), %s",
           this, ToString(RefPtr{&aFocusEventTargetNode}).c_str(),
           GetFocusedElement() ? "but already lost focus" : "still has focus"));

  AutoEditActionDataSetter editActionData(*this, EditAction::eNotEditing);
  if (MOZ_UNLIKELY(!editActionData.CanHandle())) {
    LogOrWarn(this, gTextEditorLog, LogLevel::Error,
              "AutoEditActionDataSetter::CanHandle() failed");
    return;
  }

  // Spell check a textarea the first time that it is focused.
  nsresult rv = FlushPendingSpellCheck();
  if (rv == NS_ERROR_EDITOR_DESTROYED) [[unlikely]] {
    LogOrWarn(this, gTextEditorLog, LogLevel::Error,
              "EditorBase::FlushPendingSpellCheck() failed");
    return;
  }
  NS_WARNING_ASSERTION(
      NS_SUCCEEDED(rv),
      "EditorBase::FlushPendingSpellCheck() failed, but ignored");
  if (!CanKeepHandlingFocusEvent(aFocusEventTargetNode)) [[unlikely]] {
    MOZ_LOG(gTextEditorLog, LogLevel::Debug,
            ("%p: CanKeepHandlingFocusEvent() returned false after "
             "FlushPendingSpellCheck()",
             this));
    return;
  }
  EditorBase::PostHandleFocusEvent(aFocusEventTargetNode);
}

nsresult TextEditor::OnBlur(const EventTarget* aEventTarget) {
  MOZ_LOG(gTextEditorLog, LogLevel::Info,
          ("%p: OnBlur(aEventTarget=%s)", this,
           ToString(RefPtr{aEventTarget}).c_str()));

  // check if something else is focused. If another element is focused, then
  // we should not change the selection.  If another element already has focus,
  // we should not maintain the selection because we may not have the rights
  // doing it.
  if ([[maybe_unused]] Element* const focusedElement =
          nsFocusManager::GetFocusedElementStatic()) {
    MOZ_LOG(gTextEditorLog, LogLevel::Info,
            ("%p: OnBlur() is ignored because another element already has "
             "focus (%s)",
             this, ToString(RefPtr{focusedElement}).c_str()));
    return NS_OK;
  }

  nsresult rv = FinalizeSelection();
  if (NS_FAILED(rv)) {
    LogOrWarn(this, gTextEditorLog, LogLevel::Error,
              "EditorBase::FinalizeSelection() failed");
    return rv;
  }
  return NS_OK;
}

nsresult TextEditor::SetAttributeOrEquivalent(Element* aElement,
                                              nsAtom* aAttribute,
                                              const nsAString& aValue,
                                              bool /*aSuppressTransaction*/) {
  if (NS_WARN_IF(!aElement) || NS_WARN_IF(!aAttribute)) {
    return NS_ERROR_INVALID_ARG;
  }

  AutoEditActionDataSetter editActionData(*this, EditAction::eSetAttribute);
  nsresult rv = editActionData.CanHandleAndMaybeDispatchBeforeInputEvent();
  if (NS_FAILED(rv)) {
    NS_WARNING_ASSERTION(rv == NS_ERROR_EDITOR_ACTION_CANCELED,
                         "CanHandleAndMaybeDispatchBeforeInputEvent() failed");
    return EditorBase::ToGenericNSResult(rv);
  }

  rv = SetAttributeWithTransaction(*aElement, *aAttribute, aValue);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "EditorBase::SetAttributeWithTransaction() failed");
  return EditorBase::ToGenericNSResult(rv);
}

nsresult TextEditor::RemoveAttributeOrEquivalent(
    Element* aElement, nsAtom* aAttribute, bool /*aSuppressTransaction*/) {
  if (NS_WARN_IF(!aElement) || NS_WARN_IF(!aAttribute)) {
    return NS_ERROR_INVALID_ARG;
  }

  AutoEditActionDataSetter editActionData(*this, EditAction::eRemoveAttribute);
  nsresult rv = editActionData.CanHandleAndMaybeDispatchBeforeInputEvent();
  if (NS_FAILED(rv)) {
    NS_WARNING_ASSERTION(rv == NS_ERROR_EDITOR_ACTION_CANCELED,
                         "CanHandleAndMaybeDispatchBeforeInputEvent() failed");
    return EditorBase::ToGenericNSResult(rv);
  }

  rv = RemoveAttributeWithTransaction(*aElement, *aAttribute);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "EditorBase::RemoveAttributeWithTransaction() failed");
  return EditorBase::ToGenericNSResult(rv);
}

template <typename EditorDOMPointType>
EditorDOMPointType TextEditor::FindBetterInsertionPoint(
    const EditorDOMPointType& aPoint) const {
  if (MOZ_UNLIKELY(NS_WARN_IF(!aPoint.IsInContentNode()))) {
    return aPoint;
  }

  MOZ_ASSERT(aPoint.IsSetAndValid());

  Element* const anonymousDivElement = GetRoot();
  if (aPoint.GetContainer() == anonymousDivElement) {
    // In some cases, aPoint points start of the anonymous <div>.  To avoid
    // injecting unneeded text nodes, we first look to see if we have one
    // available.  In that case, we'll just adjust node and offset accordingly.
    if (aPoint.IsStartOfContainer()) {
      if (aPoint.GetContainer()->HasChildren() &&
          aPoint.GetContainer()->GetFirstChild()->IsText()) {
        return EditorDOMPointType(aPoint.GetContainer()->GetFirstChild(), 0u);
      }
    }
    // In some other cases, aPoint points the terminating padding <br> element
    // for empty last line in the anonymous <div>.  In that case, we'll adjust
    // aInOutNode and aInOutOffset to the preceding text node, if any.
    else {
      nsIContent* child = aPoint.GetContainer()->GetLastChild();
      while (child) {
        if (child->IsText()) {
          return EditorDOMPointType::AtEndOf(*child);
        }
        child = child->GetPreviousSibling();
      }
    }
  }

  // Sometimes, aPoint points the padding <br> element.  In that case, we'll
  // adjust the insertion point to the previous text node, if one exists, or to
  // the parent anonymous DIV.
  if (EditorUtils::IsPaddingBRElementForEmptyLastLine(
          *aPoint.template ContainerAs<nsIContent>()) &&
      aPoint.IsStartOfContainer()) {
    nsIContent* previousSibling = aPoint.GetContainer()->GetPreviousSibling();
    if (previousSibling && previousSibling->IsText()) {
      return EditorDOMPointType::AtEndOf(*previousSibling);
    }

    nsINode* parentOfContainer = aPoint.GetContainerParent();
    if (parentOfContainer && parentOfContainer == anonymousDivElement) {
      return EditorDOMPointType(parentOfContainer,
                                aPoint.template ContainerAs<nsIContent>(), 0u);
    }
  }

  return aPoint;
}

// static
void TextEditor::MaskString(nsString& aString, const Text& aTextNode,
                            uint32_t aStartOffsetInString,
                            uint32_t aStartOffsetInText) {
  MOZ_ASSERT(aTextNode.HasFlag(NS_MAYBE_MASKED));
  MOZ_ASSERT(aStartOffsetInString == 0 || aStartOffsetInText == 0);

  const TextEditor* const textEditor =
      nsContentUtils::GetExtantTextEditorFromAnonymousNode(&aTextNode);
  if (textEditor) {
    // The reveal button shows the whole value, so nothing should be masked.
    const Element* const textControlElement = textEditor->GetExposedRoot();
    if (textControlElement &&
        textControlElement->State().HasState(ElementState::REVEALED)) {
      return;
    }
  }
  uint32_t unmaskStart = UINT32_MAX, unmaskLength = 0;
  if (textEditor && textEditor->UnmaskedLength() > 0) {
    unmaskStart = textEditor->UnmaskedStart();
    unmaskLength = textEditor->UnmaskedLength();
    // If text is copied from after unmasked range, we can treat this case
    // as mask all.
    if (aStartOffsetInText >= unmaskStart + unmaskLength) {
      unmaskLength = 0;
      unmaskStart = UINT32_MAX;
    } else {
      // If text is copied from middle of unmasked range, reduce the length
      // and adjust start offset.
      if (aStartOffsetInText > unmaskStart) {
        unmaskLength = unmaskStart + unmaskLength - aStartOffsetInText;
        unmaskStart = 0;
      }
      // If text is copied from before start of unmasked range, just adjust
      // the start offset.
      else {
        unmaskStart -= aStartOffsetInText;
      }
      // Make the range is in the string.
      unmaskStart += aStartOffsetInString;
    }
  }

  const char16_t kPasswordMask = TextEditor::PasswordMask();
  for (uint32_t i = aStartOffsetInString; i < aString.Length(); ++i) {
    bool isSurrogatePair = mozilla::IsHighSurrogate(aString.CharAt(i)) &&
                           i < aString.Length() - 1 &&
                           mozilla::IsLowSurrogate(aString.CharAt(i + 1));
    if (i < unmaskStart || i >= unmaskStart + unmaskLength) {
      if (isSurrogatePair) {
        aString.SetCharAt(kPasswordMask, i);
        aString.SetCharAt(kPasswordMask, i + 1);
      } else {
        aString.SetCharAt(kPasswordMask, i);
      }
    }

    // Skip the following low surrogate.
    if (isSurrogatePair) {
      ++i;
    }
  }
}

nsresult TextEditor::SetUnmaskRangeInternal(uint32_t aStart, uint32_t aLength,
                                            uint32_t aTimeout, bool aNotify,
                                            bool aForceStartMasking) {
  if (mPasswordMaskData) {
    mPasswordMaskData->mIsMaskingPassword = aForceStartMasking || aTimeout != 0;

    // We cannot manage multiple unmasked ranges so that shrink the previous
    // range first.
    if (!IsAllMasked()) {
      mPasswordMaskData->mUnmaskedLength = 0;
      mPasswordMaskData->CancelTimer(PasswordMaskData::ReleaseTimer::No);
    }
  }

  // If we're not a password editor, return error since this call does not
  // make sense.
  if (!IsPasswordEditor() || NS_WARN_IF(!mPasswordMaskData)) {
    mPasswordMaskData->CancelTimer(PasswordMaskData::ReleaseTimer::Yes);
    return NS_ERROR_NOT_AVAILABLE;
  }

  if (NS_WARN_IF(!GetRoot())) {
    return NS_ERROR_NOT_INITIALIZED;
  }
  Text* const text = GetTextNode();
  if (!text || !text->Length()) {
    // There is no anonymous text node in the editor.
    return aStart > 0 && aStart != UINT32_MAX ? NS_ERROR_INVALID_ARG : NS_OK;
  }

  if (aStart < UINT32_MAX) {
    uint32_t valueLength = text->Length();
    if (aStart >= valueLength) {
      return NS_ERROR_INVALID_ARG;  // There is no character can be masked.
    }
    // If aStart is middle of a surrogate pair, expand it to include the
    // preceding high surrogate because the caller may want to show a
    // character before the character at `aStart + 1`.
    const CharacterDataBuffer& characterDataBuffer = text->DataBuffer();
    if (characterDataBuffer.IsLowSurrogateFollowingHighSurrogateAt(aStart)) {
      mPasswordMaskData->mUnmaskedStart = aStart - 1;
      // If caller collapses the range, keep it.  Otherwise, expand the length.
      if (aLength > 0) {
        ++aLength;
      }
    } else {
      mPasswordMaskData->mUnmaskedStart = aStart;
    }
    mPasswordMaskData->mUnmaskedLength =
        std::min(valueLength - UnmaskedStart(), aLength);
    // If unmasked end is middle of a surrogate pair, expand it to include
    // the following low surrogate because the caller may want to show a
    // character after the character at `aStart + aLength`.
    if (UnmaskedEnd() < valueLength &&
        characterDataBuffer.IsLowSurrogateFollowingHighSurrogateAt(
            UnmaskedEnd())) {
      mPasswordMaskData->mUnmaskedLength++;
    }
    // If it's first time to mask the unmasking characters with timer, create
    // the timer now.  Then, we'll keep using it for saving the creation cost.
    if (!HasAutoMaskingTimer() && aLength && aTimeout && UnmaskedLength()) {
      mPasswordMaskData->mTimer = NS_NewTimer();
    }
  } else {
    if (NS_WARN_IF(aLength != 0)) {
      return NS_ERROR_INVALID_ARG;
    }
    mPasswordMaskData->MaskAll();
  }

  // Notify nsTextFrame of this update if the caller wants this to do it.
  // Only in this case, script may run.
  if (aNotify) {
    MOZ_ASSERT(IsEditActionDataAvailable());

    RefPtr<Document> document = GetDocument();
    if (NS_WARN_IF(!document)) {
      return NS_ERROR_NOT_INITIALIZED;
    }
    // Notify nsTextFrame of masking range change.
    if (RefPtr<PresShell> presShell = document->GetObservingPresShell()) {
      nsAutoScriptBlocker blockRunningScript;
      uint32_t valueLength = text->Length();
      CharacterDataChangeInfo changeInfo = {false, 0, valueLength, valueLength};
      presShell->CharacterDataChanged(text, changeInfo);
    }

    // Scroll caret into the view since masking or unmasking character may
    // move caret to outside of the view.
    nsresult rv = ScrollSelectionFocusIntoView();
    if (NS_FAILED(rv)) {
      NS_WARNING("EditorBase::ScrollSelectionFocusIntoView() failed");
      return rv;
    }
  }

  if (!IsAllMasked() && aTimeout != 0) {
    // Initialize the timer to mask the range automatically.
    MOZ_ASSERT(HasAutoMaskingTimer());
    DebugOnly<nsresult> rvIgnored = mPasswordMaskData->mTimer->InitWithCallback(
        this, aTimeout, nsITimer::TYPE_ONE_SHOT);
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rvIgnored),
                         "nsITimer::InitWithCallback() failed, but ignored");
  }

  return NS_OK;
}

// static
char16_t TextEditor::PasswordMask() {
  char16_t ret = LookAndFeel::GetPasswordCharacter();
  if (!ret) {
    ret = '*';
  }
  return ret;
}

MOZ_CAN_RUN_SCRIPT_BOUNDARY NS_IMETHODIMP
TextEditor::Notify(nsITimer* /*aTimer*/) {
  // Check whether our text editor's password flag was changed before this
  // "hide password character" timer actually fires.
  if (!IsPasswordEditor() || NS_WARN_IF(!mPasswordMaskData)) {
    return NS_OK;
  }

  if (IsAllMasked()) {
    return NS_OK;
  }

  AutoEditActionDataSetter editActionData(*this, EditAction::eHidePassword);
  if (NS_WARN_IF(!editActionData.CanHandle())) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  // Mask all characters.
  nsresult rv = MaskAllCharactersAndNotify();
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "TextEditor::MaskAllCharactersAndNotify() failed");

  if (StaticPrefs::editor_password_testing_mask_delay()) {
    if (RefPtr<Element> target = GetInputEventTargetElement()) {
      DebugOnly<nsresult> rvIgnored = nsContentUtils::DispatchTrustedEvent(
          target, u"MozLastInputMasked"_ns, CanBubble::eYes, Cancelable::eNo);
      NS_WARNING_ASSERTION(NS_SUCCEEDED(rvIgnored),
                           "nsContentUtils::DispatchTrustedEvent("
                           "MozLastInputMasked) failed, but ignored");
    }
  }

  return EditorBase::ToGenericNSResult(rv);
}

NS_IMETHODIMP TextEditor::GetName(nsACString& aName) {
  aName.AssignLiteral("TextEditor");
  return NS_OK;
}

void TextEditor::WillDeleteText(uint32_t /*aCurrentLength*/,
                                uint32_t aRemoveStartOffset,
                                uint32_t aRemoveLength) {
  MOZ_ASSERT(IsEditActionDataAvailable());

  if (!IsPasswordEditor() || NS_WARN_IF(!mPasswordMaskData) || IsAllMasked()) {
    return;
  }

  // Adjust unmasked range before deletion since DOM mutation may cause
  // layout referring the range in old text.

  // If we need to mask automatically, mask all now.
  if (IsMaskingPassword()) {
    DebugOnly<nsresult> rvIgnored = MaskAllCharacters();
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rvIgnored),
                         "TextEditor::MaskAllCharacters() failed, but ignored");
    return;
  }

  if (aRemoveStartOffset < UnmaskedStart()) {
    // If removing range is before the unmasked range, move it.
    if (aRemoveStartOffset + aRemoveLength <= UnmaskedStart()) {
      DebugOnly<nsresult> rvIgnored =
          SetUnmaskRange(UnmaskedStart() - aRemoveLength, UnmaskedLength());
      NS_WARNING_ASSERTION(NS_SUCCEEDED(rvIgnored),
                           "TextEditor::SetUnmaskRange() failed, but ignored");
      return;
    }

    // If removing range starts before unmasked range, and ends in unmasked
    // range, move and shrink the range.
    if (aRemoveStartOffset + aRemoveLength < UnmaskedEnd()) {
      uint32_t unmaskedLengthInRemovingRange =
          aRemoveStartOffset + aRemoveLength - UnmaskedStart();
      DebugOnly<nsresult> rvIgnored = SetUnmaskRange(
          aRemoveStartOffset, UnmaskedLength() - unmaskedLengthInRemovingRange);
      NS_WARNING_ASSERTION(NS_SUCCEEDED(rvIgnored),
                           "TextEditor::SetUnmaskRange() failed, but ignored");
      return;
    }

    // If removing range includes all unmasked range, collapse it to the
    // remove offset.
    DebugOnly<nsresult> rvIgnored = SetUnmaskRange(aRemoveStartOffset, 0);
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rvIgnored),
                         "TextEditor::SetUnmaskRange() failed, but ignored");
    return;
  }

  if (aRemoveStartOffset < UnmaskedEnd()) {
    // If removing range is in unmasked range, shrink the range.
    if (aRemoveStartOffset + aRemoveLength <= UnmaskedEnd()) {
      DebugOnly<nsresult> rvIgnored =
          SetUnmaskRange(UnmaskedStart(), UnmaskedLength() - aRemoveLength);
      NS_WARNING_ASSERTION(NS_SUCCEEDED(rvIgnored),
                           "TextEditor::SetUnmaskRange() failed, but ignored");
      return;
    }

    // If removing range starts from unmasked range, and ends after it,
    // shrink it.
    DebugOnly<nsresult> rvIgnored =
        SetUnmaskRange(UnmaskedStart(), aRemoveStartOffset - UnmaskedStart());
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rvIgnored),
                         "TextEditor::SetUnmaskRange() failed, but ignored");
    return;
  }

  // If removing range is after the unmasked range, keep it.
}

nsresult TextEditor::DidInsertText(uint32_t /*aNewLength*/,
                                   uint32_t aInsertedOffset,
                                   uint32_t aInsertedLength) {
  MOZ_ASSERT(IsEditActionDataAvailable());

  if (!IsPasswordEditor() || NS_WARN_IF(!mPasswordMaskData) || IsAllMasked()) {
    return NS_OK;
  }

  if (IsMaskingPassword()) {
    // If we need to mask password, mask all right now.
    nsresult rv = MaskAllCharactersAndNotify();
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                         "TextEditor::MaskAllCharacters() failed");
    return rv;
  }

  if (aInsertedOffset < UnmaskedStart()) {
    // If insertion point is before unmasked range, expand the unmasked range
    // to include the new text.
    nsresult rv = SetUnmaskRangeAndNotify(
        aInsertedOffset, UnmaskedEnd() + aInsertedLength - aInsertedOffset);
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                         "TextEditor::SetUnmaskRangeAndNotify() failed");
    return rv;
  }

  if (aInsertedOffset <= UnmaskedEnd()) {
    // If insertion point is in unmasked range, unmask new text.
    nsresult rv = SetUnmaskRangeAndNotify(UnmaskedStart(),
                                          UnmaskedLength() + aInsertedLength);
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                         "TextEditor::SetUnmaskRangeAndNotify() failed");
    return rv;
  }

  // If insertion point is after unmasked range, extend the unmask range to
  // include the new text.
  nsresult rv = SetUnmaskRangeAndNotify(
      UnmaskedStart(), aInsertedOffset + aInsertedLength - UnmaskedStart());
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "TextEditor::SetUnmaskRangeAndNotify() failed");
  return rv;
}

}  // namespace mozilla
