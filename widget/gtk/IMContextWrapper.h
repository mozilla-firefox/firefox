/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef IMContextWrapper_h_
#define IMContextWrapper_h_

#include <gdk/gdk.h>
#include <gtk/gtk.h>

#include "mozilla/ContentData.h"
#include "mozilla/EventForwards.h"
#include "mozilla/GUniquePtr.h"
#include "mozilla/Maybe.h"
#include "mozilla/TextEventDispatcherListener.h"
#include "mozilla/WritingModes.h"
#include "mozilla/widget/IMEData.h"
#include "nsCOMPtr.h"
#include "nsIWidget.h"
#include "nsString.h"
#include "nsTArray.h"

class nsWindow;

namespace mozilla {
namespace widget {

/**
 * KeyHandlingState is result of IMContextWrapper::OnKeyEvent().
 */
enum class KeyHandlingState {
  // The native key event has not been handled by IMContextWrapper.
  eNotHandled,
  // The native key event was handled by IMContextWrapper.
  eHandled,
  // The native key event has not been handled by IMContextWrapper,
  // but eKeyDown or eKeyUp event has been dispatched.
  eNotHandledButEventDispatched,
  // The native key event has not been handled by IMContextWrapper,
  // but eKeyDown or eKeyUp event has been dispatched and consumed.
  eNotHandledButEventConsumed,
};

class IMContextWrapper final : public TextEventDispatcherListener {
 public:
  // TextEventDispatcherListener implementation
  NS_DECL_ISUPPORTS

  NS_IMETHOD NotifyIME(TextEventDispatcher* aTextEventDispatcher,
                       const IMENotification& aNotification) override;
  NS_IMETHOD_(IMENotificationRequests) GetIMENotificationRequests() override;
  NS_IMETHOD_(void)
  OnRemovedFrom(TextEventDispatcher* aTextEventDispatcher) override;
  NS_IMETHOD_(void)
  WillDispatchKeyboardEvent(TextEventDispatcher* aTextEventDispatcher,
                            WidgetKeyboardEvent& aKeyboardEvent,
                            uint32_t aIndexOfKeypress, void* aData) override;

 public:
  // aOwnerWindow is a pointer of the owner window.  When aOwnerWindow is
  // destroyed, the related IME contexts are released (i.e., IME cannot be
  // used with the instance after that).
  explicit IMContextWrapper(nsWindow* aOwnerWindow);

  // Called when the process is being shut down.
  static void Shutdown();

  // "Enabled" means the users can use all IMEs.
  // I.e., the focus is in the normal editors.
  bool IsEnabled() const;

  bool IsEditable() const { return mInputContext.mIMEState.IsEditable(); }

  // OnFocusWindow is a notification that aWindow is going to be focused.
  void OnFocusWindow(nsWindow* aWindow);
  // OnBlurWindow is a notification that aWindow is going to be unfocused.
  void OnBlurWindow(nsWindow* aWindow);
  // OnDestroyWindow is a notification that aWindow is going to be destroyed.
  void OnDestroyWindow(nsWindow* aWindow);
  // OnFocusChangeInGecko is a notification that an editor gets focus.
  void OnFocusChangeInGecko(bool aFocus);
  // OnSelectionChange is a notification that selection (caret) is changed
  // in the focused editor.
  void OnSelectionChange(nsWindow* aCaller,
                         const IMENotification& aIMENotification);
  // OnThemeChanged is called when desktop theme is changed.
  static void OnThemeChanged();

  /**
   * OnKeyEvent() is called when aWindow gets a native key press event or a
   * native key release event.  If this returns true, the key event was
   * filtered by IME.  Otherwise, this returns false.
   * NOTE: When the native key press event starts composition, this returns
   *       true but dispatches an eKeyDown event or eKeyUp event before
   *       dispatching composition events or content command event.
   *
   * @param aWindow                       A window on which user operate the
   *                                      key.
   * @param aEvent                        A native key press or release
   *                                      event.
   * @param aKeyboardEventWasDispatched   true if eKeyDown or eKeyUp event
   *                                      for aEvent has already been
   *                                      dispatched.  In this case,
   *                                      this class doesn't dispatch
   *                                      keyboard event anymore.
   */
  KeyHandlingState OnKeyEvent(nsWindow* aWindow, GdkEventKey* aEvent,
                              bool aKeyboardEventWasDispatched = false);

