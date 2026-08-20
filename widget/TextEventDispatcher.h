/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_textcompositionsynthesizer_h_
#define mozilla_textcompositionsynthesizer_h_

#include "WritingModes.h"
#include "mozilla/Attributes.h"
#include "mozilla/EventForwards.h"
#include "mozilla/Maybe.h"
#include "mozilla/RefPtr.h"
#include "mozilla/TextEventDispatcherListener.h"
#include "mozilla/TextRange.h"
#include "mozilla/widget/IMEData.h"
#include "nsString.h"

class nsIWidget;

namespace mozilla {
namespace widget {

class PuppetWidget;

/**
 * TextEventDispatcher is a helper class for dispatching widget events defined
 * in TextEvents.h.  Currently, this is a helper for dispatching
 * WidgetCompositionEvent and WidgetKeyboardEvent.  This manages the behavior
 * of them for conforming to DOM Level 3 Events.
 * An instance of this class is created by nsIWidget instance and owned by it.
 * This is typically created only by the top level widgets because only they
 * handle IME.
 */

class TextEventDispatcher final {
  ~TextEventDispatcher() = default;

  NS_INLINE_DECL_REFCOUNTING(TextEventDispatcher)

 public:
  explicit TextEventDispatcher(nsIWidget* aWidget);

  /**
   * Initializes the instance for IME or automated test.  Either IME or tests
   * need to call one of them before starting composition.  If they return
   * NS_ERROR_ALREADY_INITIALIZED, it means that the listener already listens
   * notifications from TextEventDispatcher for same purpose (for IME or tests).
   * If this returns another error, the caller shouldn't keep starting
   * composition.
   *
   * @param aListener       Specify the listener to listen notifications and
   *                        requests.  This must not be null.
   *                        NOTE: aListener is stored as weak reference in
   *                              TextEventDispatcher.  See mListener
   *                              definition below.
   */
  nsresult BeginInputTransaction(TextEventDispatcherListener* aListener);
  nsresult BeginTestInputTransaction(TextEventDispatcherListener* aListener,
                                     bool aIsAPZAware);
  nsresult BeginNativeInputTransaction();

  /**
   * BeginInputTransactionFor() should be used when aPuppetWidget dispatches
   * a composition or keyboard event coming from its parent process.
   */
  nsresult BeginInputTransactionFor(const WidgetGUIEvent* aEvent,
                                    PuppetWidget* aPuppetWidget);

  /**
   * EndInputTransaction() should be called when the listener stops using
   * the TextEventDispatcher.
   *
   * FYI: This is marked as MOZ_CAN_RUN_SCRIPT but it's for the safety in the
   * future because of calling a callback which can do anything unexpected.
   *
   * @param aListener       The listener using the TextEventDispatcher instance.
   */
  MOZ_CAN_RUN_SCRIPT void EndInputTransaction(
      TextEventDispatcherListener* aListener);

  /**
   * OnDestroyWidget() is called when mWidget is being destroyed.
   */
  void OnDestroyWidget();

  nsIWidget* GetWidget() const { return mWidget; }

  /**
   * Return true starting from ending handling focus notification and until
   * receiving blur notification.
   */
  bool HasFocus() const { return mHasFocus; }

  const IMENotificationRequests& IMENotificationRequestsRef() const {
    return mIMENotificationRequests;
  }

  /**
   * OnWidgetChangeIMENotificationRequests() is called when aWidget's
   * IMENotificationRequest is maybe modified by unusual path.  E.g.,
   * modified in an async path.
   */
  void OnWidgetChangeIMENotificationRequests(nsIWidget* aWidget) {
    MOZ_ASSERT(aWidget);
    if (mWidget == aWidget) {
      UpdateNotificationRequests();
    }
  }

