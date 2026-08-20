/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/TextInputProcessor.h"

#include "mozilla/AutoRestore.h"
#include "mozilla/EventForwards.h"
#include "mozilla/Maybe.h"
#include "mozilla/NativeKeyBindingsType.h"
#include "mozilla/StaticPrefs_test.h"
#include "mozilla/TextEventDispatcher.h"
#include "mozilla/TextEvents.h"
#include "mozilla/WritingModes.h"
#include "mozilla/dom/Event.h"
#include "mozilla/dom/KeyboardEvent.h"
#include "mozilla/widget/IMEData.h"
#include "nsContentUtils.h"
#include "nsIDocShell.h"
#include "nsIWidget.h"
#include "nsPIDOMWindow.h"
#include "nsPresContext.h"

using mozilla::dom::Event;
using mozilla::dom::KeyboardEvent;
using namespace mozilla::widget;

namespace mozilla {

/******************************************************************************
 * TextInputProcessorNotification
 ******************************************************************************/

class TextInputProcessorNotification final
    : public nsITextInputProcessorNotification {
  using SelectionChangeData = IMENotification::SelectionChangeData;
  using SelectionChangeDataBase = IMENotification::SelectionChangeDataBase;
  using TextChangeData = IMENotification::TextChangeData;
  using TextChangeDataBase = IMENotification::TextChangeDataBase;

 public:
  explicit TextInputProcessorNotification(const char* aType)
      : mType(aType), mTextChangeData() {}

  explicit TextInputProcessorNotification(
      const TextChangeDataBase& aTextChangeData)
      : mType("notify-text-change"), mTextChangeData(aTextChangeData) {}

  explicit TextInputProcessorNotification(
      const SelectionChangeDataBase& aSelectionChangeData)
      : mType("notify-selection-change"),
        mSelectionChangeData(aSelectionChangeData) {
    // SelectionChangeDataBase::mString still refers nsString instance owned
    // by aSelectionChangeData.  So, this needs to copy the instance.
    if (aSelectionChangeData.HasRange()) {
      mSelectionChangeData.mString =
          new nsString(aSelectionChangeData.String());
    } else {
      mSelectionChangeData.mString = nullptr;
    }
  }

  NS_DECL_ISUPPORTS

  NS_IMETHOD GetType(nsACString& aType) final {
    aType = mType;
    return NS_OK;
  }

  // "notify-text-change" and "notify-selection-change"
  NS_IMETHOD GetOffset(uint32_t* aOffset) final {
    if (NS_WARN_IF(!aOffset)) {
      return NS_ERROR_INVALID_ARG;
    }
    if (IsSelectionChange()) {
      if (!mSelectionChangeData.HasRange()) {
        return NS_ERROR_NOT_AVAILABLE;
      }
      *aOffset = mSelectionChangeData.mOffset;
      return NS_OK;
    }
    if (IsTextChange()) {
      *aOffset = mTextChangeData.mStartOffset;
      return NS_OK;
    }
    return NS_ERROR_NOT_AVAILABLE;
  }

  // "notify-selection-change"
  NS_IMETHOD GetHasRange(bool* aHasRange) final {
    if (IsSelectionChange()) {
      *aHasRange = mSelectionChangeData.HasRange();
      return NS_OK;
    }
    return NS_ERROR_NOT_AVAILABLE;
  }
  NS_IMETHOD GetText(nsAString& aText) final {
    if (IsSelectionChange()) {
      if (!mSelectionChangeData.HasRange()) {
        return NS_ERROR_NOT_AVAILABLE;
      }
      aText = mSelectionChangeData.String();
      return NS_OK;
    }
    return NS_ERROR_NOT_AVAILABLE;
  }

  NS_IMETHOD GetCollapsed(bool* aCollapsed) final {
    if (NS_WARN_IF(!aCollapsed)) {
      return NS_ERROR_INVALID_ARG;
    }
    if (IsSelectionChange()) {
      *aCollapsed = mSelectionChangeData.IsCollapsed();
      return NS_OK;
    }
    return NS_ERROR_NOT_AVAILABLE;
  }

  NS_IMETHOD GetLength(uint32_t* aLength) final {
    if (NS_WARN_IF(!aLength)) {
      return NS_ERROR_INVALID_ARG;
    }
    if (IsSelectionChange()) {
      if (!mSelectionChangeData.HasRange()) {
        return NS_ERROR_NOT_AVAILABLE;
      }
      *aLength = mSelectionChangeData.Length();
      return NS_OK;
    }
    return NS_ERROR_NOT_AVAILABLE;
  }

  NS_IMETHOD GetReversed(bool* aReversed) final {
    if (NS_WARN_IF(!aReversed)) {
      return NS_ERROR_INVALID_ARG;
    }
    if (IsSelectionChange()) {
      if (!mSelectionChangeData.HasRange()) {
        return NS_ERROR_NOT_AVAILABLE;
      }
      *aReversed = mSelectionChangeData.mReversed;
      return NS_OK;
    }
    return NS_ERROR_NOT_AVAILABLE;
  }

  NS_IMETHOD GetWritingMode(nsACString& aWritingMode) final {
    if (IsSelectionChange()) {
      WritingMode writingMode = mSelectionChangeData.GetWritingMode();
      if (!writingMode.IsVertical()) {
        aWritingMode.AssignLiteral("horizontal-tb");
      } else if (writingMode.IsVerticalLR()) {
        aWritingMode.AssignLiteral("vertical-lr");
      } else {
        aWritingMode.AssignLiteral("vertical-rl");
      }
      return NS_OK;
    }
    return NS_ERROR_NOT_AVAILABLE;
  }

  NS_IMETHOD GetCausedByComposition(bool* aCausedByComposition) final {
    if (NS_WARN_IF(!aCausedByComposition)) {
      return NS_ERROR_INVALID_ARG;
    }
    if (IsSelectionChange()) {
      *aCausedByComposition = mSelectionChangeData.mCausedByComposition;
      return NS_OK;
    }
    return NS_ERROR_NOT_AVAILABLE;
  }

  NS_IMETHOD GetCausedBySelectionEvent(bool* aCausedBySelectionEvent) final {
    if (NS_WARN_IF(!aCausedBySelectionEvent)) {
      return NS_ERROR_INVALID_ARG;
    }
    if (IsSelectionChange()) {
      *aCausedBySelectionEvent = mSelectionChangeData.mCausedBySelectionEvent;
      return NS_OK;
    }
    return NS_ERROR_NOT_AVAILABLE;
  }

  NS_IMETHOD GetOccurredDuringComposition(
      bool* aOccurredDuringComposition) final {
    if (NS_WARN_IF(!aOccurredDuringComposition)) {
      return NS_ERROR_INVALID_ARG;
    }
    if (IsSelectionChange()) {
      *aOccurredDuringComposition =
          mSelectionChangeData.mOccurredDuringComposition;
      return NS_OK;
    }
    return NS_ERROR_NOT_AVAILABLE;
  }

  // "notify-text-change"
  NS_IMETHOD GetRemovedLength(uint32_t* aLength) final {
    if (NS_WARN_IF(!aLength)) {
      return NS_ERROR_INVALID_ARG;
    }
    if (IsTextChange()) {
      *aLength = mTextChangeData.OldLength();
      return NS_OK;
    }
    return NS_ERROR_NOT_AVAILABLE;
  }

  NS_IMETHOD GetAddedLength(uint32_t* aLength) final {
    if (NS_WARN_IF(!aLength)) {
      return NS_ERROR_INVALID_ARG;
    }
    if (IsTextChange()) {
      *aLength = mTextChangeData.NewLength();
      return NS_OK;
    }
    return NS_ERROR_NOT_AVAILABLE;
  }

  NS_IMETHOD GetCausedOnlyByComposition(bool* aCausedOnlyByComposition) final {
    if (NS_WARN_IF(!aCausedOnlyByComposition)) {
      return NS_ERROR_INVALID_ARG;
    }
    if (IsTextChange()) {
      *aCausedOnlyByComposition = mTextChangeData.mCausedOnlyByComposition;
      return NS_OK;
    }
    return NS_ERROR_NOT_AVAILABLE;
  }

  NS_IMETHOD GetIncludingChangesDuringComposition(
      bool* aIncludingChangesDuringComposition) final {
    if (NS_WARN_IF(!aIncludingChangesDuringComposition)) {
      return NS_ERROR_INVALID_ARG;
    }
    if (IsTextChange()) {
      *aIncludingChangesDuringComposition =
          mTextChangeData.mIncludingChangesDuringComposition;
      return NS_OK;
    }
    return NS_ERROR_NOT_AVAILABLE;
  }

  NS_IMETHOD GetIncludingChangesWithoutComposition(
      bool* aIncludingChangesWithoutComposition) final {
    if (NS_WARN_IF(!aIncludingChangesWithoutComposition)) {
      return NS_ERROR_INVALID_ARG;
    }
    if (IsTextChange()) {
      *aIncludingChangesWithoutComposition =
          mTextChangeData.mIncludingChangesWithoutComposition;
      return NS_OK;
    }
    return NS_ERROR_NOT_AVAILABLE;
  }

 protected:
  virtual ~TextInputProcessorNotification() {
    if (IsSelectionChange() && mSelectionChangeData.mString) {
      delete mSelectionChangeData.mString;
      mSelectionChangeData.mString = nullptr;
    }
  }

  bool IsTextChange() const {
    return mType.EqualsLiteral("notify-text-change");
  }

  bool IsSelectionChange() const {
    return mType.EqualsLiteral("notify-selection-change");
  }

 private:
  nsAutoCString mType;
  union {
    TextChangeDataBase mTextChangeData;
    SelectionChangeDataBase mSelectionChangeData;
  };

  TextInputProcessorNotification() : mTextChangeData() {}
};

NS_IMPL_ISUPPORTS(TextInputProcessorNotification,
                  nsITextInputProcessorNotification)

/******************************************************************************
 * TextInputProcessor
 ******************************************************************************/

NS_IMPL_ISUPPORTS(TextInputProcessor, nsITextInputProcessor,
                  TextEventDispatcherListener, nsISupportsWeakReference)

TextInputProcessor::TextInputProcessor()
    : mDispatcher(nullptr), mForTests(false) {}

TextInputProcessor::~TextInputProcessor() {
  if (mDispatcher && mDispatcher->IsComposing()) {
    // If this is composing and not canceling the composition, nobody can steal
    // the rights of TextEventDispatcher from this instance.  Therefore, this
    // needs to cancel the composition here.
    if (NS_SUCCEEDED(IsValidStateForComposition())) {
      RefPtr<TextEventDispatcher> kungFuDeathGrip(mDispatcher);
      nsEventStatus status = nsEventStatus_eIgnore;
      kungFuDeathGrip->CommitComposition(status, &EmptyString());
    }
  }
}

bool TextInputProcessor::IsComposing() const {
  return mDispatcher && mDispatcher->IsComposing();
}

NS_IMETHODIMP
TextInputProcessor::GetHasComposition(bool* aHasComposition) {
  MOZ_RELEASE_ASSERT(aHasComposition, "aHasComposition must not be nullptr");
  MOZ_RELEASE_ASSERT(nsContentUtils::IsCallerChrome());
  *aHasComposition = IsComposing();
  return NS_OK;
}

NS_IMETHODIMP
TextInputProcessor::BeginInputTransaction(
    mozIDOMWindow* aWindow, nsITextInputProcessorCallback* aCallback,
    bool* aSucceeded) {
  MOZ_RELEASE_ASSERT(aSucceeded, "aSucceeded must not be nullptr");
  MOZ_RELEASE_ASSERT(nsContentUtils::IsCallerChrome());
  if (NS_WARN_IF(!aCallback)) {
    *aSucceeded = false;
    return NS_ERROR_INVALID_ARG;
  }
  return BeginInputTransactionInternal(aWindow, aCallback, false, *aSucceeded);
}

NS_IMETHODIMP
TextInputProcessor::BeginInputTransactionForTests(
    mozIDOMWindow* aWindow, nsITextInputProcessorCallback* aCallback,
    uint8_t aOptionalArgc, bool* aSucceeded) {
  MOZ_RELEASE_ASSERT(aSucceeded, "aSucceeded must not be nullptr");
  MOZ_RELEASE_ASSERT(nsContentUtils::IsCallerChrome());
  nsCOMPtr<nsITextInputProcessorCallback> callback =
      aOptionalArgc >= 1 ? aCallback : nullptr;
  return BeginInputTransactionInternal(aWindow, callback, true, *aSucceeded);
}

nsresult TextInputProcessor::BeginInputTransactionForFuzzing(
    nsPIDOMWindowInner* aWindow, nsITextInputProcessorCallback* aCallback,
    bool* aSucceeded) {
  MOZ_RELEASE_ASSERT(aSucceeded, "aSucceeded must not be nullptr");
  return BeginInputTransactionInternal(aWindow, aCallback, false, *aSucceeded);
}

nsresult TextInputProcessor::BeginInputTransactionInternal(
    mozIDOMWindow* aWindow, nsITextInputProcessorCallback* aCallback,
    bool aForTests, bool& aSucceeded) {
  aSucceeded = false;
  if (NS_WARN_IF(!aWindow)) {
    return NS_ERROR_INVALID_ARG;
  }
  nsCOMPtr<nsPIDOMWindowInner> pWindow = nsPIDOMWindowInner::From(aWindow);
  if (NS_WARN_IF(!pWindow)) {
    return NS_ERROR_INVALID_ARG;
  }
  nsCOMPtr<nsIDocShell> docShell(pWindow->GetDocShell());
  if (NS_WARN_IF(!docShell)) {
    return NS_ERROR_FAILURE;
  }
  RefPtr<nsPresContext> presContext = docShell->GetPresContext();
  if (NS_WARN_IF(!presContext)) {
    return NS_ERROR_FAILURE;
  }
  nsCOMPtr<nsIWidget> widget = presContext->GetRootWidget();
  if (NS_WARN_IF(!widget)) {
    return NS_ERROR_FAILURE;
  }

  RefPtr<TextEventDispatcher> dispatcher = widget->GetTextEventDispatcher();
  MOZ_RELEASE_ASSERT(dispatcher, "TextEventDispatcher must not be null");

  // If the instance was initialized and is being initialized for same
  // dispatcher and same purpose, we don't need to initialize the dispatcher
  // again.
  if (mDispatcher && dispatcher == mDispatcher && aCallback == mCallback &&
      aForTests == mForTests) {
    aSucceeded = true;
    return NS_OK;
  }

  // If this instance is composing or dispatching an event, don't allow to
  // initialize again.  Especially, if we allow to begin input transaction with
  // another TextEventDispatcher during dispatching an event, it may cause that
  // nobody cannot begin input transaction with it if the last event causes
  // opening modal dialog.
  if (mDispatcher &&
      (mDispatcher->IsComposing() || mDispatcher->IsDispatchingEvent())) {
    return NS_ERROR_ALREADY_INITIALIZED;
  }

  // And also if another instance is composing with the new dispatcher or
  // dispatching an event, it'll fail to steal its ownership.  Then, we should
  // not throw an exception, just return false.
  if (dispatcher->IsComposing() || dispatcher->IsDispatchingEvent()) {
    return NS_OK;
  }

  // This instance has finished preparing to link to the dispatcher.  Therefore,
  // let's forget the old dispatcher and purpose.
  if (mDispatcher) {
    OwningNonNull dispatcher = *mDispatcher;
    dispatcher->EndInputTransaction(this);
    if (NS_WARN_IF(mDispatcher)) {
      // Forcibly initialize the members if we failed to end the input
      // transaction.
      UnlinkFromTextEventDispatcher();
    }
  }

  nsresult rv = NS_OK;
  if (aForTests) {
    bool isAPZAware = StaticPrefs::test_events_async_enabled();
    rv = dispatcher->BeginTestInputTransaction(this, isAPZAware);
  } else {
    rv = dispatcher->BeginInputTransaction(this);
  }

  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  mDispatcher = dispatcher;
  mCallback = aCallback;
  mForTests = aForTests;
  aSucceeded = true;
  return NS_OK;
}

void TextInputProcessor::UnlinkFromTextEventDispatcher() {
  mDispatcher = nullptr;
  mForTests = false;
  if (mCallback) {
    nsCOMPtr<nsITextInputProcessorCallback> callback(mCallback);
    mCallback = nullptr;

    RefPtr<TextInputProcessorNotification> notification =
        new TextInputProcessorNotification("notify-end-input-transaction");
    bool result = false;
    callback->OnNotify(this, notification, &result);
  }
}

nsresult TextInputProcessor::IsValidStateForComposition() {
  if (NS_WARN_IF(!mDispatcher)) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  nsresult rv = mDispatcher->GetState();
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  return NS_OK;
}

bool TextInputProcessor::IsValidEventTypeForComposition(
    const WidgetKeyboardEvent& aKeyboardEvent) const {
  // The key event type of composition methods must be "", "keydown" or "keyup".
  if (aKeyboardEvent.mMessage == eKeyDown ||
      aKeyboardEvent.mMessage == eKeyUp) {
    return true;
  }
  if (aKeyboardEvent.mMessage == eUnidentifiedEvent &&
      aKeyboardEvent.mSpecifiedEventType &&
      nsDependentAtomString(aKeyboardEvent.mSpecifiedEventType)
          .EqualsLiteral("on")) {
    return true;
  }
  return false;
}

TextInputProcessor::EventDispatcherResult
TextInputProcessor::MaybeDispatchKeydownForComposition(
    const WidgetKeyboardEvent* aKeyboardEvent, uint32_t aKeyFlags) {
  EventDispatcherResult result;

  result.mResult = IsValidStateForComposition();
  if (NS_WARN_IF(NS_FAILED(result.mResult))) {
    result.mCanContinue = false;
    return result;
  }

  if (!aKeyboardEvent) {
    return result;
  }

  // If the mMessage is eKeyUp, the caller doesn't want TIP to dispatch
  // eKeyDown event.
  if (aKeyboardEvent->mMessage == eKeyUp) {
    return result;
  }

  // Modifier keys are not allowed because managing modifier state in this
  // method makes this messy.
  if (NS_WARN_IF(aKeyboardEvent->IsModifierKeyEvent())) {
    result.mResult = NS_ERROR_INVALID_ARG;
    result.mCanContinue = false;
    return result;
  }

  uint32_t consumedFlags = 0;

  result.mResult =
      KeydownInternal(*aKeyboardEvent, aKeyFlags, false, consumedFlags);
  result.mDoDefault = !consumedFlags;
  if (NS_WARN_IF(NS_FAILED(result.mResult))) {
    result.mCanContinue = false;
    return result;
  }

  result.mCanContinue = NS_SUCCEEDED(IsValidStateForComposition());
  return result;
}

TextInputProcessor::EventDispatcherResult
TextInputProcessor::MaybeDispatchKeyupForComposition(
    const WidgetKeyboardEvent* aKeyboardEvent, uint32_t aKeyFlags) {
  EventDispatcherResult result;

  if (!aKeyboardEvent) {
    return result;
  }

  // If the mMessage is eKeyDown, the caller doesn't want TIP to dispatch
  // eKeyUp event.
  if (aKeyboardEvent->mMessage == eKeyDown) {
    return result;
  }

  // If the widget has been destroyed, we can do nothing here.
  result.mResult = IsValidStateForComposition();
  if (NS_FAILED(result.mResult)) {
    result.mCanContinue = false;
    return result;
  }

  result.mResult = KeyupInternal(*aKeyboardEvent, aKeyFlags, result.mDoDefault);
  if (NS_WARN_IF(NS_FAILED(result.mResult))) {
    result.mCanContinue = false;
    return result;
  }

  result.mCanContinue = NS_SUCCEEDED(IsValidStateForComposition());
  return result;
}

nsresult TextInputProcessor::PrepareKeyboardEventForComposition(
    KeyboardEvent* aDOMKeyEvent, uint32_t& aKeyFlags, uint8_t aOptionalArgc,
    WidgetKeyboardEvent*& aKeyboardEvent) {
  aKeyboardEvent = nullptr;

  aKeyboardEvent = aOptionalArgc && aDOMKeyEvent
                       ? aDOMKeyEvent->WidgetEventPtr()->AsKeyboardEvent()
                       : nullptr;
  if (!aKeyboardEvent || aOptionalArgc < 2) {
    aKeyFlags = 0;
  }

  if (!aKeyboardEvent) {
    return NS_OK;
  }

  if (NS_WARN_IF(!IsValidEventTypeForComposition(*aKeyboardEvent))) {
    return NS_ERROR_INVALID_ARG;
  }

  return NS_OK;
}

NS_IMETHODIMP
TextInputProcessor::StartComposition(Event* aDOMKeyEvent, uint32_t aKeyFlags,
                                     uint8_t aOptionalArgc, bool* aSucceeded) {
  MOZ_RELEASE_ASSERT(aSucceeded, "aSucceeded must not be nullptr");
  MOZ_RELEASE_ASSERT(nsContentUtils::IsCallerChrome());
  *aSucceeded = false;

  RefPtr<KeyboardEvent> keyEvent;
  if (aDOMKeyEvent) {
    keyEvent = aDOMKeyEvent->AsKeyboardEvent();
    if (NS_WARN_IF(!keyEvent)) {
      return NS_ERROR_INVALID_ARG;
    }
  }

  RefPtr<TextEventDispatcher> kungFuDeathGrip(mDispatcher);

  WidgetKeyboardEvent* keyboardEvent;
  nsresult rv = PrepareKeyboardEventForComposition(
      keyEvent, aKeyFlags, aOptionalArgc, keyboardEvent);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  EventDispatcherResult dispatcherResult =
      MaybeDispatchKeydownForComposition(keyboardEvent, aKeyFlags);
  if (NS_WARN_IF(NS_FAILED(dispatcherResult.mResult)) ||
      !dispatcherResult.mCanContinue) {
    return dispatcherResult.mResult;
  }

  if (dispatcherResult.mDoDefault) {
    nsEventStatus status = nsEventStatus_eIgnore;
    rv = kungFuDeathGrip->StartComposition(status);
    *aSucceeded = status != nsEventStatus_eConsumeNoDefault &&
                  kungFuDeathGrip && kungFuDeathGrip->IsComposing();
  }

  MaybeDispatchKeyupForComposition(keyboardEvent, aKeyFlags);

  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }
  return NS_OK;
}

