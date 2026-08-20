/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "TextEventDispatcher.h"

#include "IMEData.h"
#include "PuppetWidget.h"
#include "TextEvents.h"
#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/Utf16.h"
#include "nsCharTraits.h"
#include "nsIFrame.h"
#include "nsIWidget.h"

namespace mozilla {
namespace widget {

/******************************************************************************
 * TextEventDispatcher
 *****************************************************************************/
TextEventDispatcher::TextEventDispatcher(nsIWidget* aWidget)
    : mWidget(aWidget),
      mInputTransactionType(eNoInputTransaction),
      mIsComposing(false),
      mIsHandlingComposition(false),
      mHasFocus(false) {
  MOZ_RELEASE_ASSERT(mWidget, "aWidget must not be nullptr");

  ClearNotificationRequests();
}

nsresult TextEventDispatcher::BeginInputTransaction(
    TextEventDispatcherListener* aListener) {
  return BeginInputTransactionInternal(aListener,
                                       eSameProcessSyncInputTransaction);
}

nsresult TextEventDispatcher::BeginTestInputTransaction(
    TextEventDispatcherListener* aListener, bool aIsAPZAware) {
  return BeginInputTransactionInternal(
      aListener, aIsAPZAware ? eAsyncTestInputTransaction
                             : eSameProcessSyncTestInputTransaction);
}

nsresult TextEventDispatcher::BeginNativeInputTransaction() {
  if (NS_WARN_IF(!mWidget)) {
    return NS_ERROR_FAILURE;
  }
  RefPtr<TextEventDispatcherListener> listener =
      mWidget->GetNativeTextEventDispatcherListener();
  if (NS_WARN_IF(!listener)) {
    return NS_ERROR_FAILURE;
  }
  return BeginInputTransactionInternal(listener, eNativeInputTransaction);
}

nsresult TextEventDispatcher::BeginInputTransactionInternal(
    TextEventDispatcherListener* aListener, InputTransactionType aType) {
  if (NS_WARN_IF(!aListener)) {
    return NS_ERROR_INVALID_ARG;
  }
  nsCOMPtr<TextEventDispatcherListener> listener = do_QueryReferent(mListener);
  if (listener) {
    if (listener == aListener && mInputTransactionType == aType) {
      UpdateNotificationRequests();
      return NS_OK;
    }
    // If this has composition or is dispatching an event, any other listener
    // can steal ownership.  Especially, if the latter case is allowed,
    // nobody cannot begin input transaction with this if a modal dialog is
    // opened during dispatching an event.
    if (IsComposing() || IsDispatchingEvent()) {
      return NS_ERROR_ALREADY_INITIALIZED;
    }
  }
  mListener = do_GetWeakReference(aListener);
  mInputTransactionType = aType;
  if (listener && listener != aListener) {
    listener->OnRemovedFrom(this);
  }
  UpdateNotificationRequests();
  return NS_OK;
}

nsresult TextEventDispatcher::BeginInputTransactionFor(
    const WidgetGUIEvent* aEvent, PuppetWidget* aPuppetWidget) {
  // If the event is dispatched by this instance, we need to do nothing.
  if (IsDispatching(*aEvent)) {
    return NS_OK;
  }

  // If we're the dispatcher for native text input, we shouldn't begin new
  // transaction nor manage composition state here unless we're dispatching a
  // synthesized event for tests because the owner must use the methods for the
  // parent process in the case.
  if (aPuppetWidget->HasExternalNativeTextEventDispatcherListener() &&
      !aEvent->mFlags.mIsSynthesizedForTests) {
    return NS_OK;
  }

  switch (aEvent->mMessage) {
    case eKeyDown:
    case eKeyPress:
    case eKeyUp:
      MOZ_ASSERT(aEvent->mClass == eKeyboardEventClass);
      break;
    case eCompositionStart:
    case eCompositionChange:
    case eCompositionCommit:
    case eCompositionCommitAsIs:
      MOZ_ASSERT(aEvent->mClass == eCompositionEventClass);
      break;
    default:
      return NS_ERROR_INVALID_ARG;
  }

  const nsresult rv = [&]() {
    // We don't dispatch events via TextEventDispatcher::DispatchEvent(), but
    // let's check it.
    if (IsDispatchingEvent()) [[unlikely]] {
      return NS_OK;
    }

    if (!aEvent->mFlags.mIsSynthesizedForTests) {
      nsresult rv = BeginNativeInputTransaction();
      NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                           "BeginNativeInputTransaction() failed");
      return rv;
    }
    // If the event is for an automated test and this instance dispatched
    // an event to the parent process, we can assume that this is already
    // initialized properly.
    if (mInputTransactionType == eAsyncTestInputTransaction &&
        !aEvent->CameFromAnotherProcess()) {
      return NS_OK;
    }
    // Even if the event is a synthesized for tests and coming from the parent
    // process, this process should treat it as "sync" test here because it
    // won't be go back to the parent process.
    nsresult rv = BeginInputTransactionInternal(
        static_cast<TextEventDispatcherListener*>(aPuppetWidget),
        eSameProcessSyncTestInputTransaction);
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                         "BeginInputTransactionInternal("
                         "eSameProcessSyncTestInputTransaction) failed");
    return rv;
  }();