  /**
   * GetState() returns current state of this class.
   *
   * @return        NS_OK: Fine to compose text.
   *                NS_ERROR_NOT_INITIALIZED: BeginInputTransaction() or
   *                                          BeginInputTransactionForTests()
   *                                          should be called.
   *                NS_ERROR_NOT_AVAILABLE: The widget isn't available for
   *                                        composition.
   */
  nsresult GetState() const;

  /**
   * IsComposing() returns true after calling StartComposition() and before
   * calling CommitComposition().  In other words, native IME has composition
   * when this returns true.
   */
  bool IsComposing() const { return mIsComposing; }

  /**
   * IsHandlingComposition() returns true after calling StartComposition() and
   * content has not handled eCompositionCommit(AsIs) event.  In other words,
   * our content has composition when this returns true.
   */
  bool IsHandlingComposition() const { return mIsHandlingComposition; }

  /**
   * IsInNativeInputTransaction() returns true if native IME handler began a
   * transaction and it's not finished yet.
   */
  bool IsInNativeInputTransaction() const {
    return mInputTransactionType == eNativeInputTransaction;
  }

  /**
   * Return true if this instance dispatching an event.
   */
  [[nodiscard]] bool IsDispatchingEvent() const { return mDispatchingEvent; }

  /**
   * Return true if this instance dispatching aEvent.
   */
  [[nodiscard]] bool IsDispatching(const WidgetEvent& aEvent) const {
    return mDispatchingEvent && mDispatchingEvent->IsDispatching(aEvent);
  }

  /**
   * GetPseudoIMEContext() returns pseudo native IME context if there is an
   * input transaction whose type is not for native event handler.
   * Otherwise, returns nullptr.
   */
  void* GetPseudoIMEContext() const {
    if (mInputTransactionType == eNoInputTransaction ||
        mInputTransactionType == eNativeInputTransaction) {
      return nullptr;
    }
    return const_cast<TextEventDispatcher*>(this);
  }

  /**
   * Return writing mode at selection while this has focus.  Otherwise, or
   * never exists selection ranges, this returns Nothing.
   */
  const Maybe<WritingMode>& MaybeWritingModeRefAtSelection() const {
    return mWritingMode;
  }

  /**
   * MaybeQueryWritingModeAtSelection() returns writing mode at current
   * selection even if this does not have focus.  If this is not focused, this
   * queries selection.  Then, chrome script can run due to flushing the layout
   * if an element in chrome has focus (but it should not cause any problem
   * hopefully).
   */
  MOZ_CAN_RUN_SCRIPT Maybe<WritingMode> MaybeQueryWritingModeAtSelection()
      const;

  /**
   * StartComposition() starts composition explicitly.
   *
   * @param aEventTime  If this is not nullptr, WidgetCompositionEvent will
   *                    be initialized with this.  Otherwise, initialized
   *                    with the time at initializing.
   */
  nsresult StartComposition(nsEventStatus& aStatus,
                            const WidgetEventTime* aEventTime = nullptr);

  /**
   * CommitComposition() commits composition.
   *
   * @param aCommitString   If this is null, commits with the last composition
   *                        string.  Otherwise, commits the composition with
   *                        this value.
   * @param aEventTime      If this is not nullptr, WidgetCompositionEvent will
   *                        be initialized with this.  Otherwise, initialized
   *                        with the time at initializing.
   */
  nsresult CommitComposition(nsEventStatus& aStatus,
                             const nsAString* aCommitString = nullptr,
                             const WidgetEventTime* aEventTime = nullptr);

  /**
   * SetPendingCompositionString() sets new composition string which will be
   * dispatched with eCompositionChange event by calling Flush().
   *
   * @param aString         New composition string.
   */
  nsresult SetPendingCompositionString(const nsAString& aString) {
    return mPendingComposition.SetString(aString);
  }

  /**
   * AppendClauseToPendingComposition() appends a clause information to
   * the pending composition string.
   *
   * @param aLength         Length of the clause.
   * @param aTextRangeType  One of TextRangeType::eRawClause,
   *                        TextRangeType::eSelectedRawClause,
   *                        TextRangeType::eConvertedClause or
   *                        TextRangeType::eSelectedClause.
   */
  nsresult AppendClauseToPendingComposition(uint32_t aLength,
                                            TextRangeType aTextRangeType) {
    return mPendingComposition.AppendClause(aLength, aTextRangeType);
  }