  /**
   * Return a grapheme cluster if the IM filtered the aEvent of the previous
   * OnKeyEvent call and that caused committing a grapheme cluster
   * synchronously.
   * Otherwise, return a void string.
   */
  const nsString& GetCommittedGraphemeCluster() const {
    return mGraphemeClusterFallbackToKeyEvent;
  }

  // IME related nsIWidget methods.
  nsresult EndIMEComposition(nsWindow* aCaller);
  void SetInputContext(nsWindow* aCaller, const InputContext* aContext,
                       const InputContextAction* aAction);
  InputContext GetInputContext();
  void OnUpdateComposition();
  void OnLayoutChange();

  TextEventDispatcher* GetTextEventDispatcher();

  // TODO: Typically, new IM comes every several years.  And now, our code
  //       becomes really IM behavior dependent.  So, perhaps, we need prefs
  //       to control related flags for IM developers.
  enum class IMContextID : uint8_t {
    Fcitx,  // 4.x or earlier
    Fcitx5,
    IBus,
    IIIMF,
    Scim,
    Uim,
    Wayland,
    Unknown,
  };

  friend auto format_as(const IMContextID aIMContextID) {
    switch (aIMContextID) {
      case IMContextID::Fcitx:
        return "Fcitx";
      case IMContextID::Fcitx5:
        return "Fcitx5";
      case IMContextID::IBus:
        return "IBus";
      case IMContextID::IIIMF:
        return "IIIMF";
      case IMContextID::Scim:
        return "Scim";
      case IMContextID::Uim:
        return "Uim";
      case IMContextID::Wayland:
        return "Wayland";
      case IMContextID::Unknown:
        return "Unknown";
    }
    MOZ_ASSERT_UNREACHABLE("Add new case for the new IM support");
    return "Unknown";
  }
  friend std::ostream& operator<<(std::ostream& aStream,
                                  const IMContextID& aIMContextID) {
    return aStream << format_as(aIMContextID);
  }

  [[nodiscard]] static guint GetAsyncSynthesizedEventStateFlag(
      IMContextID aIMContextID) {
    switch (aIMContextID) {
      case IMContextID::IBus:
        // See src/ibustypes.h
        constexpr static guint IBUS_IGNORED_MASK = 1 << 25;
        return IBUS_IGNORED_MASK;
      case IMContextID::Fcitx:
      case IMContextID::Fcitx5:
        // See src/lib/fcitx-utils/keysym.h
        constexpr static guint FcitxKeyState_IgnoredMask = 1 << 25;
        return FcitxKeyState_IgnoredMask;
      default:
        return 0;
    }
  }
  [[nodiscard]] guint GetAsyncSynthesizedEventStateFlag() const {
    return GetAsyncSynthesizedEventStateFlag(mIMContextID);
  }

  /**
   * GetIMName() returns IM name associated with mContext.  If the context is
   * xim, this look for actual engine from XMODIFIERS environment variable.
   */
  nsDependentCSubstring GetIMName() const;

  /**
   * GetWaitingSynthesizedKeyPressHardwareKeyCode() returns hardware_keycode
   * value of last handled GDK_KEY_PRESS event which is probable handled by
   * IME asynchronously and we have not received synthesized GDK_KEY_PRESS
   * event yet.
   */
  static guint16 GetWaitingSynthesizedKeyPressHardwareKeyCode() {
    return sWaitingSynthesizedKeyPressHardwareKeyCode;
  }

 protected:
  ~IMContextWrapper();

  /**
   * SetInputPurposeAndInputHints() sets input-purpose and input-hints of
   * current IM context to the values computed with mInputContext.
   */
  void SetInputPurposeAndInputHints();

  // Owner of an instance of this class. This should be top level window.
  // The owner window must release the contexts when it's destroyed because
  // the IME contexts need the native window.  If OnDestroyWindow() is called
  // with the owner window, it'll release IME contexts.  Otherwise, it'll
  // just clean up any existing composition if it's related to the destroying
  // child window.
  nsWindow* mOwnerWindow;

  // A last focused window in this class's context.
  nsWindow* mLastFocusedWindow;

  // Actual context. This is used for handling the user's input.
  GtkIMContext* mContext;

  // mSimpleContext is used for the password field and
  // the |ime-mode: disabled;| editors if sUseSimpleContext is true.
  // These editors disable IME.  But dead keys should work.  Fortunately,
  // the simple IM context of GTK2 support only them.
  GtkIMContext* mSimpleContext;