  // Emulate modifying members which indicate the state of composition.
  // If we need to manage more states and/or more complexly, we should create
  // internal methods which are called by both here and each event dispatcher
  // method of this class.
  switch (aEvent->mMessage) {
    case eKeyDown:
    case eKeyPress:
    case eKeyUp:
      return rv;
    case eCompositionStart:
      MOZ_ASSERT(!mIsComposing);
      mIsComposing = mIsHandlingComposition = true;
      return rv;
    case eCompositionChange:
      MOZ_ASSERT(mIsComposing);
      MOZ_ASSERT(mIsHandlingComposition);
      mIsComposing = mIsHandlingComposition = true;
      return rv;
    case eCompositionCommit:
    case eCompositionCommitAsIs:
      MOZ_ASSERT(mIsComposing);
      MOZ_ASSERT(mIsHandlingComposition);
      mIsComposing = false;
      mIsHandlingComposition = true;
      return rv;
    default:
      MOZ_ASSERT_UNREACHABLE("You forgot to handle the event");
      return NS_ERROR_UNEXPECTED;
  }
}
void TextEventDispatcher::EndInputTransaction(
    TextEventDispatcherListener* aListener) {
  if (NS_WARN_IF(IsComposing()) || NS_WARN_IF(IsDispatchingEvent())) {
    return;
  }

  mInputTransactionType = eNoInputTransaction;

  nsCOMPtr<TextEventDispatcherListener> listener = do_QueryReferent(mListener);
  if (NS_WARN_IF(!listener)) {
    return;
  }

  if (NS_WARN_IF(listener != aListener)) {
    return;
  }

  mListener = nullptr;
  listener->OnRemovedFrom(this);
  UpdateNotificationRequests();
}

void TextEventDispatcher::OnDestroyWidget() {
  mWidget = nullptr;
  mHasFocus = false;
  ClearNotificationRequests();
  mPendingComposition.Clear();
  nsCOMPtr<TextEventDispatcherListener> listener = do_QueryReferent(mListener);
  mListener = nullptr;
  mWritingMode.reset();
  mInputTransactionType = eNoInputTransaction;
  if (listener) {
    listener->OnRemovedFrom(this);
  }
}

nsresult TextEventDispatcher::GetState() const {
  nsCOMPtr<TextEventDispatcherListener> listener = do_QueryReferent(mListener);
  if (!listener) {
    return NS_ERROR_NOT_INITIALIZED;
  }
  if (!mWidget || mWidget->Destroyed()) {
    return NS_ERROR_NOT_AVAILABLE;
  }
  return NS_OK;
}

void TextEventDispatcher::InitEvent(WidgetGUIEvent& aEvent) const {
  aEvent.mRefPoint = LayoutDeviceIntPoint(0, 0);
  aEvent.mFlags.mIsSynthesizedForTests = IsForTests();
  if (aEvent.mClass != eCompositionEventClass) {
    return;
  }
  void* pseudoIMEContext = GetPseudoIMEContext();
  if (pseudoIMEContext) {
    aEvent.AsCompositionEvent()->mNativeIMEContext.InitWithRawNativeIMEContext(
        pseudoIMEContext);
  }
#ifdef DEBUG
  else {
    MOZ_ASSERT(!XRE_IsContentProcess(),
               "Why did the content process start native event transaction?");
    MOZ_ASSERT(aEvent.AsCompositionEvent()->mNativeIMEContext.IsValid(),
               "Native IME context shouldn't be invalid");
  }
#endif  // #ifdef DEBUG
}

Maybe<WritingMode> TextEventDispatcher::MaybeQueryWritingModeAtSelection()
    const {
  if (mHasFocus || mWritingMode.isSome()) {
    return mWritingMode;
  }

  if (NS_WARN_IF(!mWidget)) {
    return Nothing();
  }

  // If a remote content has focus and IME does not have focus, it's going to
  // fail eQuerySelectedText in ContentCacheParent.  For avoiding to waste
  // unnecessary runtime cost and to prevent unnecessary warnings, we should
  // not dispatch the event in the case.
  const InputContext inputContext = mWidget->GetInputContext();
  if (XRE_IsE10sParentProcess() && inputContext.IsOriginContentProcess() &&
      !inputContext.mIMEState.IsEditable()) {
    return Nothing();
  }

  WidgetQueryContentEvent querySelectedTextEvent(true, eQuerySelectedText,
                                                 mWidget);
  const_cast<TextEventDispatcher*>(this)->DispatchEvent(mWidget,
                                                        querySelectedTextEvent);
  if (!querySelectedTextEvent.FoundSelection()) {
    return Nothing();
  }

  return Some(querySelectedTextEvent.mReply->mWritingMode);
}

nsEventStatus TextEventDispatcher::DispatchEvent(nsIWidget* aWidget,
                                                 WidgetGUIEvent& aEvent) {
  MOZ_ASSERT(!aEvent.AsInputEvent(), "Use DispatchInputEvent()");

  nsCOMPtr<nsIWidget> widget(aWidget);
  AutoDispatchingEvent storeDispatchingEvent(*this, aEvent);
  auto status = widget->DispatchEvent(&aEvent);
  return status;
}