  /**
   * SetCaretInPendingComposition() sets caret position in the pending
   * composition string and its length.  This is optional.  If IME doesn't
   * want to show caret, it shouldn't need to call this.
   *
   * @param aOffset         Offset of the caret in the pending composition
   *                        string.  This should not be larger than the length
   *                        of the pending composition string.
   * @param aLength         Caret width.  If this is 0, caret will be collapsed.
   *                        Note that Gecko doesn't supported wide caret yet,
   *                        therefore, this is ignored for now.
   */
  nsresult SetCaretInPendingComposition(uint32_t aOffset, uint32_t aLength) {
    return mPendingComposition.SetCaret(aOffset, aLength);
  }

  /**
   * SetPendingComposition() is useful if native IME handler already creates
   * array of clauses and/or caret information.
   *
   * @param aString         Composition string.  This may include native line
   *                        breakers since they will be replaced with XP line
   *                        breakers automatically.
   * @param aRanges         This should include the ranges of clauses and/or
   *                        a range of caret.  Note that this method allows
   *                        some ranges overlap each other and the range order
   *                        is not from start to end.
   */
  nsresult SetPendingComposition(const nsAString& aString,
                                 const TextRangeArray* aRanges) {
    return mPendingComposition.Set(aString, aRanges);
  }

  /**
   * FlushPendingComposition() sends the pending composition string
   * to the widget of the store DOM window.  Before calling this, IME needs to
   * set pending composition string with SetPendingCompositionString(),
   * AppendClauseToPendingComposition() and/or
   * SetCaretInPendingComposition().
   *
   * @param aEventTime      If this is not nullptr, WidgetCompositionEvent will
   *                        be initialized with this.  Otherwise, initialized
   *                        with the time at initializing.
   */
  nsresult FlushPendingComposition(
      nsEventStatus& aStatus, const WidgetEventTime* aEventTime = nullptr) {
    return mPendingComposition.Flush(this, aStatus, aEventTime);
  }

  /**
   * ClearPendingComposition() makes this instance forget pending composition.
   */
  void ClearPendingComposition() { mPendingComposition.Clear(); }

  /**
   * GetPendingCompositionClauses() returns text ranges which was appended by
   * AppendClauseToPendingComposition() or SetPendingComposition().
   */
  const TextRangeArray* GetPendingCompositionClauses() const {
    return mPendingComposition.GetClauses();
  }

  /**
   * @see nsIWidget::NotifyIME()
   */
  // Calling NotifyIME may call OS's API so that everything could happen.
  // We should mark it MOZ_CAN_RUN_SCRIPT later.
  MOZ_CAN_RUN_SCRIPT_BOUNDARY nsresult
  NotifyIME(const IMENotification& aIMENotification);

  /**
   * DispatchKeyboardEvent() maybe dispatches aKeyboardEvent.
   *
   * @param aMessage        Must be eKeyDown or eKeyUp.
   *                        Use MaybeDispatchKeypressEvents() for dispatching
   *                        eKeyPress.
   * @param aKeyboardEvent  A keyboard event.
   * @param aStatus         If dispatching event should be marked as consumed,
   *                        set nsEventStatus_eConsumeNoDefault.  Otherwise,
   *                        set nsEventStatus_eIgnore.  After dispatching
   *                        a event and it's consumed this returns
   *                        nsEventStatus_eConsumeNoDefault.
   * @param aData           Calling this method may cause calling
   *                        WillDispatchKeyboardEvent() of the listener.
   *                        aData will be set to its argument.
   * @return                true if an event is dispatched.  Otherwise, false.
   */
  bool DispatchKeyboardEvent(EventMessage aMessage,
                             const WidgetKeyboardEvent& aKeyboardEvent,
                             nsEventStatus& aStatus, void* aData = nullptr);