NS_IMETHODIMP
TextInputProcessor::SetPendingCompositionString(const nsAString& aString) {
  MOZ_RELEASE_ASSERT(nsContentUtils::IsCallerChrome());
  RefPtr<TextEventDispatcher> kungFuDeathGrip(mDispatcher);
  nsresult rv = IsValidStateForComposition();
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }
  return kungFuDeathGrip->SetPendingCompositionString(aString);
}

NS_IMETHODIMP
TextInputProcessor::AppendClauseToPendingComposition(uint32_t aLength,
                                                     uint32_t aAttribute) {
  MOZ_RELEASE_ASSERT(nsContentUtils::IsCallerChrome());
  RefPtr<TextEventDispatcher> kungFuDeathGrip(mDispatcher);
  TextRangeType textRangeType;
  switch (aAttribute) {
    case ATTR_RAW_CLAUSE:
    case ATTR_SELECTED_RAW_CLAUSE:
    case ATTR_CONVERTED_CLAUSE:
    case ATTR_SELECTED_CLAUSE:
      textRangeType = ToTextRangeType(aAttribute);
      break;
    default:
      return NS_ERROR_INVALID_ARG;
  }
  nsresult rv = IsValidStateForComposition();
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }
  return kungFuDeathGrip->AppendClauseToPendingComposition(aLength,
                                                           textRangeType);
}