  // mDummyContext is a dummy context and will be used in Focus()
  // when the state of mEnabled means disabled.  This context's IME state is
  // always "closed", so it closes IME forcedly.
  GtkIMContext* mDummyContext;

  // mComposingContext is not nullptr while one of mContext, mSimpleContext
  // and mDummyContext has composition.
  // XXX: We don't assume that two or more context have composition same time.
  GtkIMContext* mComposingContext;

  // IME enabled state and other things defined in InputContext.
  // Use following helper methods if you don't need the detail of the status.
  InputContext mInputContext;

  // mCompositionStart is the start offset of the composition string in the
  // current content.  When <textarea> or <input> have focus, it means offset
  // from the first character of them.  When a HTML editor has focus, it
  // means offset from the first character of the root element of the editor.
  uint32_t mCompositionStart;

  // mDispatchedCompositionString is the latest composition string which
  // was dispatched by compositionupdate event.
  nsString mDispatchedCompositionString;

  // mSelectedStringRemovedByComposition is the selected string which was
  // removed by first compositionchange event.
  nsString mSelectedStringRemovedByComposition;

  // If we're handling a key event, this is set to the event.
  GdkEventKey* mHandlingKeyEvent = nullptr;

  /**
   * GdkEventKeyQueue stores *copy* of GdkEventKey instances.  However, this
   * must be safe to our usecase since it has |time| and the value should not
   * be same as older event.
   */
  class GdkEventKeyQueue final {
   public:
    ~GdkEventKeyQueue() { Clear(); }

    void Clear() {
      mEvents.Clear();
      mState.Clear();
    }

    /**
     * Push new event into the queue.
     */
    void Push(const GdkEventKey* aEvent) {
      GdkEventKey* newEvent = reinterpret_cast<GdkEventKey*>(
          gdk_event_copy(reinterpret_cast<const GdkEvent*>(aEvent)));
      newEvent->state &= GDK_MODIFIER_MASK;
      mEvents.AppendElement(newEvent);
      mState.AppendElement();
    }

    struct KeyEventState {
      // If we receive a synthesized event as a result of asynchronous handling
      // of the event in the IME process, this is set to true.
      bool mReceived = false;
      // If IME sends a signal for the key event, this is set to true.
      bool mHandledSomething = false;
      // If OnKeyEvent() ends processing the key event, this is set to true.
      bool mProcessed = false;

      friend inline auto format_as(const KeyEventState& aState) {
        return fmt::format(
            "{{ mProcessed={}, mHandledSomething={}, mReceived={} }}",
            TrueOrFalse(aState.mProcessed),
            TrueOrFalse(aState.mHandledSomething),
            TrueOrFalse(aState.mReceived));
      }
    };

    /**
     * Called when aEvent is handled asynchronously.
     */
    void AsyncEventReceived(const GdkEventKey* aEvent) {
      const size_t index = IndexOf(aEvent);
      if (NS_WARN_IF(index == GdkEventKeyQueue::NoIndex())) {
        return;
      }
      mState[index].mReceived = true;
    }

    /**
     * Called when composition signal is received while we're processing aEvent.
     */
    void HandledSomething(const GdkEventKey* aEvent) {
      if (!aEvent) {
        return;
      }
      const size_t index = IndexOf(aEvent);
      if (index == GdkEventKeyQueue::NoIndex()) {
        return;
      }
      mState[index].mHandledSomething = true;
    }

    // Called when OnKeyEvent ends processing aEvent.
    void Processed(const GdkEventKey* aEvent) {
      if (!aEvent) {
        return;
      }
      const size_t index = IndexOf(aEvent);
      if (index == GdkEventKeyQueue::NoIndex()) {
        return;
      }
      mState[index].mProcessed = true;
    }

    void MarkEventAsProcessedIfHandled(const GdkEventKey* aEvent) {
      const size_t index = IndexOf(aEvent);
      if (index == NoIndex()) {
        return;
      }
      if (mState[index].mHandledSomething) {
        mState[index].mProcessed = true;
      }
    }

    bool MarkNonReceivedButHandledEventsAsProcessed() {
      bool ret = false;
      for (auto& state : mState) {
        if (!state.mReceived && state.mHandledSomething && !state.mProcessed) {
          state.mProcessed = true;
          ret = true;
        }
      }
      return ret;
    }