  /**
   * MaybeDispatchKeypressEvents() maybe dispatches a keypress event which is
   * generated from aKeydownEvent.
   *
   * @param aKeyboardEvent  A keyboard event.
   * @param aStatus         Sets the result when the caller dispatches
   *                        aKeyboardEvent.  Note that if the value is
   *                        nsEventStatus_eConsumeNoDefault, this does NOT
   *                        dispatch keypress events.
   *                        When this method dispatches one or more keypress
   *                        events and one of them is consumed, this returns
   *                        nsEventStatus_eConsumeNoDefault.
   * @param aData           Calling this method may cause calling
   *                        WillDispatchKeyboardEvent() of the listener.
   *                        aData will be set to its argument.
   * @param aNeedsCallback  Set true when caller needs to initialize each
   *                        eKeyPress event immediately before dispatch.
   *                        Then, WillDispatchKeyboardEvent() is always called.
   * @return                true if one or more events are dispatched.
   *                        Otherwise, false.
   */
  bool MaybeDispatchKeypressEvents(const WidgetKeyboardEvent& aKeyboardEvent,
                                   nsEventStatus& aStatus,
                                   void* aData = nullptr,
                                   bool aNeedsCallback = false);

 private:
  // mWidget is owner of the instance.  When this is created, this is set.
  // And when mWidget is released, this is cleared by OnDestroyWidget().
  // Note that mWidget may be destroyed already (i.e., mWidget->Destroyed() may
  // return true).
  nsIWidget* mWidget;
  // mListener is a weak reference to TextEventDispatcherListener.  That might
  // be referred by JS.  Therefore, the listener might be difficult to release
  // itself if this is a strong reference.  Additionally, it's difficult to
  // check if a method to uninstall the listener is called by valid instance.
  // So, using weak reference is the best way in this case.
  nsWeakPtr mListener;
  // mIMENotificationRequests should store current IME's notification requests.
  // So, this may be invalid when IME doesn't have focus.
  IMENotificationRequests mIMENotificationRequests;
  // mWritingMode stores writing mode at current selection starting from
  // receiving focus notification and until receiving blur notification.  When
  // selection is changed, this is updated by every selection change
  // notification.
  Maybe<WritingMode> mWritingMode;

  // mPendingComposition stores new composition string temporarily.
  // These values will be used for dispatching eCompositionChange event
  // in Flush().  When Flush() is called, the members will be cleared
  // automatically.
  class PendingComposition {
   public:
    PendingComposition();
    nsresult SetString(const nsAString& aString);
    nsresult AppendClause(uint32_t aLength, TextRangeType aTextRangeType);
    nsresult SetCaret(uint32_t aOffset, uint32_t aLength);
    nsresult Set(const nsAString& aString, const TextRangeArray* aRanges);
    nsresult Flush(TextEventDispatcher* aDispatcher, nsEventStatus& aStatus,
                   const WidgetEventTime* aEventTime);
    const TextRangeArray* GetClauses() const { return mClauses; }
    void Clear();

   private:
    nsString mString;
    RefPtr<TextRangeArray> mClauses;
    TextRange mCaret;
    bool mReplacedNativeLineBreakers;

    void EnsureClauseArray();

    /**
     * ReplaceNativeLineBreakers() replaces "\r\n" and "\r" to "\n" and adjust
     * each clause information and the caret information.
     */
    void ReplaceNativeLineBreakers();

    /**
     * AdjustRange() adjusts aRange as in the string with XP line breakers.
     *
     * @param aRange            The reference to a range in aNativeString.
     *                          This will be modified.
     * @param aNativeString     The string with native line breakers.
     *                          This may include "\r\n" and/or "\r".
     */
    static void AdjustRange(TextRange& aRange, const nsAString& aNativeString);
  };
  PendingComposition mPendingComposition;