NS_IMETHODIMP
TextInputProcessor::SetCaretInPendingComposition(uint32_t aOffset) {
  MOZ_RELEASE_ASSERT(nsContentUtils::IsCallerChrome());
  RefPtr<TextEventDispatcher> kungFuDeathGrip(mDispatcher);
  nsresult rv = IsValidStateForComposition();
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }
  return kungFuDeathGrip->SetCaretInPendingComposition(aOffset, 0);
}

NS_IMETHODIMP
TextInputProcessor::FlushPendingComposition(Event* aDOMKeyEvent,
                                            uint32_t aKeyFlags,
                                            uint8_t aOptionalArgc,
                                            bool* aSucceeded) {
  MOZ_RELEASE_ASSERT(aSucceeded, "aSucceeded must not be nullptr");
  MOZ_RELEASE_ASSERT(nsContentUtils::IsCallerChrome());

  // Even if this doesn't flush pending composition actually, we need to reset
  // pending composition for starting next composition with new user input.
  AutoPendingCompositionResetter resetter(this);

  *aSucceeded = false;
  RefPtr<TextEventDispatcher> kungFuDeathGrip(mDispatcher);
  bool wasComposing = IsComposing();

  RefPtr<KeyboardEvent> keyEvent;
  if (aDOMKeyEvent) {
    keyEvent = aDOMKeyEvent->AsKeyboardEvent();
    if (NS_WARN_IF(!keyEvent)) {
      return NS_ERROR_INVALID_ARG;
    }
  }

  WidgetKeyboardEvent* keyboardEvent;
  nsresult rv = PrepareKeyboardEventForComposition(
      keyEvent, aKeyFlags, aOptionalArgc, keyboardEvent);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  EventDispatcherResult dispatcherResult =
      MaybeDispatchKeydownForComposition(keyboardEvent, aKeyFlags);
  if (NS_WARN_IF(NS_FAILED(dispatcherResult.mResult)) ||
      !dispatcherResult.mCanContinue) {
    return dispatcherResult.mResult;
  }

  // Even if the preceding keydown event was consumed, if the composition
  // was already started, we shouldn't prevent the change of composition.
  if (dispatcherResult.mDoDefault || wasComposing) {
    // Preceding keydown event may cause destroying the widget.
    if (NS_WARN_IF(NS_FAILED(IsValidStateForComposition()))) {
      return NS_OK;
    }
    nsEventStatus status = nsEventStatus_eIgnore;
    rv = kungFuDeathGrip->FlushPendingComposition(status);
    NS_WARNING_ASSERTION(
        NS_SUCCEEDED(rv),
        "TextEventDispatcher::FlushPendingComposition() failed");
    *aSucceeded = status != nsEventStatus_eConsumeNoDefault;
  }

  MaybeDispatchKeyupForComposition(keyboardEvent, aKeyFlags);
  return rv;
}

NS_IMETHODIMP
TextInputProcessor::CommitComposition(Event* aDOMKeyEvent, uint32_t aKeyFlags,
                                      uint8_t aOptionalArgc) {
  MOZ_RELEASE_ASSERT(nsContentUtils::IsCallerChrome());

  RefPtr<KeyboardEvent> keyEvent;
  if (aDOMKeyEvent) {
    keyEvent = aDOMKeyEvent->AsKeyboardEvent();
    if (NS_WARN_IF(!keyEvent)) {
      return NS_ERROR_INVALID_ARG;
    }
  }

  WidgetKeyboardEvent* keyboardEvent;
  nsresult rv = PrepareKeyboardEventForComposition(
      keyEvent, aKeyFlags, aOptionalArgc, keyboardEvent);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  return CommitCompositionInternal(keyboardEvent, aKeyFlags);
}

NS_IMETHODIMP
TextInputProcessor::CommitCompositionWith(const nsAString& aCommitString,
                                          Event* aDOMKeyEvent,
                                          uint32_t aKeyFlags,
                                          uint8_t aOptionalArgc,
                                          bool* aSucceeded) {
  MOZ_RELEASE_ASSERT(aSucceeded, "aSucceeded must not be nullptr");
  MOZ_RELEASE_ASSERT(nsContentUtils::IsCallerChrome());

  RefPtr<KeyboardEvent> keyEvent;
  if (aDOMKeyEvent) {
    keyEvent = aDOMKeyEvent->AsKeyboardEvent();
    if (NS_WARN_IF(!keyEvent)) {
      return NS_ERROR_INVALID_ARG;
    }
  }

  WidgetKeyboardEvent* keyboardEvent;
  nsresult rv = PrepareKeyboardEventForComposition(
      keyEvent, aKeyFlags, aOptionalArgc, keyboardEvent);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  return CommitCompositionInternal(keyboardEvent, aKeyFlags, &aCommitString,
                                   aSucceeded);
}