    void RemoveProcessedEvents() {
      const size_t count = [&]() {
        size_t len = 0;
        for (const auto& state : mState) {
          if (!state.mProcessed) {
            break;
          }
          len++;
        }
        return len;
      }();
      if (count) {
        mEvents.RemoveElementsAt(0, count);
        mState.RemoveElementsAt(0, count);
      }
    }

    /**
     * Return corresponding GDK_KEY_PRESS event for aEvent.  aEvent must be a
     * GDK_KEY_RELEASE event.
     */
    std::pair<const GdkEventKey*, const KeyEventState*>
    GetCorrespondingNonProcessedKeyPressEvent(const GdkEventKey* aEvent) const {
      MOZ_ASSERT(aEvent->type == GDK_KEY_RELEASE);
      for (const size_t i : IntegerRange(mEvents.Length())) {
        const auto& pendingKeyEvent = mEvents[i];
        const auto& state = mState[i];
        if (!state.mProcessed && pendingKeyEvent->type == GDK_KEY_PRESS &&
            aEvent->hardware_keycode == pendingKeyEvent->hardware_keycode &&
            pendingKeyEvent->time <= aEvent->time) {
          return {pendingKeyEvent.get(), &state};
        }
      }
      return {nullptr, nullptr};
    }

    /**
     * Return the oldest event which has not been handled nor processed in the
     * queue.
     */
    std::pair<const GdkEventKey*, const KeyEventState*>
    GetFirstNonHandledEvent() const {
      const size_t index = [&]() {
        size_t i = 0;
        for (const auto& state : mState) {
          if (!state.mProcessed && !state.mHandledSomething) {
            return i;
          }
          i++;
        }
        return NoIndex();
      }();
      if (index == NoIndex()) {
        return {nullptr, nullptr};
      }
      MOZ_ASSERT(!mState[index].mProcessed);
      return {mEvents[index].get(), &mState[index]};
    }

    /**
     * Return the oldest event which has not been processed in the queue.
     */
    std::pair<const GdkEventKey*, const KeyEventState*>
    GetFirstNonProcessedEvent() const {
      const size_t index = [&]() {
        size_t i = 0;
        for (const auto& state : mState) {
          if (!state.mProcessed) {
            return i;
          }
          i++;
        }
        return NoIndex();
      }();
      if (index == NoIndex()) {
        return {nullptr, nullptr};
      }
      MOZ_ASSERT(!mState[index].mProcessed);
      return {mEvents[index].get(), &mState[index]};
    }

    /**
     * Return a copy of the oldest pending event in the queue.
     */
    GUniquePtr<GdkEventKey> MakeCopyOfFirstNonProcessedEvent() const {
      GdkEventKey* const firstEvent =
          const_cast<GdkEventKey*>(GetFirstNonProcessedEvent().first);
      GUniquePtr<GdkEventKey> copy;
      if (firstEvent) {
        copy.reset(reinterpret_cast<GdkEventKey*>(
            gdk_event_copy(reinterpret_cast<GdkEvent*>(firstEvent))));
      }
      return copy;
    }

    /**
     * Return the latest pending event in the queue.
     */
    std::pair<const GdkEventKey*, const KeyEventState*>
    GetLatestNonProcessedEvent() const {
      const size_t index = [&]() {
        size_t i = mState.Length();
        for (const auto& state : Reversed(mState)) {
          if (!state.mProcessed) {
            return i - 1;
          }
          i--;
        }
        return NoIndex();
      }();
      if (index >= mState.Length()) {
        return {nullptr, nullptr};
      }
      MOZ_ASSERT(!mState[index].mProcessed);
      return {mEvents[index].get(), &mState[index]};
    }

    [[nodiscard]] bool HasNonProcessedEvents() const {
      if (mState.IsEmpty()) {
        return false;
      }
      for (const auto& state : Reversed(mState)) {
        if (!state.mProcessed) {
          return true;
        }
      }
      return false;
    }

    static size_t NoIndex() { return nsTArray<GdkEventKey*>::NoIndex; }

    [[nodiscard]] size_t CountOfPendingEvents() const {
      size_t count = 0;
      for (const auto& state : mState) {
        if (!state.mProcessed) {
          count++;
        }
      }
      return count;
    }

    [[nodiscard]] size_t CountOfAllEvents() const { return mEvents.Length(); }