  class MOZ_STACK_CLASS AutoDispatchingEvent {
   public:
    AutoDispatchingEvent(TextEventDispatcher& aDispatcher, WidgetEvent& aEvent)
        : mDispatcher(aDispatcher),
          mPrevDispatchingEvent(aDispatcher.mDispatchingEvent),
          mEvent(aEvent) {
      mDispatcher->mDispatchingEvent = this;
    }
    ~AutoDispatchingEvent() {
      mDispatcher->mDispatchingEvent = mPrevDispatchingEvent;
    }

    bool IsDispatching(const WidgetEvent& aEvent) const {
      return &mEvent == &aEvent ||
             (mPrevDispatchingEvent &&
              mPrevDispatchingEvent->IsDispatching(aEvent));
    }

   private:
    const OwningNonNull<TextEventDispatcher> mDispatcher;
    const AutoDispatchingEvent* const mPrevDispatchingEvent;
    const WidgetEvent& mEvent;
  };
  // While dispatching an event, this is incremented.
  const AutoDispatchingEvent* mDispatchingEvent = nullptr;

  enum InputTransactionType : uint8_t {
    // No input transaction has been started.
    eNoInputTransaction,
    // Input transaction for native IME or keyboard event handler.  Note that
    // keyboard events may be dispatched via parent process if there is.
    // In remote processes, this is also used when events come from the parent
    // process and are not for tests because we cannot distinguish if
    // TextEventDispatcher has which type of transaction when it dispatches
    // (eNativeInputTransaction or eSameProcessSyncInputTransaction).
    eNativeInputTransaction,
    // Input transaction for automated tests which are APZ-aware.  Note that
    // keyboard events may be dispatched via parent process if there is.
    eAsyncTestInputTransaction,
    // Input transaction for automated tests which assume events are fired
    // synchronously.  I.e., keyboard events are always dispatched in the
    // current process.
    // In remote processes, this is also used when events come from the parent
    // process and are not dispatched by the instance itself for APZ-aware
    // tests because this instance won't dispatch the events via the parent
    // process again.
    eSameProcessSyncTestInputTransaction,
    // Input transaction for others (currently, only FuzzingFunctions).
    // Events are fired synchronously in the process.
    // XXX Should we make this async for testing default action handlers in
    //     the main process?
    eSameProcessSyncInputTransaction
  };

  InputTransactionType mInputTransactionType;

  bool IsForTests() const {
    return mInputTransactionType == eAsyncTestInputTransaction ||
           mInputTransactionType == eSameProcessSyncTestInputTransaction;
  }

  // ShouldSendInputEventToAPZ() returns true when WidgetInputEvent should
  // be dispatched via its parent process (if there is) for APZ.  Otherwise,
  // when the input transaction is for IME of B2G or automated tests which
  // isn't APZ-aware, WidgetInputEvent should be dispatched form current
  // process directly.
  bool ShouldSendInputEventToAPZ() const {
    switch (mInputTransactionType) {
      case eNativeInputTransaction:
      case eAsyncTestInputTransaction:
        return true;
      case eSameProcessSyncTestInputTransaction:
      case eSameProcessSyncInputTransaction:
        return false;
      case eNoInputTransaction:
        NS_WARNING(
            "Why does the caller need to dispatch an event when "
            "there is no input transaction?");
        return true;
      default:
        MOZ_CRASH("Define the behavior of new InputTransactionType");
    }
  }

  // See IsComposing().
  bool mIsComposing;

  // See IsHandlingComposition().
  bool mIsHandlingComposition;

  // true while NOTIFY_IME_OF_FOCUS is received but NOTIFY_IME_OF_BLUR has not
  // received yet.  Otherwise, false.
  bool mHasFocus;

  nsresult BeginInputTransactionInternal(TextEventDispatcherListener* aListener,
                                         InputTransactionType aType);

  /**
   * InitEvent() initializes aEvent.  This must be called before dispatching
   * the event.
   */
  void InitEvent(WidgetGUIEvent& aEvent) const;