nsresult TextInputProcessor::CommitCompositionInternal(
    const WidgetKeyboardEvent* aKeyboardEvent, uint32_t aKeyFlags,
    const nsAString* aCommitString, bool* aSucceeded) {
  if (aSucceeded) {
    *aSucceeded = false;
  }
  RefPtr<TextEventDispatcher> kungFuDeathGrip(mDispatcher);
  bool wasComposing = IsComposing();

  EventDispatcherResult dispatcherResult =
      MaybeDispatchKeydownForComposition(aKeyboardEvent, aKeyFlags);
  if (NS_WARN_IF(NS_FAILED(dispatcherResult.mResult)) ||
      !dispatcherResult.mCanContinue) {
    return dispatcherResult.mResult;
  }

  // Even if the preceding keydown event was consumed, if the composition
  // was already started, we shouldn't prevent the commit of composition.
  nsresult rv = NS_OK;
  if (dispatcherResult.mDoDefault || wasComposing) {
    // Preceding keydown event may cause destroying the widget.
    if (NS_FAILED(IsValidStateForComposition())) {
      return NS_OK;
    }
    nsEventStatus status = nsEventStatus_eIgnore;
    rv = kungFuDeathGrip->CommitComposition(status, aCommitString);
    if (aSucceeded) {
      *aSucceeded = status != nsEventStatus_eConsumeNoDefault;
    }
  }

  MaybeDispatchKeyupForComposition(aKeyboardEvent, aKeyFlags);

  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }
  return NS_OK;
}

NS_IMETHODIMP
TextInputProcessor::CancelComposition(Event* aDOMKeyEvent, uint32_t aKeyFlags,
                                      uint8_t aOptionalArgc) {
  MOZ_RELEASE_ASSERT(nsContentUtils::IsCallerChrome());

  RefPtr<KeyboardEvent> keyEvent;
  if (aDOMKeyEvent) {
    keyEvent = aDOMKeyEvent->AsKeyboardEvent();
    if (NS_WARN_IF(!keyEvent)) {
      return NS_ERROR_INVALID_ARG;
    }
  }

  WidgetKeyboardEvent* keyboardEvent;
  nsresult rv = PrepareKeyboardEventForComposition(
      keyEvent, aKeyFlags, aOptionalArgc, keyboardEvent);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  return CancelCompositionInternal(keyboardEvent, aKeyFlags);
}

nsresult TextInputProcessor::CancelCompositionInternal(
    const WidgetKeyboardEvent* aKeyboardEvent, uint32_t aKeyFlags) {
  RefPtr<TextEventDispatcher> kungFuDeathGrip(mDispatcher);

  EventDispatcherResult dispatcherResult =
      MaybeDispatchKeydownForComposition(aKeyboardEvent, aKeyFlags);
  if (NS_WARN_IF(NS_FAILED(dispatcherResult.mResult)) ||
      !dispatcherResult.mCanContinue) {
    return dispatcherResult.mResult;
  }

  nsEventStatus status = nsEventStatus_eIgnore;
  nsresult rv = kungFuDeathGrip->CommitComposition(status, &EmptyString());

  MaybeDispatchKeyupForComposition(aKeyboardEvent, aKeyFlags);

  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }
  return NS_OK;
}

NS_IMETHODIMP
TextInputProcessor::NotifyIME(TextEventDispatcher* aTextEventDispatcher,
                              const IMENotification& aNotification) {
  // If This is called while this is being initialized, ignore the call.
  // In such case, this method should return NS_ERROR_NOT_IMPLEMENTED because
  // we can say, TextInputProcessor doesn't implement any handlers of the
  // requests and notifications.
  if (!mDispatcher) {
    return NS_ERROR_NOT_IMPLEMENTED;
  }
  MOZ_ASSERT(aTextEventDispatcher == mDispatcher,
             "Wrong TextEventDispatcher notifies this");
  NS_ASSERTION(mForTests || mCallback,
               "mCallback can be null only when IME is initialized for tests");
  if (mCallback) {
    RefPtr<TextInputProcessorNotification> notification;
    switch (aNotification.mMessage) {
      case REQUEST_TO_COMMIT_COMPOSITION: {
        NS_ASSERTION(aTextEventDispatcher->IsComposing(),
                     "Why is this requested without composition?");
        notification = new TextInputProcessorNotification("request-to-commit");
        break;
      }
      case REQUEST_TO_CANCEL_COMPOSITION: {
        NS_ASSERTION(aTextEventDispatcher->IsComposing(),
                     "Why is this requested without composition?");
        notification = new TextInputProcessorNotification("request-to-cancel");
        break;
      }
      case NOTIFY_IME_OF_FOCUS:
        notification = new TextInputProcessorNotification("notify-focus");
        break;
      case NOTIFY_IME_OF_BLUR:
        notification = new TextInputProcessorNotification("notify-blur");
        break;
      case NOTIFY_IME_OF_TEXT_CHANGE:
        notification =
            new TextInputProcessorNotification(aNotification.mTextChangeData);
        break;
      case NOTIFY_IME_OF_SELECTION_CHANGE:
        notification = new TextInputProcessorNotification(
            aNotification.mSelectionChangeData);
        break;
      case NOTIFY_IME_OF_POSITION_CHANGE:
        notification =
            new TextInputProcessorNotification("notify-position-change");
        break;
      default:
        return NS_ERROR_NOT_IMPLEMENTED;
    }
    MOZ_RELEASE_ASSERT(notification);
    bool result = false;
    nsCOMPtr callback = mCallback;
    nsresult rv = callback->OnNotify(this, notification, &result);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }
    return result ? NS_OK : NS_ERROR_FAILURE;
  }

  switch (aNotification.mMessage) {
    case REQUEST_TO_COMMIT_COMPOSITION: {
      NS_ASSERTION(aTextEventDispatcher->IsComposing(),
                   "Why is this requested without composition?");
      CommitCompositionInternal();
      return NS_OK;
    }
    case REQUEST_TO_CANCEL_COMPOSITION: {
      NS_ASSERTION(aTextEventDispatcher->IsComposing(),
                   "Why is this requested without composition?");
      CancelCompositionInternal();
      return NS_OK;
    }
    default:
      return NS_ERROR_NOT_IMPLEMENTED;
  }
}

NS_IMETHODIMP_(IMENotificationRequests)
TextInputProcessor::GetIMENotificationRequests() {
  // TextInputProcessor should support all change notifications.
  return {IMENotificationRequest::TextChange,
          IMENotificationRequest::PositionChange};
}

NS_IMETHODIMP_(void)
TextInputProcessor::OnRemovedFrom(TextEventDispatcher* aTextEventDispatcher) {
  // If This is called while this is being initialized, ignore the call.
  if (!mDispatcher) {
    return;
  }
  MOZ_ASSERT(aTextEventDispatcher == mDispatcher,
             "Wrong TextEventDispatcher notifies this");
  UnlinkFromTextEventDispatcher();
}

NS_IMETHODIMP_(void)
TextInputProcessor::WillDispatchKeyboardEvent(
    TextEventDispatcher* aTextEventDispatcher,
    WidgetKeyboardEvent& aKeyboardEvent, uint32_t aIndexOfKeypress,
    void* aData) {
  // TextInputProcessor doesn't set alternative char code nor modify charCode
  // even when Ctrl key is pressed.
}

nsresult TextInputProcessor::PrepareKeyboardEventToDispatch(
    WidgetKeyboardEvent& aKeyboardEvent, uint32_t aKeyFlags) {
  if (NS_WARN_IF(aKeyboardEvent.mCodeNameIndex == CODE_NAME_INDEX_USE_STRING)) {
    return NS_ERROR_INVALID_ARG;
  }
  if ((aKeyFlags & KEY_NON_PRINTABLE_KEY) &&
      NS_WARN_IF(aKeyboardEvent.mKeyNameIndex == KEY_NAME_INDEX_USE_STRING)) {
    return NS_ERROR_INVALID_ARG;
  }
  if ((aKeyFlags & KEY_FORCE_PRINTABLE_KEY) &&
      aKeyboardEvent.mKeyNameIndex != KEY_NAME_INDEX_USE_STRING) {
    aKeyboardEvent.GetDOMKeyName(aKeyboardEvent.mKeyValue);
    aKeyboardEvent.mKeyNameIndex = KEY_NAME_INDEX_USE_STRING;
  }
  if (aKeyFlags & KEY_KEEP_KEY_LOCATION_STANDARD) {
    // If .location is initialized with specific value, using
    // KEY_KEEP_KEY_LOCATION_STANDARD must be a bug of the caller.
    // Let's throw an exception for notifying the developer of this bug.
    if (NS_WARN_IF(aKeyboardEvent.mLocation)) {
      return NS_ERROR_INVALID_ARG;
    }
  } else if (!aKeyboardEvent.mLocation) {
    // If KeyboardEvent.mLocation is 0, it may be uninitialized.  If so, we
    // should compute proper mLocation value from its .code value.
    aKeyboardEvent.mLocation =
        WidgetKeyboardEvent::ComputeLocationFromCodeValue(
            aKeyboardEvent.mCodeNameIndex);
  }

  if (aKeyFlags & KEY_KEEP_KEYCODE_ZERO) {
    // If .keyCode is initialized with specific value, using
    // KEY_KEEP_KEYCODE_ZERO must be a bug of the caller.  Let's throw an
    // exception for notifying the developer of such bug.
    if (NS_WARN_IF(aKeyboardEvent.mKeyCode)) {
      return NS_ERROR_INVALID_ARG;
    }
  } else if (!aKeyboardEvent.mKeyCode &&
             aKeyboardEvent.mKeyNameIndex > KEY_NAME_INDEX_Unidentified &&
             aKeyboardEvent.mKeyNameIndex < KEY_NAME_INDEX_USE_STRING) {
    // If KeyboardEvent.keyCode is 0, it may be uninitialized.  If so, we may
    // be able to decide a good .keyCode value if the .key value is a
    // non-printable key.
    aKeyboardEvent.mKeyCode =
        WidgetKeyboardEvent::ComputeKeyCodeFromKeyNameIndex(
            aKeyboardEvent.mKeyNameIndex);
  }

  aKeyboardEvent.mIsSynthesizedByTIP = true;
  aKeyboardEvent.mFlags.mIsSynthesizedForTests = mForTests;

  return NS_OK;
}