    size_t IndexOf(const GdkEventKey* aEvent) const {
      static_assert(!(GDK_MODIFIER_MASK & (1 << 24)),
                    "We assumes 25th bit is used by some IM, but used by GDK");
      static_assert(!(GDK_MODIFIER_MASK & (1 << 25)),
                    "We assumes 26th bit is used by some IM, but used by GDK");
      for (size_t i = 0; i < mEvents.Length(); i++) {
        GdkEventKey* event = mEvents[i].get();
        // It must be enough to compare only type, time, keyval and
        // part of state.   Note that we cannot compare two events
        // simply since IME may have changed unused bits of state.
        if (event->time == aEvent->time) {
          if (NS_WARN_IF(event->type != aEvent->type) ||
              NS_WARN_IF(event->keyval != aEvent->keyval) ||
              NS_WARN_IF(event->state != (aEvent->state & GDK_MODIFIER_MASK))) {
            continue;
          }
          return i;
        }
      }
      return GdkEventKeyQueue::NoIndex();
    }

    const KeyEventState& KeyEventStateAt(size_t aIndex) const {
      MOZ_ASSERT(aIndex <= mState.Length());
      return mState[aIndex];
    }

    std::string FormatAs(const IMContextID aIMContextID) const;

   private:
    AutoTArray<GUniquePtr<GdkEventKey>, 4> mEvents;
    AutoTArray<KeyEventState, 4> mState;
  };
  // Append by OnKeyEvent() when it believes that a key event will be handled by
  // IME process and IME sends something later.
  GdkEventKeyQueue mPendingKeyEvents;

  /**
   * Make this instance when a signal handler starts handling the signal.
   */
  class AutoHandlingCompositionSignalHelper;

  static guint16 sWaitingSynthesizedKeyPressHardwareKeyCode;

  struct Range {
    uint32_t mOffset;
    uint32_t mLength;

    Range() : mOffset(UINT32_MAX), mLength(UINT32_MAX) {}

    bool IsValid() const { return mOffset != UINT32_MAX; }
    void Clear() {
      mOffset = UINT32_MAX;
      mLength = UINT32_MAX;
    }
  };

  // current target offset and length of IME composition
  Range mCompositionTargetRange;

  // mCompositionState indicates current status of composition.
  enum eCompositionState : uint8_t {
    eCompositionState_NotComposing,
    eCompositionState_CompositionStartDispatched,
    eCompositionState_CompositionChangeEventDispatched
  };
  eCompositionState mCompositionState;

  bool IsComposing() const {
    return (mCompositionState != eCompositionState_NotComposing);
  }

  bool IsComposingOn(GtkIMContext* aContext) const {
    return IsComposing() && mComposingContext == aContext;
  }

  bool IsComposingOnCurrentContext() const {
    return IsComposingOn(GetCurrentContext());
  }

  bool EditorHasCompositionString() {
    return (mCompositionState ==
            eCompositionState_CompositionChangeEventDispatched);
  }

  /**
   * Checks if aContext is valid context for handling composition.
   *
   * @param aContext          An IM context which is specified by native
   *                          composition events.
   * @return                  true if the context is valid context for
   *                          handling composition.  Otherwise, false.
   */
  bool IsValidContext(GtkIMContext* aContext) const;

  const char* GetCompositionStateName() {
    switch (mCompositionState) {
      case eCompositionState_NotComposing:
        return "NotComposing";
      case eCompositionState_CompositionStartDispatched:
        return "CompositionStartDispatched";
      case eCompositionState_CompositionChangeEventDispatched:
        return "CompositionChangeEventDispatched";
      default:
        return "InvaildState";
    }
  }

  // mIMContextID indicates the ID of mContext.  This is actually indicates
  // IM which user selected.
  IMContextID mIMContextID;

  // If mContentSelection is Nothing, it means that
  // EnsureToCacheContentSelection failed to get selection or just not caching
  // the selection.
  Maybe<ContentSelection> mContentSelection;

  /**
   * Return true if mContentSelection is set to some.  Otherwise, false.
   */
  bool EnsureToCacheContentSelection(nsAString* aSelectedString = nullptr);

  enum class IMEFocusState : uint8_t {
    // IME has focus
    Focused,
    // IME was blurred
    Blurred,
    // IME was blurred without a focus change
    BlurredWithoutFocusChange,
  };
  friend auto format_as(IMEFocusState aState) {
    switch (aState) {
      case IMEFocusState::Focused:
        return "Focused";
      case IMEFocusState::Blurred:
        return "Blurred";
      case IMEFocusState::BlurredWithoutFocusChange:
        return "BlurredWithoutFocusChange";
      default:
        MOZ_ASSERT_UNREACHABLE("Invalid value");
        return "<illegal value>";
    }
  }
  friend std::ostream& operator<<(std::ostream& aStream, IMEFocusState aState) {
    return aStream << format_as(aState);
  }
  IMEFocusState mIMEFocusState = IMEFocusState::Blurred;