nsEventStatus TextEventDispatcher::DispatchInputEvent(
    nsIWidget* aWidget, WidgetInputEvent& aEvent) {
  nsCOMPtr<nsIWidget> widget(aWidget);
  AutoDispatchingEvent storeDispatchingEvent(*this, aEvent);

  // If the event is dispatched via nsIWidget::DispatchInputEvent(), it
  // sends the event to the parent process first since APZ needs to handle it
  // first.  However, some callers (e.g., keyboard apps on B2G and tests
  // expecting synchronous dispatch) don't want this to do that.
  nsEventStatus status =
      ShouldSendInputEventToAPZ()
          ? widget->DispatchInputEvent(&aEvent).mContentStatus
          : widget->DispatchEvent(&aEvent);

  return status;
}

nsresult TextEventDispatcher::StartComposition(
    nsEventStatus& aStatus, const WidgetEventTime* aEventTime) {
  aStatus = nsEventStatus_eIgnore;

  nsresult rv = GetState();
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  if (NS_WARN_IF(mIsComposing)) {
    return NS_ERROR_FAILURE;
  }

  // When you change some members from here, you may need same change in
  // BeginInputTransactionFor().
  mIsComposing = mIsHandlingComposition = true;
  WidgetCompositionEvent compositionStartEvent(true, eCompositionStart,
                                               mWidget);
  InitEvent(compositionStartEvent);
  if (aEventTime) {
    compositionStartEvent.AssignEventTime(*aEventTime);
  }
  DispatchEvent(mWidget, compositionStartEvent);
  return NS_OK;
}

nsresult TextEventDispatcher::StartCompositionAutomaticallyIfNecessary(
    nsEventStatus& aStatus, const WidgetEventTime* aEventTime) {
  if (IsComposing()) {
    return NS_OK;
  }

  nsresult rv = StartComposition(aStatus, aEventTime);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  // If started composition has already been committed, we shouldn't dispatch
  // the compositionchange event.
  if (!IsComposing()) {
    aStatus = nsEventStatus_eConsumeNoDefault;
    return NS_OK;
  }

  // Note that the widget might be destroyed during a call of
  // StartComposition().  In such case, we shouldn't keep dispatching next
  // event.
  rv = GetState();
  if (NS_FAILED(rv)) {
    MOZ_ASSERT(rv != NS_ERROR_NOT_INITIALIZED,
               "aDispatcher must still be initialized in this case");
    aStatus = nsEventStatus_eConsumeNoDefault;
    return NS_OK;  // Don't throw exception in this case
  }

  aStatus = nsEventStatus_eIgnore;
  return NS_OK;
}

nsresult TextEventDispatcher::CommitComposition(
    nsEventStatus& aStatus, const nsAString* aCommitString,
    const WidgetEventTime* aEventTime) {
  aStatus = nsEventStatus_eIgnore;

  nsresult rv = GetState();
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  // When there is no composition, caller shouldn't try to commit composition
  // with non-existing composition string nor commit composition with empty
  // string.
  if (NS_WARN_IF(!IsComposing() &&
                 (!aCommitString || aCommitString->IsEmpty()))) {
    return NS_ERROR_FAILURE;
  }

  nsCOMPtr<nsIWidget> widget(mWidget);
  rv = StartCompositionAutomaticallyIfNecessary(aStatus, aEventTime);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }
  if (aStatus == nsEventStatus_eConsumeNoDefault) {
    return NS_OK;
  }

  // When you change some members from here, you may need same change in
  // BeginInputTransactionFor().

  // End current composition and make this free for other IMEs.
  mIsComposing = false;

  EventMessage message =
      aCommitString ? eCompositionCommit : eCompositionCommitAsIs;
  WidgetCompositionEvent compositionCommitEvent(true, message, widget);
  InitEvent(compositionCommitEvent);
  if (aEventTime) {
    compositionCommitEvent.AssignEventTime(*aEventTime);
  }
  if (message == eCompositionCommit) {
    compositionCommitEvent.mData = *aCommitString;
    // If aCommitString comes from TextInputProcessor, it may be void, but
    // editor requires non-void string even when it's empty.
    compositionCommitEvent.mData.SetIsVoid(false);
    // Don't send CRLF nor CR, replace it with LF here.
    compositionCommitEvent.mData.ReplaceSubstring(u"\r\n"_ns, u"\n"_ns);
    compositionCommitEvent.mData.ReplaceSubstring(u"\r"_ns, u"\n"_ns);
  }
  aStatus = DispatchEvent(widget, compositionCommitEvent);
  return NS_OK;
}