nsresult TextInputProcessor::InitEditCommands(
    WidgetKeyboardEvent& aKeyboardEvent) const {
  MOZ_ASSERT(XRE_IsContentProcess());
  MOZ_ASSERT(aKeyboardEvent.mMessage == eKeyPress);

  // When this emulates real input only in content process, we need to
  // initialize edit commands with the main process's widget via PuppetWidget
  // because they are initialized by BrowserParent before content process treats
  // them.
  // And also when this synthesizes keyboard events for tests, we need default
  // shortcut keys on the platform for making any developers get constant
  // results in any environments.

  // Note that retrieving edit commands via PuppetWidget is expensive.
  // Let's skip it when the keyboard event is inputting text.
  if (aKeyboardEvent.IsInputtingText()) {
    aKeyboardEvent.PreventNativeKeyBindings();
    return NS_OK;
  }

  Maybe<WritingMode> writingMode;
  if (RefPtr<TextEventDispatcher> dispatcher = mDispatcher) {
    writingMode = dispatcher->MaybeQueryWritingModeAtSelection();
  }

  // FYI: WidgetKeyboardEvent::InitAllEditCommands() isn't available here
  //      since it checks whether it's called in the main process to
  //      avoid performance issues so that we need to initialize each
  //      command manually here.
  if (NS_WARN_IF(!aKeyboardEvent.InitEditCommandsFor(
          NativeKeyBindingsType::SingleLineEditor, writingMode))) {
    return NS_ERROR_NOT_AVAILABLE;
  }
  if (NS_WARN_IF(!aKeyboardEvent.InitEditCommandsFor(
          NativeKeyBindingsType::MultiLineEditor, writingMode))) {
    return NS_ERROR_NOT_AVAILABLE;
  }
  if (NS_WARN_IF(!aKeyboardEvent.InitEditCommandsFor(
          NativeKeyBindingsType::RichTextEditor, writingMode))) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  return NS_OK;
}

NS_IMETHODIMP
TextInputProcessor::Keydown(Event* aDOMKeyEvent, uint32_t aKeyFlags,
                            uint8_t aOptionalArgc, uint32_t* aConsumedFlags) {
  MOZ_RELEASE_ASSERT(aConsumedFlags, "aConsumedFlags must not be nullptr");
  MOZ_RELEASE_ASSERT(nsContentUtils::IsCallerChrome());
  if (!aOptionalArgc) {
    aKeyFlags = 0;
  }
  if (NS_WARN_IF(!aDOMKeyEvent)) {
    return NS_ERROR_INVALID_ARG;
  }
  WidgetKeyboardEvent* originalKeyEvent =
      aDOMKeyEvent->WidgetEventPtr()->AsKeyboardEvent();
  if (NS_WARN_IF(!originalKeyEvent)) {
    return NS_ERROR_INVALID_ARG;
  }
  return KeydownInternal(*originalKeyEvent, aKeyFlags, true, *aConsumedFlags);
}

nsresult TextInputProcessor::Keydown(const WidgetKeyboardEvent& aKeyboardEvent,
                                     uint32_t aKeyFlags,
                                     uint32_t* aConsumedFlags) {
  uint32_t consumedFlags = 0;
  return KeydownInternal(aKeyboardEvent, aKeyFlags, true,
                         aConsumedFlags ? *aConsumedFlags : consumedFlags);
}

nsresult TextInputProcessor::KeydownInternal(
    const WidgetKeyboardEvent& aKeyboardEvent, uint32_t aKeyFlags,
    bool aAllowToDispatchKeypress, uint32_t& aConsumedFlags) {
  aConsumedFlags = KEYEVENT_NOT_CONSUMED;

  // We shouldn't modify the internal WidgetKeyboardEvent.
  WidgetKeyboardEvent keyEvent(aKeyboardEvent);
  keyEvent.mFlags.mIsTrusted = true;
  keyEvent.mMessage = eKeyDown;
  nsresult rv = PrepareKeyboardEventToDispatch(keyEvent, aKeyFlags);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  aConsumedFlags = (aKeyFlags & KEY_DEFAULT_PREVENTED) ? KEYDOWN_IS_CONSUMED
                                                       : KEYEVENT_NOT_CONSUMED;

  if (WidgetKeyboardEvent::GetModifierForKeyName(keyEvent.mKeyNameIndex)) {
    ModifierKeyData modifierKeyData(keyEvent);
    if (WidgetKeyboardEvent::IsLockableModifier(keyEvent.mKeyNameIndex)) {
      // If the modifier key is lockable modifier key such as CapsLock,
      // let's toggle modifier key state at keydown.
      ToggleModifierKey(modifierKeyData);
    } else {
      // Activate modifier flag before dispatching keydown event (i.e., keydown
      // event should indicate the releasing modifier is active.
      ActivateModifierKey(modifierKeyData);
    }
    if (aKeyFlags & KEY_DONT_DISPATCH_MODIFIER_KEY_EVENT) {
      return NS_OK;
    }
  } else if (NS_WARN_IF(aKeyFlags & KEY_DONT_DISPATCH_MODIFIER_KEY_EVENT)) {
    return NS_ERROR_INVALID_ARG;
  }
  keyEvent.mModifiers = GetActiveModifiers();

  if (!aAllowToDispatchKeypress &&
      !(aKeyFlags & KEY_DONT_MARK_KEYDOWN_AS_PROCESSED)) {
    keyEvent.mKeyCode = NS_VK_PROCESSKEY;
    keyEvent.mKeyNameIndex = KEY_NAME_INDEX_Process;
  }

  RefPtr<TextEventDispatcher> kungFuDeathGrip(mDispatcher);
  rv = IsValidStateForComposition();
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  nsEventStatus status =
      aConsumedFlags ? nsEventStatus_eConsumeNoDefault : nsEventStatus_eIgnore;
  if (!kungFuDeathGrip->DispatchKeyboardEvent(eKeyDown, keyEvent, status)) {
    // If keydown event isn't dispatched, we don't need to dispatch keypress
    // events.
    return NS_OK;
  }

  aConsumedFlags |= (status == nsEventStatus_eConsumeNoDefault)
                        ? KEYDOWN_IS_CONSUMED
                        : KEYEVENT_NOT_CONSUMED;

  if (!aAllowToDispatchKeypress) {
    return NS_OK;
  }

  keyEvent.mMessage = eKeyPress;

  // Only `eKeyPress` events, editor wants to execute system default edit
  // commands mapped to the key combination.  In e10s world, edit commands can
  // be retrieved only in the parent process due to the performance reason.
  // Therefore, BrowserParent initializes edit commands for all cases before
  // sending the event to focused content process.  For emulating this, we
  // need to do it now for synthesizing `eKeyPress` events if and only if
  // we're dispatching the events in a content process.
  if (XRE_IsContentProcess()) {
    nsresult rv = InitEditCommands(keyEvent);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }
  }
  if (kungFuDeathGrip->MaybeDispatchKeypressEvents(keyEvent, status)) {
    aConsumedFlags |= (status == nsEventStatus_eConsumeNoDefault)
                          ? KEYPRESS_IS_CONSUMED
                          : KEYEVENT_NOT_CONSUMED;
  }

  return NS_OK;
}

NS_IMETHODIMP
TextInputProcessor::Keyup(Event* aDOMKeyEvent, uint32_t aKeyFlags,
                          uint8_t aOptionalArgc, bool* aDoDefault) {
  MOZ_RELEASE_ASSERT(aDoDefault, "aDoDefault must not be nullptr");
  MOZ_RELEASE_ASSERT(nsContentUtils::IsCallerChrome());
  if (!aOptionalArgc) {
    aKeyFlags = 0;
  }
  if (NS_WARN_IF(!aDOMKeyEvent)) {
    return NS_ERROR_INVALID_ARG;
  }
  WidgetKeyboardEvent* originalKeyEvent =
      aDOMKeyEvent->WidgetEventPtr()->AsKeyboardEvent();
  if (NS_WARN_IF(!originalKeyEvent)) {
    return NS_ERROR_INVALID_ARG;
  }
  return KeyupInternal(*originalKeyEvent, aKeyFlags, *aDoDefault);
}