  // mGraphemeClusterFallbackToKeyEvent is set to void when this class starts to
  // handle a native key event (at that time, mProcessingKeyEvent is set to the
  // native event).  If active IME just commits composition with a character
  // which is produced by the key with current keyboard layout, this is set
  // to the string which should be inserted as the result of the key press.
  nsString mGraphemeClusterFallbackToKeyEvent = VoidString();
  // mKeyboardEventWasDispatched is used by OnKeyEvent() and
  // MaybeDispatchKeyEventAsProcessedByIME().
  // MaybeDispatchKeyEventAsProcessedByIME() dispatches an eKeyDown or
  // eKeyUp event event if the composition is caused by a native
  // key press event.  If this is true, a keyboard event has been dispatched
  // for the native event.  If so, MaybeDispatchKeyEventAsProcessedByIME()
  // won't dispatch keyboard event anymore.
  bool mKeyboardEventWasDispatched;
  // Whether the keyboard event which as dispatched at setting
  // mKeyboardEventWasDispatched to true was consumed or not.
  bool mKeyboardEventWasConsumed;
  // mIsDeletingSurrounding is true while OnDeleteSurroundingNative() is
  // trying to delete the surrounding text.
  bool mIsDeletingSurrounding;
  // mLayoutChanged is true after OnLayoutChange() is called.  This is reset
  // when eCompositionChange is being dispatched.
  bool mLayoutChanged;
  // mSetCursorPositionOnKeyEvent true when caret rect or position is updated
  // with no composition.  If true, we update candidate window position
  // before key down
  bool mSetCursorPositionOnKeyEvent;
  // mPendingResettingIMContext becomes true if selection change notification
  // is received during composition but the selection change occurred before
  // starting the composition.  In such case, we cannot notify IME of
  // selection change during composition because we don't want to commit
  // the composition in such case.  However, we should notify IME of the
  // selection change after the composition is committed.
  bool mPendingResettingIMContext;
  // mRetrieveSurroundingSignalReceived is true after "retrieve_surrounding"
  // signal is received until selection is changed in Gecko.
  bool mRetrieveSurroundingSignalReceived;
  // mMaybeInDeadKeySequence is set to true when we detect a dead key press
  // and set to false when we're sure dead key sequence has been finished.
  // Note that we cannot detect which key event causes ending a dead key
  // sequence.  For example, when you press dead key grave with ibus Spanish
  // keyboard layout, it just consumes the key event when we call
  // gtk_im_context_filter_keypress().  Then, pressing "Escape" key cancels
  // the dead key sequence but we don't receive any signal and it's consumed
  // by gtk_im_context_filter_keypress() normally.  On the other hand, when
  // pressing "Shift" key causes exactly same behavior but dead key sequence
  // isn't finished yet.
  bool mMaybeInDeadKeySequence;
  // mIsIMInAsyncKeyHandlingMode is set to true if we know that IM handles
  // key events asynchronously.  I.e., filtered key event may come again
  // later.
  //
  // ibus - ibus filters the native GDK_KEY_PRESS and GDK_KEY_RELEASE if
  // they handle the event in their process asynchronously. Then, they may
  // send the same key event with setting `state` to contain IBUS_IGNORED_MASK.
  // Then, the synthesized key event will be filtered by ibus and send some
  // composition signals synchronously. Note that in some environments, ibus
  // does not send the synthesized events for GDK_KEY_PRESS. In such case, they
  // send composition signals asynchronously, i.e., after calling
  // `gtk_im_context_filter_keypress()`.
  //
  // fcitx, fcitx5 - they filter the native GDK_KEY_PRESS and GDK_KEY_RELEASE if
  // they handle the event in their process asynchronously. Then, they send the
  // same key event with setting `state` to contain FcitxKeyState_IgnoredMask.
  bool mIsIMInAsyncKeyHandlingMode;
  // mIsKeySnooped is set to true if IM uses key snooper to listen key events.
  // In such case, we won't receive key events if IME consumes the event.
  bool mIsKeySnooped;
  // mSetInputPurposeAndInputHints is set if `SetInputContext` wants `Focus`
  // to set input-purpose and input-hints.
  bool mSetInputPurposeAndInputHints;
  // mPendingSetSurrounding is set if "retrieve_surrounding" signal is received
  // but the surrounding text is not set yet. We retry setting the surrounding
  // text when selection is changed in Gecko.
  bool mPendingSetSurrounding = false;