nsresult TextEventDispatcher::NotifyIME(
    const IMENotification& aIMENotification) {
  nsresult rv = NS_ERROR_NOT_IMPLEMENTED;

  switch (aIMENotification.mMessage) {
    case NOTIFY_IME_OF_FOCUS: {
      mWritingMode = MaybeQueryWritingModeAtSelection();
      break;
    }
    case NOTIFY_IME_OF_BLUR:
      mHasFocus = false;
      mWritingMode.reset();
      ClearNotificationRequests();
      break;
    case NOTIFY_IME_OF_COMPOSITION_EVENT_HANDLED:
      // If content handles composition events when native IME doesn't have
      // composition, that means that we completely finished handling
      // composition(s).  Note that when focused content is in a remote
      // process, this is sent when all dispatched composition events
      // have been handled in the remote process.
      if (!IsComposing()) {
        mIsHandlingComposition = false;
      }
      break;
    case NOTIFY_IME_OF_SELECTION_CHANGE:
      if (mHasFocus && aIMENotification.mSelectionChangeData.HasRange()) {
        mWritingMode =
            Some(aIMENotification.mSelectionChangeData.GetWritingMode());
      }
      break;
    default:
      break;
  }

  // First, send the notification to current input transaction's listener.
  nsCOMPtr<TextEventDispatcherListener> listener = do_QueryReferent(mListener);
  if (listener) {
    rv = listener->NotifyIME(this, aIMENotification);
  }

  if (!mWidget) {
    return rv;
  }

  // If current input transaction isn't for native event handler, we should
  // send the notification to the native text event dispatcher listener
  // since native event handler may need to do something from
  // TextEventDispatcherListener::NotifyIME() even before there is no
  // input transaction yet.  For example, native IME handler may need to
  // create new context at receiving NOTIFY_IME_OF_FOCUS.  In this case,
  // mListener may not be initialized since input transaction should be
  // initialized immediately before dispatching every WidgetKeyboardEvent
  // and WidgetCompositionEvent (dispatching events always occurs after
  // focus move).
  nsCOMPtr<TextEventDispatcherListener> nativeListener =
      mWidget->GetNativeTextEventDispatcherListener();
  if (listener != nativeListener && nativeListener) {
    switch (aIMENotification.mMessage) {
      case REQUEST_TO_COMMIT_COMPOSITION:
      case REQUEST_TO_CANCEL_COMPOSITION:
        // It's not necessary to notify native IME of requests.
        break;
      default: {
        // Even if current input transaction's listener returns NS_OK or
        // something, we need to notify native IME of notifications because
        // when user typing after TIP does something, the changed information
        // is necessary for them.
        nsresult rv2 = nativeListener->NotifyIME(this, aIMENotification);
        // But return the result from current listener except when the
        // notification isn't handled.
        if (rv == NS_ERROR_NOT_IMPLEMENTED) {
          rv = rv2;
        }
        break;
      }
    }
  }

  if (aIMENotification.mMessage == NOTIFY_IME_OF_FOCUS) {
    mHasFocus = true;
    UpdateNotificationRequests();
  }

  return rv;
}

void TextEventDispatcher::ClearNotificationRequests() {
  mIMENotificationRequests = IMENotificationRequests();
}

void TextEventDispatcher::UpdateNotificationRequests() {
  ClearNotificationRequests();

  // If it doesn't has focus, no notifications are available.
  if (!mHasFocus || !mWidget) {
    return;
  }

  // If there is a listener, its requests are necessary.
  nsCOMPtr<TextEventDispatcherListener> listener = do_QueryReferent(mListener);
  if (listener) {
    mIMENotificationRequests = listener->GetIMENotificationRequests();
  }

  // Even if this is in non-native input transaction, native IME needs
  // requests.  So, add native IME requests too.
  if (!IsInNativeInputTransaction()) {
    nsCOMPtr<TextEventDispatcherListener> nativeListener =
        mWidget->GetNativeTextEventDispatcherListener();
    if (nativeListener) {
      mIMENotificationRequests += nativeListener->GetIMENotificationRequests();
    }
  }
}

bool TextEventDispatcher::DispatchKeyboardEvent(
    EventMessage aMessage, const WidgetKeyboardEvent& aKeyboardEvent,
    nsEventStatus& aStatus, void* aData) {
  return DispatchKeyboardEventInternal(aMessage, aKeyboardEvent, aStatus,
                                       aData);
}