nsresult TextInputProcessor::Keyup(const WidgetKeyboardEvent& aKeyboardEvent,
                                   uint32_t aKeyFlags, bool* aDoDefault) {
  bool doDefault = false;
  return KeyupInternal(aKeyboardEvent, aKeyFlags,
                       aDoDefault ? *aDoDefault : doDefault);
}

nsresult TextInputProcessor::KeyupInternal(
    const WidgetKeyboardEvent& aKeyboardEvent, uint32_t aKeyFlags,
    bool& aDoDefault) {
  aDoDefault = false;

  // We shouldn't modify the internal WidgetKeyboardEvent.
  WidgetKeyboardEvent keyEvent(aKeyboardEvent);
  keyEvent.mFlags.mIsTrusted = true;
  keyEvent.mMessage = eKeyUp;
  nsresult rv = PrepareKeyboardEventToDispatch(keyEvent, aKeyFlags);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  aDoDefault = !(aKeyFlags & KEY_DEFAULT_PREVENTED);

  if (WidgetKeyboardEvent::GetModifierForKeyName(keyEvent.mKeyNameIndex)) {
    if (!WidgetKeyboardEvent::IsLockableModifier(keyEvent.mKeyNameIndex)) {
      // Inactivate modifier flag before dispatching keyup event (i.e., keyup
      // event shouldn't indicate the releasing modifier is active.
      InactivateModifierKey(ModifierKeyData(keyEvent));
    }
    if (aKeyFlags & KEY_DONT_DISPATCH_MODIFIER_KEY_EVENT) {
      return NS_OK;
    }
  } else if (NS_WARN_IF(aKeyFlags & KEY_DONT_DISPATCH_MODIFIER_KEY_EVENT)) {
    return NS_ERROR_INVALID_ARG;
  }
  keyEvent.mModifiers = GetActiveModifiers();

  if (aKeyFlags & KEY_MARK_KEYUP_AS_PROCESSED) {
    keyEvent.mKeyCode = NS_VK_PROCESSKEY;
    keyEvent.mKeyNameIndex = KEY_NAME_INDEX_Process;
  }

  RefPtr<TextEventDispatcher> kungFuDeathGrip(mDispatcher);
  rv = IsValidStateForComposition();
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  nsEventStatus status =
      aDoDefault ? nsEventStatus_eIgnore : nsEventStatus_eConsumeNoDefault;
  kungFuDeathGrip->DispatchKeyboardEvent(eKeyUp, keyEvent, status);
  aDoDefault = (status != nsEventStatus_eConsumeNoDefault);
  return NS_OK;
}

NS_IMETHODIMP
TextInputProcessor::InsertTextWithKeyPress(const nsAString& aString,
                                           Event* aDOMKeyEvent,
                                           uint32_t aKeyFlags,
                                           uint8_t aOptionalArgc,
                                           bool* aDoDefault) {
  MOZ_RELEASE_ASSERT(aDoDefault, "aDoDefault must not be nullptr");
  MOZ_RELEASE_ASSERT(nsContentUtils::IsCallerChrome());

  nsresult rv = IsValidStateForComposition();
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  if (aOptionalArgc < 1) {
    aDOMKeyEvent = nullptr;
  }
  if (aOptionalArgc < 2) {
    aKeyFlags = 0;
  }
  *aDoDefault = !(aKeyFlags & KEY_DEFAULT_PREVENTED);

  WidgetKeyboardEvent* const originalKeyEvent =
      aDOMKeyEvent ? aDOMKeyEvent->WidgetEventPtr()->AsKeyboardEvent()
                   : nullptr;
  if (NS_WARN_IF(aDOMKeyEvent && !originalKeyEvent)) {
    return NS_ERROR_INVALID_ARG;
  }

  WidgetKeyboardEvent keyEvent(true, eKeyPress, nullptr);
  if (originalKeyEvent) {
    keyEvent = WidgetKeyboardEvent(*originalKeyEvent);
    keyEvent.mFlags.mIsTrusted = true;
    keyEvent.mMessage = eKeyPress;
  }
  keyEvent.mKeyNameIndex = KEY_NAME_INDEX_USE_STRING;
  keyEvent.mKeyValue = aString;
  rv = PrepareKeyboardEventToDispatch(keyEvent, aKeyFlags);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }
  // Do not dispatch modifier key events even if the source event is a modifier
  // key event because modifier state should be changed before this.
  // TODO: In some test scenarios, we may need a new flag to use the given
  // modifier state as-is.
  keyEvent.mModifiers = GetActiveModifiers();

  // See KeyDownInternal() for the detail of this.
  if (XRE_IsContentProcess()) {
    nsresult rv = InitEditCommands(keyEvent);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }
  }

  nsEventStatus status =
      *aDoDefault ? nsEventStatus_eIgnore : nsEventStatus_eConsumeNoDefault;
  RefPtr<TextEventDispatcher> dispatcher(mDispatcher);
  if (dispatcher->MaybeDispatchKeypressEvents(keyEvent, status)) {
    *aDoDefault = (status != nsEventStatus_eConsumeNoDefault);
  }

  return NS_OK;
}

NS_IMETHODIMP
TextInputProcessor::GetModifierState(const nsAString& aModifierKeyName,
                                     bool* aActive) {
  MOZ_RELEASE_ASSERT(aActive, "aActive must not be null");
  MOZ_RELEASE_ASSERT(nsContentUtils::IsCallerChrome());
  Modifiers modifier = WidgetInputEvent::GetModifier(aModifierKeyName);
  *aActive = ((GetActiveModifiers() & modifier) != 0);
  return NS_OK;
}

NS_IMETHODIMP
TextInputProcessor::ShareModifierStateOf(nsITextInputProcessor* aOther) {
  MOZ_RELEASE_ASSERT(nsContentUtils::IsCallerChrome());
  if (!aOther) {
    mModifierKeyDataArray = nullptr;
    return NS_OK;
  }
  TextInputProcessor* other = static_cast<TextInputProcessor*>(aOther);
  if (!other->mModifierKeyDataArray) {
    other->mModifierKeyDataArray = new ModifierKeyDataArray();
  }
  mModifierKeyDataArray = other->mModifierKeyDataArray;
  return NS_OK;
}

NS_IMETHODIMP
TextInputProcessor::ComputeCodeValueOfNonPrintableKey(
    const nsAString& aKeyValue, JS::Handle<JS::Value> aLocation,
    uint8_t aOptionalArgc, nsAString& aCodeValue) {
  aCodeValue.Truncate();

  Maybe<uint32_t> location;
  if (aOptionalArgc) {
    if (aLocation.isNullOrUndefined()) {
      // location should be nothing.
    } else if (aLocation.isInt32()) {
      location = mozilla::Some(static_cast<uint32_t>(aLocation.toInt32()));
    } else {
      NS_WARNING_ASSERTION(aLocation.isNullOrUndefined() || aLocation.isInt32(),
                           "aLocation must be undefined, null or int");
      return NS_ERROR_INVALID_ARG;
    }
  }

  KeyNameIndex keyNameIndex = WidgetKeyboardEvent::GetKeyNameIndex(aKeyValue);
  if (keyNameIndex == KEY_NAME_INDEX_Unidentified ||
      keyNameIndex == KEY_NAME_INDEX_USE_STRING) {
    return NS_OK;
  }

  CodeNameIndex codeNameIndex =
      WidgetKeyboardEvent::ComputeCodeNameIndexFromKeyNameIndex(keyNameIndex,
                                                                location);
  if (codeNameIndex == CODE_NAME_INDEX_UNKNOWN) {
    return NS_OK;
  }
  MOZ_ASSERT(codeNameIndex != CODE_NAME_INDEX_USE_STRING);
  WidgetKeyboardEvent::GetDOMCodeName(codeNameIndex, aCodeValue);
  return NS_OK;
}

NS_IMETHODIMP
TextInputProcessor::GuessCodeValueOfPrintableKeyInUSEnglishKeyboardLayout(
    const nsAString& aKeyValue, JS::Handle<JS::Value> aLocation,
    uint8_t aOptionalArgc, nsAString& aCodeValue) {
  aCodeValue.Truncate();

  Maybe<uint32_t> location;
  if (aOptionalArgc) {
    if (aLocation.isNullOrUndefined()) {
      // location should be nothing.
    } else if (aLocation.isInt32()) {
      location = mozilla::Some(static_cast<uint32_t>(aLocation.toInt32()));
    } else {
      NS_WARNING_ASSERTION(aLocation.isNullOrUndefined() || aLocation.isInt32(),
                           "aLocation must be undefined, null or int");
      return NS_ERROR_INVALID_ARG;
    }
  }
  CodeNameIndex codeNameIndex =
      GuessCodeNameIndexOfPrintableKeyInUSEnglishLayout(aKeyValue, location);
  if (codeNameIndex == CODE_NAME_INDEX_UNKNOWN) {
    return NS_OK;
  }
  MOZ_ASSERT(codeNameIndex != CODE_NAME_INDEX_USE_STRING);
  WidgetKeyboardEvent::GetDOMCodeName(codeNameIndex, aCodeValue);
  return NS_OK;
}