  // sLastFocusedContext is a pointer to the last focused instance of this
  // class.  When a instance is destroyed and sLastFocusedContext refers it,
  // this is cleared.  So, this refers valid pointer always.
  static IMContextWrapper* sLastFocusedContext;

  // sUseSimpleContext indeicates if password editors and editors with
  // |ime-mode: disabled;| should use GtkIMContextSimple.
  // If true, they use GtkIMContextSimple.  Otherwise, not.
  static bool sUseSimpleContext;

  // Callback methods for native IME events.  These methods should call
  // the related instance methods simply.
  static gboolean OnRetrieveSurroundingCallback(GtkIMContext* aContext,
                                                IMContextWrapper* aModule);
  static gboolean OnDeleteSurroundingCallback(GtkIMContext* aContext,
                                              gint aOffset, gint aNChars,
                                              IMContextWrapper* aModule);
  static void OnCommitCompositionCallback(GtkIMContext* aContext,
                                          const gchar* aString,
                                          IMContextWrapper* aModule);
  static void OnChangeCompositionCallback(GtkIMContext* aContext,
                                          IMContextWrapper* aModule);
  static void OnStartCompositionCallback(GtkIMContext* aContext,
                                         IMContextWrapper* aModule);
  static void OnEndCompositionCallback(GtkIMContext* aContext,
                                       IMContextWrapper* aModule);

  // The instance methods for the native IME events.
  gboolean OnRetrieveSurroundingNative(GtkIMContext* aContext);
  gboolean OnDeleteSurroundingNative(GtkIMContext* aContext, gint aOffset,
                                     gint aNChars);
  void OnCommitCompositionNative(GtkIMContext*, const gchar*);
  void OnChangeCompositionNative(GtkIMContext* aContext);
  void OnStartCompositionNative(GtkIMContext* aContext);
  void OnEndCompositionNative(GtkIMContext* aContext);

  /**
   * GetCurrentContext() returns current IM context which is chosen with the
   * enabled state.
   * WARNING:
   *     When this class receives some signals for a composition after focus
   *     is moved in Gecko, the result of this may be different from given
   *     context by the signals.
   */
  GtkIMContext* GetCurrentContext() const;

  /**
   * GetActiveContext() returns a composing context or current context.
   */
  GtkIMContext* GetActiveContext() const {
    return mComposingContext ? mComposingContext : GetCurrentContext();
  }

  // If the owner window and IM context have been destroyed, returns TRUE.
  bool IsDestroyed() { return !mOwnerWindow; }

  void NotifyIMEOfFocusChange(IMEFocusState aIMEFocusState);

  // Initializes the instance.
  void Init();

  /**
   * Reset the active context, i.e., if there is mComposingContext, reset it.
   * Otherwise, reset current context.  Note that all native composition
   * events during calling this will be ignored.
   */
  void ResetIME();

  // Gets the current composition string by the native APIs.
  void GetCompositionString(GtkIMContext* aContext,
                            nsAString& aCompositionString);

  /**
   * Generates our text range array from current composition string.
   *
   * @param aContext              A GtkIMContext which is being handled.
   * @param aCompositionString    The data to be dispatched with
   *                              compositionchange event.
   */
  already_AddRefed<TextRangeArray> CreateTextRangeArray(
      GtkIMContext* aContext, const nsAString& aCompositionString);

  /**
   * SetTextRange() initializes aTextRange with aPangoAttrIter.
   *
   * @param aPangoAttrIter            An iter which represents a clause of the
   *                                  composition string.
   * @param aUTF8CompositionString    The whole composition string (UTF-8).
   * @param aUTF16CaretOffset         The caret offset in the composition
   *                                  string encoded as UTF-16.
   * @param aTextRange                The result.
   * @return                          true if this initializes aTextRange.
   *                                  Otherwise, false.
   */
  bool SetTextRange(PangoAttrIterator* aPangoAttrIter,
                    const gchar* aUTF8CompositionString,
                    uint32_t aUTF16CaretOffset, TextRange& aTextRange) const;