bool TextEventDispatcher::DispatchKeyboardEventInternal(
    EventMessage aMessage, const WidgetKeyboardEvent& aKeyboardEvent,
    nsEventStatus& aStatus, void* aData, uint32_t aIndexOfKeypress,
    bool aNeedsCallback) {
  // Note that this method is also used for dispatching key events on a plugin
  // because key events on a plugin should be dispatched same as normal key
  // events.  Then, only some handlers which need to intercept key events
  // before the focused plugin (e.g., reserved shortcut key handlers) can
  // consume the events.
  MOZ_ASSERT(
      aMessage == eKeyDown || aMessage == eKeyUp || aMessage == eKeyPress,
      "Invalid aMessage value");
  nsresult rv = GetState();
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return false;
  }

  // If the key shouldn't cause keypress events, don't this patch them.
  if (aMessage == eKeyPress && !aKeyboardEvent.ShouldCauseKeypressEvents()) {
    return false;
  }

  // While we have an IME composition, `keydown` and `keyup` events should be
  // fired as "Process" key events. However, `keypress` events should not be
  // fired. https://w3c.github.io/uievents/#events-composition-key-events
  if (IsComposing() && aMessage == eKeyPress) {
    return false;
  }

  WidgetKeyboardEvent keyEvent(true, aMessage, mWidget);
  InitEvent(keyEvent);
  keyEvent.AssignKeyEventData(aKeyboardEvent, false);
  // Command arrays are not duplicated by AssignKeyEventData() due to
  // both performance and footprint reasons.  So, when TextInputProcessor
  // emulates real text input or synthesizing keyboard events for tests,
  // the arrays may be initialized all commands already.  If so, we need to
  // duplicate the arrays here, but we should do this only when we're
  // dispatching eKeyPress and eKeyDown events because
  // BrowserParent::SendRealKeyEvent() does this only for eKeyPress and eKeyDown
  // event.  Note that this is not required if we're in the main process because
  // in the parent process, the edit commands will be initialized by
  // `ExecuteEditCommands()` (when the event is handled by editor event
  // listener) or `InitAllEditCommands()` (when the event is set to a content
  // process).  We should test whether these pathes work or not too.
  if (XRE_IsContentProcess() && keyEvent.mIsSynthesizedByTIP) {
    if (aMessage == eKeyPress || aMessage == eKeyDown) {
      keyEvent.AssignCommands(aKeyboardEvent);
    } else {
      // Prevent retriving native edit commands if we're in a content process
      // because only `eKeyPress` and `eKeyDown` events coming from the main
      // process have edit commands (See `BrowserParent::SendRealKeyEvent`). And
      // also retriving edit commands from a content process requires synchonous
      // IPC and that makes running tests slower.  Therefore, we should mark the
      // other events as no longer needing to retrieve edit commands.
      keyEvent.PreventNativeKeyBindings();
    }
  }

  if (aStatus == nsEventStatus_eConsumeNoDefault) {
    // If the key event should be dispatched as consumed event, marking it here.
    // This is useful to prevent double action.  This is intended to the system
    // has already consumed the event but we need to dispatch the event for
    // compatibility with older version and other browsers.  So, we should not
    // stop cross process forwarding of them.
    keyEvent.PreventDefaultBeforeDispatch(CrossProcessForwarding::eAllow);
  }

  // If the widget wants to reply to OS asynchronously, the given event is
  // marked as "waiting reply from remote process".  Then, we need to mark the
  // dispatching event as so. NOTE: This is currently used by Android widget. On
  // desktop, marking it will run GlobalKeyListener to scan all shortcut keys
  // which match with the dispatching keyboard event.  So, if you need to use
  // this path on desktop, we should add a flag to skip the check at receiving
  // the reply event.
  if (XRE_IsParentProcess() &&
      aKeyboardEvent.IsWaitingReplyFromRemoteProcess()) {
    keyEvent.MarkAsWaitingReplyFromRemoteProcess();
  }

  // Corrects each member for the specific key event type.
  if (keyEvent.mKeyNameIndex != KEY_NAME_INDEX_USE_STRING) {
    MOZ_ASSERT(!aIndexOfKeypress,
               "aIndexOfKeypress must be 0 for non-printable key");
    // If the keyboard event isn't caused by printable key, its charCode should
    // be 0.
    keyEvent.SetCharCode(0);
  } else {
    MOZ_DIAGNOSTIC_ASSERT_IF(aMessage == eKeyDown || aMessage == eKeyUp,
                             !aIndexOfKeypress);
    MOZ_DIAGNOSTIC_ASSERT_IF(
        aMessage == eKeyPress,
        aIndexOfKeypress < std::max<size_t>(keyEvent.mKeyValue.Length(), 1));
    char16_t ch =
        keyEvent.mKeyValue.IsEmpty() ? 0 : keyEvent.mKeyValue[aIndexOfKeypress];
    keyEvent.SetCharCode(static_cast<uint32_t>(ch));
    if (aMessage == eKeyPress) {
      // keyCode of eKeyPress events of printable keys should be always 0.
      keyEvent.mKeyCode = 0;
      // eKeyPress events are dispatched for every character.
      // So, each key value of eKeyPress events should be a character.
      if (ch) {
        if (!IsSurrogate(ch)) {
          keyEvent.mKeyValue.Assign(ch);
        } else {
          const bool isHighSurrogateFollowedByLowSurrogate =
              aIndexOfKeypress + 1 < keyEvent.mKeyValue.Length() &&
              IsHighSurrogate(ch) &&
              IsLowSurrogate(keyEvent.mKeyValue[aIndexOfKeypress + 1]);
          const bool isLowSurrogateFollowingHighSurrogate =
              !isHighSurrogateFollowedByLowSurrogate && aIndexOfKeypress > 0 &&
              IsLowSurrogate(ch) &&
              IsHighSurrogate(keyEvent.mKeyValue[aIndexOfKeypress - 1]);
          NS_WARNING_ASSERTION(isHighSurrogateFollowedByLowSurrogate ||
                                   isLowSurrogateFollowingHighSurrogate,
                               "Lone surrogate input should not happen");
          if (StaticPrefs::
                  dom_event_keypress_dispatch_once_per_surrogate_pair()) {
            if (isHighSurrogateFollowedByLowSurrogate) {
              keyEvent.mKeyValue.Assign(
                  keyEvent.mKeyValue.BeginReading() + aIndexOfKeypress, 2);
              keyEvent.SetCharCode(SurrogateToUCS4(ch, keyEvent.mKeyValue[1]));
            } else if (isLowSurrogateFollowingHighSurrogate) {
              // Although not dispatching eKeyPress event (because it's already
              // dispatched for the low surrogate above), the caller should
              // treat that this dispatched eKeyPress event normally so that
              // return true here.
              return true;
            }
            // Do not expose ill-formed UTF-16 string because it's a
            // problematic for Rust-running-as-wasm for example.
            else {
              keyEvent.mKeyValue.Truncate();
            }
          } else if (!StaticPrefs::
                         dom_event_keypress_key_allow_lone_surrogate()) {
            // If it's a high surrogate followed by a low surrogate, we should
            // expose the surrogate pair with .key value.
            if (isHighSurrogateFollowedByLowSurrogate) {
              keyEvent.mKeyValue.Assign(
                  keyEvent.mKeyValue.BeginReading() + aIndexOfKeypress, 2);
            }
            // Do not expose low surrogate which should be handled by the
            // preceding keypress event.  And also do not expose ill-formed
            // UTF-16 because it's a problematic for Rust-running-as-wasm for
            // example.
            else {
              keyEvent.mKeyValue.Truncate();
            }
          } else {
            // Here is a path for traditional behavior. We set `.key` to
            // high-surrogate and low-surrogate separately.
            keyEvent.mKeyValue.Assign(ch);
          }
        }
      } else {
        keyEvent.mKeyValue.Truncate();
      }
    }
  }
  if (aMessage == eKeyUp) {
    // mIsRepeat of keyup event must be false.
    keyEvent.mIsRepeat = false;
  }
  // mIsComposing should be initialized later.
  keyEvent.mIsComposing = false;
  if (mInputTransactionType == eNativeInputTransaction) {
    // Copy mNativeKeyEvent here because for safety for other users of
    // AssignKeyEventData(), it doesn't copy this.
    keyEvent.mNativeKeyEvent = aKeyboardEvent.mNativeKeyEvent;
  } else {
    // If it's not a keyboard event for native key event, we should ensure that
    // mNativeKeyEvent is null.
    keyEvent.mNativeKeyEvent = nullptr;
  }
  // TODO: Manage mUniqueId here.

  // Request the alternative char codes for the key event.
  // eKeyDown also needs alternative char codes because nsXBLWindowKeyHandler
  // needs to check if a following keypress event is reserved by chrome for
  // stopping propagation of its preceding keydown event.
  keyEvent.mAlternativeCharCodes.Clear();
  if ((aMessage == eKeyDown || aMessage == eKeyPress) &&
      (aNeedsCallback || keyEvent.IsControl() || keyEvent.IsAlt() ||
       keyEvent.IsMeta())) {
    nsCOMPtr<TextEventDispatcherListener> listener =
        do_QueryReferent(mListener);
    if (listener) {
      DebugOnly<WidgetKeyboardEvent> original(keyEvent);
      listener->WillDispatchKeyboardEvent(this, keyEvent, aIndexOfKeypress,
                                          aData);
      MOZ_ASSERT(keyEvent.mMessage ==
                 static_cast<WidgetKeyboardEvent&>(original).mMessage);
      MOZ_ASSERT(keyEvent.mKeyCode ==
                 static_cast<WidgetKeyboardEvent&>(original).mKeyCode);
      MOZ_ASSERT(keyEvent.mLocation ==
                 static_cast<WidgetKeyboardEvent&>(original).mLocation);
      MOZ_ASSERT(keyEvent.mIsRepeat ==
                 static_cast<WidgetKeyboardEvent&>(original).mIsRepeat);
      MOZ_ASSERT(keyEvent.mIsComposing ==
                 static_cast<WidgetKeyboardEvent&>(original).mIsComposing);
      MOZ_ASSERT(keyEvent.mKeyNameIndex ==
                 static_cast<WidgetKeyboardEvent&>(original).mKeyNameIndex);
      MOZ_ASSERT(keyEvent.mCodeNameIndex ==
                 static_cast<WidgetKeyboardEvent&>(original).mCodeNameIndex);
      MOZ_ASSERT(keyEvent.mKeyValue ==
                 static_cast<WidgetKeyboardEvent&>(original).mKeyValue);
      MOZ_ASSERT(keyEvent.mCodeValue ==
                 static_cast<WidgetKeyboardEvent&>(original).mCodeValue);
    }
  }

  if (StaticPrefs::
          dom_keyboardevent_keypress_dispatch_non_printable_keys_only_system_group_in_content() &&
      keyEvent.mMessage == eKeyPress &&
      !keyEvent.ShouldKeyPressEventBeFiredOnContent()) {
    // Note that even if we set it to true, this may be overwritten by
    // PresShell::DispatchEventToDOM().
    keyEvent.mFlags.mOnlySystemGroupDispatchInContent = true;
  }

  // If an editable element has focus and we're in the parent process, we should
  // retrieve native key bindings right now because even if it matches with a
  // reserved shortcut key, it should be handled by the editor.
  if (XRE_IsParentProcess() && mHasFocus &&
      (aMessage == eKeyDown || aMessage == eKeyPress)) {
    keyEvent.InitAllEditCommands(mWritingMode);
  }

  aStatus = DispatchInputEvent(mWidget, keyEvent);
  return true;
}