  /**
   * DispatchEvent() dispatches aEvent on aWidget.
   */
  nsEventStatus DispatchEvent(nsIWidget* aWidget, WidgetGUIEvent& aEvent);

  /**
   * DispatchInputEvent() dispatches aEvent on aWidget.
   */
  nsEventStatus DispatchInputEvent(nsIWidget* aWidget,
                                   WidgetInputEvent& aEvent);

  /**
   * StartCompositionAutomaticallyIfNecessary() starts composition if it hasn't
   * been started it yet.
   *
   * @param aStatus         If it succeeded to start composition normally, this
   *                        returns nsEventStatus_eIgnore.  Otherwise, e.g.,
   *                        the composition is canceled during dispatching
   *                        compositionstart event, this returns
   *                        nsEventStatus_eConsumeNoDefault.  In this case,
   *                        the caller shouldn't keep doing its job.
   * @param aEventTime      If this is not nullptr, WidgetCompositionEvent will
   *                        be initialized with this.  Otherwise, initialized
   *                        with the time at initializing.
   * @return                Only when something unexpected occurs, this returns
   *                        an error.  Otherwise, returns NS_OK even if aStatus
   *                        is nsEventStatus_eConsumeNoDefault.
   */
  nsresult StartCompositionAutomaticallyIfNecessary(
      nsEventStatus& aStatus, const WidgetEventTime* aEventTime);

  /**
   * DispatchKeyboardEventInternal() maybe dispatches aKeyboardEvent.
   *
   * @param aMessage        Must be eKeyDown, eKeyUp or eKeyPress.
   * @param aKeyboardEvent  A keyboard event.  If aMessage is eKeyPress and
   *                        the event is for second or later character, its
   *                        mKeyValue should be empty string.
   * @param aStatus         If dispatching event should be marked as consumed,
   *                        set nsEventStatus_eConsumeNoDefault.  Otherwise,
   *                        set nsEventStatus_eIgnore.  After dispatching
   *                        a event and it's consumed this returns
   *                        nsEventStatus_eConsumeNoDefault.
   * @param aData           Calling this method may cause calling
   *                        WillDispatchKeyboardEvent() of the listener.
   *                        aData will be set to its argument.
   * @param aIndexOfKeypress    This must be 0 if aMessage isn't eKeyPress or
   *                            aKeyboard.mKeyNameIndex isn't
   *                            KEY_NAME_INDEX_USE_STRING.  Otherwise, i.e.,
   *                            when an eKeyPress event causes inputting
   *                            text, this must be between 0 and
   *                            mKeyValue.Length() - 1 since keypress events
   *                            sending only one character per event.
   * @param aNeedsCallback  Set true when caller needs to initialize each
   *                        eKeyPress event immediately before dispatch.
   *                        Then, WillDispatchKeyboardEvent() is always called.
   * @return                true if an event is dispatched.  Otherwise, false.
   */
  // TODO: Mark this as MOZ_CAN_RUN_SCRIPT instead.
  MOZ_CAN_RUN_SCRIPT_BOUNDARY bool DispatchKeyboardEventInternal(
      EventMessage aMessage, const WidgetKeyboardEvent& aKeyboardEvent,
      nsEventStatus& aStatus, void* aData, uint32_t aIndexOfKeypress = 0,
      bool aNeedsCallback = false);

  /**
   * ClearNotificationRequests() clears mIMENotificationRequests.
   */
  void ClearNotificationRequests();

  /**
   * UpdateNotificationRequests() updates mIMENotificationRequests with
   * current state.  If the instance doesn't have focus, this clears
   * mIMENotificationRequests.  Otherwise, updates it with both requests of
   * current listener and native listener.
   */
  void UpdateNotificationRequests();
};

}  // namespace widget
}  // namespace mozilla

#endif  // #ifndef mozilla_widget_textcompositionsynthesizer_h_