// static
CodeNameIndex
TextInputProcessor::GuessCodeNameIndexOfPrintableKeyInUSEnglishLayout(
    const nsAString& aKeyValue, const Maybe<uint32_t>& aLocation) {
  if (aKeyValue.IsEmpty()) {
    return CODE_NAME_INDEX_UNKNOWN;
  }
  // US keyboard layout can input only one character per key.  So, we can
  // assume that if the key value is 2 or more characters, it's a known
  // key name or not a usual key emulation.
  if (aKeyValue.Length() > 1) {
    return CODE_NAME_INDEX_UNKNOWN;
  }
  if (aLocation.isSome() &&
      aLocation.value() ==
          dom::KeyboardEvent_Binding::DOM_KEY_LOCATION_NUMPAD) {
    switch (aKeyValue[0]) {
      case '+':
        return CODE_NAME_INDEX_NumpadAdd;
      case '-':
        return CODE_NAME_INDEX_NumpadSubtract;
      case '*':
        return CODE_NAME_INDEX_NumpadMultiply;
      case '/':
        return CODE_NAME_INDEX_NumpadDivide;
      case '.':
        return CODE_NAME_INDEX_NumpadDecimal;
      case '0':
        return CODE_NAME_INDEX_Numpad0;
      case '1':
        return CODE_NAME_INDEX_Numpad1;
      case '2':
        return CODE_NAME_INDEX_Numpad2;
      case '3':
        return CODE_NAME_INDEX_Numpad3;
      case '4':
        return CODE_NAME_INDEX_Numpad4;
      case '5':
        return CODE_NAME_INDEX_Numpad5;
      case '6':
        return CODE_NAME_INDEX_Numpad6;
      case '7':
        return CODE_NAME_INDEX_Numpad7;
      case '8':
        return CODE_NAME_INDEX_Numpad8;
      case '9':
        return CODE_NAME_INDEX_Numpad9;
      default:
        return CODE_NAME_INDEX_UNKNOWN;
    }
  }

  if (aLocation.isSome() &&
      aLocation.value() !=
          dom::KeyboardEvent_Binding::DOM_KEY_LOCATION_STANDARD) {
    return CODE_NAME_INDEX_UNKNOWN;
  }

  // TODO: Support characters inputted with option key on macOS.
  switch (aKeyValue[0]) {
    case 'a':
    case 'A':
      return CODE_NAME_INDEX_KeyA;
    case 'b':
    case 'B':
      return CODE_NAME_INDEX_KeyB;
    case 'c':
    case 'C':
      return CODE_NAME_INDEX_KeyC;
    case 'd':
    case 'D':
      return CODE_NAME_INDEX_KeyD;
    case 'e':
    case 'E':
      return CODE_NAME_INDEX_KeyE;
    case 'f':
    case 'F':
      return CODE_NAME_INDEX_KeyF;
    case 'g':
    case 'G':
      return CODE_NAME_INDEX_KeyG;
    case 'h':
    case 'H':
      return CODE_NAME_INDEX_KeyH;
    case 'i':
    case 'I':
      return CODE_NAME_INDEX_KeyI;
    case 'j':
    case 'J':
      return CODE_NAME_INDEX_KeyJ;
    case 'k':
    case 'K':
      return CODE_NAME_INDEX_KeyK;
    case 'l':
    case 'L':
      return CODE_NAME_INDEX_KeyL;
    case 'm':
    case 'M':
      return CODE_NAME_INDEX_KeyM;
    case 'n':
    case 'N':
      return CODE_NAME_INDEX_KeyN;
    case 'o':
    case 'O':
      return CODE_NAME_INDEX_KeyO;
    case 'p':
    case 'P':
      return CODE_NAME_INDEX_KeyP;
    case 'q':
    case 'Q':
      return CODE_NAME_INDEX_KeyQ;
    case 'r':
    case 'R':
      return CODE_NAME_INDEX_KeyR;
    case 's':
    case 'S':
      return CODE_NAME_INDEX_KeyS;
    case 't':
    case 'T':
      return CODE_NAME_INDEX_KeyT;
    case 'u':
    case 'U':
      return CODE_NAME_INDEX_KeyU;
    case 'v':
    case 'V':
      return CODE_NAME_INDEX_KeyV;
    case 'w':
    case 'W':
      return CODE_NAME_INDEX_KeyW;
    case 'x':
    case 'X':
      return CODE_NAME_INDEX_KeyX;
    case 'y':
    case 'Y':
      return CODE_NAME_INDEX_KeyY;
    case 'z':
    case 'Z':
      return CODE_NAME_INDEX_KeyZ;

    case '`':
    case '~':
      return CODE_NAME_INDEX_Backquote;
    case '1':
    case '!':
      return CODE_NAME_INDEX_Digit1;
    case '2':
    case '@':
      return CODE_NAME_INDEX_Digit2;
    case '3':
    case '#':
      return CODE_NAME_INDEX_Digit3;
    case '4':
    case '$':
      return CODE_NAME_INDEX_Digit4;
    case '5':
    case '%':
      return CODE_NAME_INDEX_Digit5;
    case '6':
    case '^':
      return CODE_NAME_INDEX_Digit6;
    case '7':
    case '&':
      return CODE_NAME_INDEX_Digit7;
    case '8':
    case '*':
      return CODE_NAME_INDEX_Digit8;
    case '9':
    case '(':
      return CODE_NAME_INDEX_Digit9;
    case '0':
    case ')':
      return CODE_NAME_INDEX_Digit0;
    case '-':
    case '_':
      return CODE_NAME_INDEX_Minus;
    case '=':
    case '+':
      return CODE_NAME_INDEX_Equal;

    case '[':
    case '{':
      return CODE_NAME_INDEX_BracketLeft;
    case ']':
    case '}':
      return CODE_NAME_INDEX_BracketRight;
    case '\\':
    case '|':
      return CODE_NAME_INDEX_Backslash;

    case ';':
    case ':':
      return CODE_NAME_INDEX_Semicolon;
    case '\'':
    case '"':
      return CODE_NAME_INDEX_Quote;

    case ',':
    case '<':
      return CODE_NAME_INDEX_Comma;
    case '.':
    case '>':
      return CODE_NAME_INDEX_Period;
    case '/':
    case '?':
      return CODE_NAME_INDEX_Slash;

    case ' ':
      return CODE_NAME_INDEX_Space;

    default:
      return CODE_NAME_INDEX_UNKNOWN;
  }
}

NS_IMETHODIMP
TextInputProcessor::GuessKeyCodeValueOfPrintableKeyInUSEnglishKeyboardLayout(
    const nsAString& aKeyValue, JS::Handle<JS::Value> aLocation,
    uint8_t aOptionalArgc, uint32_t* aKeyCodeValue) {
  if (NS_WARN_IF(!aKeyCodeValue)) {
    return NS_ERROR_INVALID_ARG;
  }

  Maybe<uint32_t> location;
  if (aOptionalArgc) {
    if (aLocation.isNullOrUndefined()) {
      // location should be nothing.
    } else if (aLocation.isInt32()) {
      location = mozilla::Some(static_cast<uint32_t>(aLocation.toInt32()));
    } else {
      NS_WARNING_ASSERTION(aLocation.isNullOrUndefined() || aLocation.isInt32(),
                           "aLocation must be undefined, null or int");
      return NS_ERROR_INVALID_ARG;
    }
  }

  *aKeyCodeValue =
      GuessKeyCodeOfPrintableKeyInUSEnglishLayout(aKeyValue, location);
  return NS_OK;
}