  /**
   * ToNscolor() converts the PangoColor in aPangoAttrColor to nscolor.
   */
  static nscolor ToNscolor(PangoAttrColor* aPangoAttrColor);

  /**
   * Move the candidate window with "fake" cursor position.
   *
   * @param aContext              A GtkIMContext which is being handled.
   */
  void SetCursorPosition(GtkIMContext* aContext);

  // Queries the current selection offset of the window.
  uint32_t GetSelectionOffset(nsWindow* aWindow);

  // Get current paragraph text content and cursor position
  nsresult GetCurrentParagraph(nsAString& aText, uint32_t& aCursorPos);

  /**
   * Delete text portion
   *
   * @param aContext              A GtkIMContext which is being handled.
   * @param aOffset               Start offset of the range to delete.
   * @param aNChars               Count of characters to delete.  It depends
   *                              on |g_utf8_strlen()| what is one character.
   */
  nsresult DeleteText(GtkIMContext* aContext, int32_t aOffset,
                      uint32_t aNChars);

  // Called before destroying the context to work around some platform bugs.
  void PrepareToDestroyContext(GtkIMContext* aContext);

  /**
   *  WARNING:
   *    Following methods dispatch gecko events.  Then, the focused widget
   *    can be destroyed, and also it can be stolen focus.  If they returns
   *    FALSE, callers cannot continue the composition.
   *      - MaybeDispatchKeyEventAsProcessedByIME
   *      - DispatchCompositionStart
   *      - DispatchCompositionChangeEvent
   *      - DispatchCompositionCommitEvent
   */

  /**
   * Dispatch an eKeyDown or eKeyUp event whose mKeyCode value is
   * NS_VK_PROCESSKEY and mKeyNameIndex is KEY_NAME_INDEX_Process if
   * we're not in a dead key sequence, mProcessingKeyEvent is nullptr
   * but mPostingKeyEvents is not empty or mProcessingKeyEvent is not
   * nullptr and mKeyboardEventWasDispatched is still false.  If this
   * dispatches a keyboard event, this sets mKeyboardEventWasDispatched
   * to true.
   *
   * @param aFollowingEvent       The following event message.
   * @return                      If the caller can continue to handle
   *                              composition, returns true.  Otherwise,
   *                              false.  For example, if focus is moved
   *                              by dispatched keyboard event, returns
   *                              false.
   */
  bool MaybeDispatchKeyEventAsProcessedByIME(EventMessage aFollowingEvent);

  /**
   * Dispatches eKeyDown and eKeyPress events for committed character.
   * Optionally dispatches eKeyUp if aDispatchKeyUp is true (needed for
   * Wayland text-input protocol where there's no GDK key release event).
   *
   * @param aKeyEvent           The keyboard event to dispatch.
   * @param aDispatchKeyUp      If true, also dispatch eKeyUp event.
   * @return                    Always returns true (caller should return).
   */
  bool DispatchKeyEventsForCommittedCharacter(WidgetKeyboardEvent& aKeyEvent,
                                              bool aDispatchKeyUp);

  /**
   * Dispatches a composition start event.
   *
   * @param aContext              A GtkIMContext which is being handled.
   * @return                      true if the focused widget is neither
   *                              destroyed nor changed.  Otherwise, false.
   */
  bool DispatchCompositionStart(GtkIMContext* aContext);

  /**
   * Dispatches a compositionchange event.
   *
   * @param aContext              A GtkIMContext which is being handled.
   * @param aCompositionString    New composition string.
   * @return                      true if the focused widget is neither
   *                              destroyed nor changed.  Otherwise, false.
   */
  bool DispatchCompositionChangeEvent(GtkIMContext* aContext,
                                      const nsAString& aCompositionString);

  /**
   * Dispatches a compositioncommit event or compositioncommitasis event.
   *
   * @param aContext              A GtkIMContext which is being handled.
   * @param aCommitString         If this is nullptr, the composition will
   *                              be committed with last dispatched data.
   *                              Otherwise, the composition will be
   *                              committed with this value.
   * @return                      true if the focused widget is neither
   *                              destroyed nor changed.  Otherwise, false.
   */
  bool DispatchCompositionCommitEvent(GtkIMContext* aContext,
                                      const nsAString* aCommitString = nullptr);
};

}  // namespace widget
}  // namespace mozilla

#endif  // #ifndef IMContextWrapper_h_