bool TextEventDispatcher::MaybeDispatchKeypressEvents(
    const WidgetKeyboardEvent& aKeyboardEvent, nsEventStatus& aStatus,
    void* aData, bool aNeedsCallback) {
  // If the key event was consumed, keypress event shouldn't be fired.
  if (aStatus == nsEventStatus_eConsumeNoDefault) {
    return false;
  }

  // If the key shouldn't cause keypress events, don't fire them.
  if (!aKeyboardEvent.ShouldCauseKeypressEvents()) {
    return false;
  }

  // If the key isn't a printable key or just inputting one character or
  // no character, we should dispatch only one keypress.  Otherwise, i.e.,
  // if the key is a printable key and inputs multiple characters, keypress
  // event should be dispatched the count of inputting characters times.
  size_t keypressCount =
      aKeyboardEvent.mKeyNameIndex != KEY_NAME_INDEX_USE_STRING
          ? 1
          : std::max(static_cast<nsAString::size_type>(1),
                     aKeyboardEvent.mKeyValue.Length());
  bool isDispatched = false;
  bool consumed = false;
  for (size_t i = 0; i < keypressCount; i++) {
    aStatus = nsEventStatus_eIgnore;
    if (!DispatchKeyboardEventInternal(eKeyPress, aKeyboardEvent, aStatus,
                                       aData, i, aNeedsCallback)) {
      // The widget must have been gone.
      break;
    }
    isDispatched = true;
    if (!consumed) {
      consumed = (aStatus == nsEventStatus_eConsumeNoDefault);
    }
  }

  // If one of the keypress event was consumed, return ConsumeNoDefault.
  if (consumed) {
    aStatus = nsEventStatus_eConsumeNoDefault;
  }

  return isDispatched;
}