// static
uint32_t TextInputProcessor::GuessKeyCodeOfPrintableKeyInUSEnglishLayout(
    const nsAString& aKeyValue, const Maybe<uint32_t>& aLocation) {
  if (aKeyValue.IsEmpty()) {
    return 0;
  }
  // US keyboard layout can input only one character per key.  So, we can
  // assume that if the key value is 2 or more characters, it's a known
  // key name of a non-printable key or not a usual key emulation.
  if (aKeyValue.Length() > 1) {
    return 0;
  }

  if (aLocation.isSome() &&
      aLocation.value() ==
          dom::KeyboardEvent_Binding::DOM_KEY_LOCATION_NUMPAD) {
    switch (aKeyValue[0]) {
      case '+':
        return dom::KeyboardEvent_Binding::DOM_VK_ADD;
      case '-':
        return dom::KeyboardEvent_Binding::DOM_VK_SUBTRACT;
      case '*':
        return dom::KeyboardEvent_Binding::DOM_VK_MULTIPLY;
      case '/':
        return dom::KeyboardEvent_Binding::DOM_VK_DIVIDE;
      case '.':
        return dom::KeyboardEvent_Binding::DOM_VK_DECIMAL;
      case '0':
        return dom::KeyboardEvent_Binding::DOM_VK_NUMPAD0;
      case '1':
        return dom::KeyboardEvent_Binding::DOM_VK_NUMPAD1;
      case '2':
        return dom::KeyboardEvent_Binding::DOM_VK_NUMPAD2;
      case '3':
        return dom::KeyboardEvent_Binding::DOM_VK_NUMPAD3;
      case '4':
        return dom::KeyboardEvent_Binding::DOM_VK_NUMPAD4;
      case '5':
        return dom::KeyboardEvent_Binding::DOM_VK_NUMPAD5;
      case '6':
        return dom::KeyboardEvent_Binding::DOM_VK_NUMPAD6;
      case '7':
        return dom::KeyboardEvent_Binding::DOM_VK_NUMPAD7;
      case '8':
        return dom::KeyboardEvent_Binding::DOM_VK_NUMPAD8;
      case '9':
        return dom::KeyboardEvent_Binding::DOM_VK_NUMPAD9;
      default:
        return 0;
    }
  }

  if (aLocation.isSome() &&
      aLocation.value() !=
          dom::KeyboardEvent_Binding::DOM_KEY_LOCATION_STANDARD) {
    return 0;
  }

  // TODO: Support characters inputted with option key on macOS.
  switch (aKeyValue[0]) {
    case 'a':
    case 'A':
      return dom::KeyboardEvent_Binding::DOM_VK_A;
    case 'b':
    case 'B':
      return dom::KeyboardEvent_Binding::DOM_VK_B;
    case 'c':
    case 'C':
      return dom::KeyboardEvent_Binding::DOM_VK_C;
    case 'd':
    case 'D':
      return dom::KeyboardEvent_Binding::DOM_VK_D;
    case 'e':
    case 'E':
      return dom::KeyboardEvent_Binding::DOM_VK_E;
    case 'f':
    case 'F':
      return dom::KeyboardEvent_Binding::DOM_VK_F;
    case 'g':
    case 'G':
      return dom::KeyboardEvent_Binding::DOM_VK_G;
    case 'h':
    case 'H':
      return dom::KeyboardEvent_Binding::DOM_VK_H;
    case 'i':
    case 'I':
      return dom::KeyboardEvent_Binding::DOM_VK_I;
    case 'j':
    case 'J':
      return dom::KeyboardEvent_Binding::DOM_VK_J;
    case 'k':
    case 'K':
      return dom::KeyboardEvent_Binding::DOM_VK_K;
    case 'l':
    case 'L':
      return dom::KeyboardEvent_Binding::DOM_VK_L;
    case 'm':
    case 'M':
      return dom::KeyboardEvent_Binding::DOM_VK_M;
    case 'n':
    case 'N':
      return dom::KeyboardEvent_Binding::DOM_VK_N;
    case 'o':
    case 'O':
      return dom::KeyboardEvent_Binding::DOM_VK_O;
    case 'p':
    case 'P':
      return dom::KeyboardEvent_Binding::DOM_VK_P;
    case 'q':
    case 'Q':
      return dom::KeyboardEvent_Binding::DOM_VK_Q;
    case 'r':
    case 'R':
      return dom::KeyboardEvent_Binding::DOM_VK_R;
    case 's':
    case 'S':
      return dom::KeyboardEvent_Binding::DOM_VK_S;
    case 't':
    case 'T':
      return dom::KeyboardEvent_Binding::DOM_VK_T;
    case 'u':
    case 'U':
      return dom::KeyboardEvent_Binding::DOM_VK_U;
    case 'v':
    case 'V':
      return dom::KeyboardEvent_Binding::DOM_VK_V;
    case 'w':
    case 'W':
      return dom::KeyboardEvent_Binding::DOM_VK_W;
    case 'x':
    case 'X':
      return dom::KeyboardEvent_Binding::DOM_VK_X;
    case 'y':
    case 'Y':
      return dom::KeyboardEvent_Binding::DOM_VK_Y;
    case 'z':
    case 'Z':
      return dom::KeyboardEvent_Binding::DOM_VK_Z;

    case '`':
    case '~':
      return dom::KeyboardEvent_Binding::DOM_VK_BACK_QUOTE;
    case '1':
    case '!':
      return dom::KeyboardEvent_Binding::DOM_VK_1;
    case '2':
    case '@':
      return dom::KeyboardEvent_Binding::DOM_VK_2;
    case '3':
    case '#':
      return dom::KeyboardEvent_Binding::DOM_VK_3;
    case '4':
    case '$':
      return dom::KeyboardEvent_Binding::DOM_VK_4;
    case '5':
    case '%':
      return dom::KeyboardEvent_Binding::DOM_VK_5;
    case '6':
    case '^':
      return dom::KeyboardEvent_Binding::DOM_VK_6;
    case '7':
    case '&':
      return dom::KeyboardEvent_Binding::DOM_VK_7;
    case '8':
    case '*':
      return dom::KeyboardEvent_Binding::DOM_VK_8;
    case '9':
    case '(':
      return dom::KeyboardEvent_Binding::DOM_VK_9;
    case '0':
    case ')':
      return dom::KeyboardEvent_Binding::DOM_VK_0;
    case '-':
    case '_':
      return dom::KeyboardEvent_Binding::DOM_VK_HYPHEN_MINUS;
    case '=':
    case '+':
      return dom::KeyboardEvent_Binding::DOM_VK_EQUALS;

    case '[':
    case '{':
      return dom::KeyboardEvent_Binding::DOM_VK_OPEN_BRACKET;
    case ']':
    case '}':
      return dom::KeyboardEvent_Binding::DOM_VK_CLOSE_BRACKET;
    case '\\':
    case '|':
      return dom::KeyboardEvent_Binding::DOM_VK_BACK_SLASH;

    case ';':
    case ':':
      return dom::KeyboardEvent_Binding::DOM_VK_SEMICOLON;
    case '\'':
    case '"':
      return dom::KeyboardEvent_Binding::DOM_VK_QUOTE;

    case ',':
    case '<':
      return dom::KeyboardEvent_Binding::DOM_VK_COMMA;
    case '.':
    case '>':
      return dom::KeyboardEvent_Binding::DOM_VK_PERIOD;
    case '/':
    case '?':
      return dom::KeyboardEvent_Binding::DOM_VK_SLASH;

    case ' ':
      return dom::KeyboardEvent_Binding::DOM_VK_SPACE;

    default:
      return 0;
  }
}

/******************************************************************************
 * TextInputProcessor::AutoPendingCompositionResetter
 ******************************************************************************/
TextInputProcessor::AutoPendingCompositionResetter::
    AutoPendingCompositionResetter(TextInputProcessor* aTIP)
    : mTIP(aTIP) {
  MOZ_RELEASE_ASSERT(mTIP.get(), "mTIP must not be null");
}

TextInputProcessor::AutoPendingCompositionResetter::
    ~AutoPendingCompositionResetter() {
  if (mTIP->mDispatcher) {
    mTIP->mDispatcher->ClearPendingComposition();
  }
}

/******************************************************************************
 * TextInputProcessor::ModifierKeyData
 ******************************************************************************/
TextInputProcessor::ModifierKeyData::ModifierKeyData(
    const WidgetKeyboardEvent& aKeyboardEvent)
    : mKeyNameIndex(aKeyboardEvent.mKeyNameIndex),
      mCodeNameIndex(aKeyboardEvent.mCodeNameIndex) {
  mModifier = WidgetKeyboardEvent::GetModifierForKeyName(mKeyNameIndex);
  MOZ_ASSERT(mModifier, "mKeyNameIndex must be a modifier key name");
}

/******************************************************************************
 * TextInputProcessor::ModifierKeyDataArray
 ******************************************************************************/
Modifiers TextInputProcessor::ModifierKeyDataArray::GetActiveModifiers() const {
  Modifiers result = MODIFIER_NONE;
  for (uint32_t i = 0; i < Length(); i++) {
    result |= ElementAt(i).mModifier;
  }
  return result;
}

void TextInputProcessor::ModifierKeyDataArray::ActivateModifierKey(
    const TextInputProcessor::ModifierKeyData& aModifierKeyData) {
  if (Contains(aModifierKeyData)) {
    return;
  }
  AppendElement(aModifierKeyData);
}

void TextInputProcessor::ModifierKeyDataArray::InactivateModifierKey(
    const TextInputProcessor::ModifierKeyData& aModifierKeyData) {
  RemoveElement(aModifierKeyData);
}

void TextInputProcessor::ModifierKeyDataArray::ToggleModifierKey(
    const TextInputProcessor::ModifierKeyData& aModifierKeyData) {
  auto index = IndexOf(aModifierKeyData);
  if (index == NoIndex) {
    AppendElement(aModifierKeyData);
    return;
  }
  RemoveElementAt(index);
}

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(TextInputProcessorListener)
  NS_INTERFACE_MAP_ENTRY(nsITextInputProcessorListener)
  NS_INTERFACE_MAP_ENTRY(nsITextInputProcessorCallback)
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END
NS_IMPL_CYCLE_COLLECTING_ADDREF(TextInputProcessorListener)
NS_IMPL_CYCLE_COLLECTING_RELEASE(TextInputProcessorListener)
NS_IMPL_CYCLE_COLLECTION(TextInputProcessorListener, mCallback)

NS_IMETHODIMP TextInputProcessorListener::OnNotify(
    nsITextInputProcessor* aTextInputProcessor,
    nsITextInputProcessorNotification* aNotification, bool* aRetVal) {
  if (!mCallback) {
    return NS_OK;
  }
  AutoRestore restore(mNotification);
  mNotification = aNotification;
  nsCOMPtr callback = mCallback;
  return callback->OnNotify(aRetVal);
}

NS_IMETHODIMP TextInputProcessorListener::GetNotification(
    nsITextInputProcessorNotification** aRetVal) {
  NS_IF_ADDREF(*aRetVal = mNotification);
  return NS_OK;
}

NS_IMETHODIMP TextInputProcessorListener::SetCallback(
    nsITextInputProcessorListenerCallback* aCallback) {
  mCallback = aCallback;
  return NS_OK;
}

}  // namespace mozilla