/******************************************************************************
 * TextEventDispatcher::PendingComposition
 *****************************************************************************/

TextEventDispatcher::PendingComposition::PendingComposition() { Clear(); }

void TextEventDispatcher::PendingComposition::Clear() {
  mString.Truncate();
  mClauses = nullptr;
  mCaret.mRangeType = TextRangeType::eUninitialized;
  mReplacedNativeLineBreakers = false;
}

void TextEventDispatcher::PendingComposition::EnsureClauseArray() {
  if (mClauses) {
    return;
  }
  mClauses = new TextRangeArray();
}

nsresult TextEventDispatcher::PendingComposition::SetString(
    const nsAString& aString) {
  MOZ_ASSERT(!mReplacedNativeLineBreakers);
  mString = aString;
  return NS_OK;
}

nsresult TextEventDispatcher::PendingComposition::AppendClause(
    uint32_t aLength, TextRangeType aTextRangeType) {
  MOZ_ASSERT(!mReplacedNativeLineBreakers);

  if (NS_WARN_IF(!aLength)) {
    return NS_ERROR_INVALID_ARG;
  }

  switch (aTextRangeType) {
    case TextRangeType::eRawClause:
    case TextRangeType::eSelectedRawClause:
    case TextRangeType::eConvertedClause:
    case TextRangeType::eSelectedClause: {
      EnsureClauseArray();
      TextRange textRange;
      textRange.mStartOffset =
          mClauses->IsEmpty() ? 0 : mClauses->LastElement().mEndOffset;
      textRange.mEndOffset = textRange.mStartOffset + aLength;
      textRange.mRangeType = aTextRangeType;
      mClauses->AppendElement(textRange);
      return NS_OK;
    }
    default:
      return NS_ERROR_INVALID_ARG;
  }
}

nsresult TextEventDispatcher::PendingComposition::SetCaret(uint32_t aOffset,
                                                           uint32_t aLength) {
  MOZ_ASSERT(!mReplacedNativeLineBreakers);

  mCaret.mStartOffset = aOffset;
  mCaret.mEndOffset = mCaret.mStartOffset + aLength;
  mCaret.mRangeType = TextRangeType::eCaret;
  return NS_OK;
}

nsresult TextEventDispatcher::PendingComposition::Set(
    const nsAString& aString, const TextRangeArray* aRanges) {
  Clear();

  nsresult rv = SetString(aString);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  if (!aRanges || aRanges->IsEmpty()) {
    // Create dummy range if mString isn't empty.
    if (!mString.IsEmpty()) {
      rv = AppendClause(mString.Length(), TextRangeType::eRawClause);
      if (NS_WARN_IF(NS_FAILED(rv))) {
        return rv;
      }
      ReplaceNativeLineBreakers();
    }
    return NS_OK;
  }

  // Adjust offsets in the ranges for XP linefeed character (only \n).
  for (uint32_t i = 0; i < aRanges->Length(); ++i) {
    TextRange range = aRanges->ElementAt(i);
    if (range.mRangeType == TextRangeType::eCaret) {
      mCaret = range;
    } else {
      EnsureClauseArray();
      mClauses->AppendElement(range);
    }
  }
  ReplaceNativeLineBreakers();
  return NS_OK;
}

void TextEventDispatcher::PendingComposition::ReplaceNativeLineBreakers() {
  mReplacedNativeLineBreakers = true;

  // If the composition string is empty, we don't need to do anything.
  if (mString.IsEmpty()) {
    return;
  }

  nsAutoString nativeString(mString);
  // Don't expose CRLF nor CR to web contents, instead, use LF.
  mString.ReplaceSubstring(u"\r\n"_ns, u"\n"_ns);
  mString.ReplaceSubstring(u"\r"_ns, u"\n"_ns);

  // If the length isn't changed, we don't need to adjust any offset and length
  // of mClauses nor mCaret.
  if (nativeString.Length() == mString.Length()) {
    return;
  }

  if (mClauses) {
    for (TextRange& clause : *mClauses) {
      AdjustRange(clause, nativeString);
    }
  }
  if (mCaret.mRangeType == TextRangeType::eCaret) {
    AdjustRange(mCaret, nativeString);
  }
}

// static
void TextEventDispatcher::PendingComposition::AdjustRange(
    TextRange& aRange, const nsAString& aNativeString) {
  TextRange nativeRange = aRange;
  // XXX Following code wastes runtime cost because this causes computing
  //     mStartOffset for each clause from the start of composition string.
  //     If we'd make TextRange have only its length, we don't need to do
  //     this.  However, this must not be so serious problem because
  //     composition string is usually short and separated as a few clauses.
  if (nativeRange.mStartOffset > 0) {
    nsAutoString preText(Substring(aNativeString, 0, nativeRange.mStartOffset));
    preText.ReplaceSubstring(u"\r\n"_ns, u"\n"_ns);
    aRange.mStartOffset = preText.Length();
  }
  if (nativeRange.Length() == 0) {
    aRange.mEndOffset = aRange.mStartOffset;
  } else {
    nsAutoString clause(Substring(aNativeString, nativeRange.mStartOffset,
                                  nativeRange.Length()));
    clause.ReplaceSubstring(u"\r\n"_ns, u"\n"_ns);
    aRange.mEndOffset = aRange.mStartOffset + clause.Length();
  }
}

nsresult TextEventDispatcher::PendingComposition::Flush(
    TextEventDispatcher* aDispatcher, nsEventStatus& aStatus,
    const WidgetEventTime* aEventTime) {
  aStatus = nsEventStatus_eIgnore;

  nsresult rv = aDispatcher->GetState();
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  if (mClauses && !mClauses->IsEmpty() &&
      mClauses->LastElement().mEndOffset != mString.Length()) {
    NS_WARNING(
        "Sum of length of the all clauses must be same as the string "
        "length");
    Clear();
    return NS_ERROR_ILLEGAL_VALUE;
  }
  if (mCaret.mRangeType == TextRangeType::eCaret) {
    if (mCaret.mEndOffset > mString.Length()) {
      NS_WARNING("Caret position is out of the composition string");
      Clear();
      return NS_ERROR_ILLEGAL_VALUE;
    }
    EnsureClauseArray();
    mClauses->AppendElement(mCaret);
  }

  // If the composition string is set without Set(), we need to replace native
  // line breakers in the composition string with XP line breaker.
  if (!mReplacedNativeLineBreakers) {
    ReplaceNativeLineBreakers();
  }

  RefPtr<TextEventDispatcher> kungFuDeathGrip(aDispatcher);
  nsCOMPtr<nsIWidget> widget(aDispatcher->mWidget);
  WidgetCompositionEvent compChangeEvent(true, eCompositionChange, widget);
  aDispatcher->InitEvent(compChangeEvent);
  if (aEventTime) {
    compChangeEvent.AssignEventTime(*aEventTime);
  }
  compChangeEvent.mData = mString;
  // If mString comes from TextInputProcessor, it may be void, but editor
  // requires non-void string even when it's empty.
  compChangeEvent.mData.SetIsVoid(false);
  if (mClauses) {
    MOZ_ASSERT(!mClauses->IsEmpty(),
               "mClauses must be non-empty array when it's not nullptr");
    compChangeEvent.mRanges = mClauses;
  }

  // While this method dispatches a composition event, some other event handler
  // cause more clauses to be added.  So, we should clear pending composition
  // before dispatching the event.
  Clear();

  rv = aDispatcher->StartCompositionAutomaticallyIfNecessary(aStatus,
                                                             aEventTime);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }
  if (aStatus == nsEventStatus_eConsumeNoDefault) {
    return NS_OK;
  }
  aStatus = aDispatcher->DispatchEvent(widget, compChangeEvent);
  return NS_OK;
}

}  // namespace widget
}  // namespace mozilla
