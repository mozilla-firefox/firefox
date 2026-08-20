/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_widget_IMEData_h_
#define mozilla_widget_IMEData_h_

#include <fmt/format.h>

#include "Units.h"
#include "mozilla/CheckedInt.h"
#include "mozilla/EnumSet.h"
#include "mozilla/EnumTypeTraits.h"
#include "mozilla/EventForwards.h"
#include "mozilla/NativeKeyBindingsType.h"
#include "nsCOMPtr.h"
#include "nsIURI.h"
#include "nsPoint.h"
#include "nsRect.h"
#include "nsString.h"
#include "nsXULAppAPI.h"

class nsIWidget;

namespace mozilla {

class ContentSelection;
class WritingMode;

template <class T>
class Maybe;

// Helper class to logging string which may contain various Unicode characters
// and/or may be too long string for logging.
class MOZ_STACK_CLASS PrintStringDetail : public nsAutoCString {
 public:
  static constexpr uint32_t kMaxLengthForCompositionString = 8;
  static constexpr uint32_t kMaxLengthForSelectedString = 12;
  static constexpr uint32_t kMaxLengthForEditor = 20;

  PrintStringDetail() = delete;
  explicit PrintStringDetail(const nsAString& aString,
                             uint32_t aMaxLength = UINT32_MAX);
  template <typename StringType>
  explicit PrintStringDetail(const Maybe<StringType>& aMaybeString,
                             uint32_t aMaxLength = UINT32_MAX);

  friend std::string format_as(const PrintStringDetail&);
  friend std::ostream& operator<<(std::ostream&, const PrintStringDetail&);

 private:
  static nsCString PrintCharData(char32_t aChar);
};

// StartAndEndOffsets represents a range in flat-text.
template <typename IntType>
class StartAndEndOffsets {
 protected:
  static IntType MaxOffset() { return std::numeric_limits<IntType>::max(); }

 public:
  StartAndEndOffsets() = delete;
  explicit StartAndEndOffsets(IntType aStartOffset, IntType aEndOffset)
      : mStartOffset(aStartOffset),
        mEndOffset(aStartOffset <= aEndOffset ? aEndOffset : aStartOffset) {
    MOZ_ASSERT(aStartOffset <= mEndOffset);
  }

  IntType StartOffset() const { return mStartOffset; }
  IntType Length() const { return mEndOffset - mStartOffset; }
  IntType EndOffset() const { return mEndOffset; }

  bool IsOffsetInRange(IntType aOffset) const {
    return aOffset >= mStartOffset && aOffset < mEndOffset;
  }
  bool IsOffsetInRangeOrEndOffset(IntType aOffset) const {
    return aOffset >= mStartOffset && aOffset <= mEndOffset;
  }

  void MoveTo(IntType aNewStartOffset) {
    auto delta = static_cast<int64_t>(mStartOffset) - aNewStartOffset;
    mStartOffset += delta;
    mEndOffset += delta;
  }
  void SetOffsetAndLength(IntType aNewOffset, IntType aNewLength) {
    mStartOffset = aNewOffset;
    CheckedInt<IntType> endOffset(aNewOffset + aNewLength);
    mEndOffset = endOffset.isValid() ? endOffset.value() : MaxOffset();
  }
  void SetEndOffset(IntType aEndOffset) {
    MOZ_ASSERT(mStartOffset <= aEndOffset);
    mEndOffset = std::max(aEndOffset, mStartOffset);
  }
  void SetStartAndEndOffsets(IntType aStartOffset, IntType aEndOffset) {
    MOZ_ASSERT(aStartOffset <= aEndOffset);
    mStartOffset = aStartOffset;
    mEndOffset = aStartOffset <= aEndOffset ? aEndOffset : aStartOffset;
  }
  void SetLength(IntType aNewLength) {
    CheckedInt<IntType> endOffset(mStartOffset + aNewLength);
    mEndOffset = endOffset.isValid() ? endOffset.value() : MaxOffset();
  }

  friend std::ostream& operator<<(
      std::ostream& aStream,
      const StartAndEndOffsets<IntType>& aStartAndEndOffsets) {
    aStream << "{ mStartOffset=" << aStartAndEndOffsets.mStartOffset
            << ", mEndOffset=" << aStartAndEndOffsets.mEndOffset
            << ", Length()=" << aStartAndEndOffsets.Length() << " }";
    return aStream;
  }

 private:
  IntType mStartOffset;
  IntType mEndOffset;
};

// OffsetAndData class is designed for storing composition string and its
// start offset.  Length() and EndOffset() return only valid length or
// offset.  I.e., if the string is too long for inserting at the offset,
// the length is shrunken.  However, the string itself is not shrunken.
// Therefore, moving it to where all of the string can be contained,
// they will return longer/bigger value.
enum class OffsetAndDataFor {
  CompositionString,
  SelectedString,
  EditorString,
};
template <typename IntType>
class OffsetAndData {
 protected:
  static IntType MaxOffset() { return std::numeric_limits<IntType>::max(); }

 public:
  OffsetAndData() = delete;
  explicit OffsetAndData(
      IntType aStartOffset, const nsAString& aData,
      OffsetAndDataFor aFor = OffsetAndDataFor::CompositionString)
      : mData(aData), mOffset(aStartOffset), mFor(aFor) {}

  bool IsValid() const {
    CheckedInt<IntType> offset(mOffset);
    offset += mData.Length();
    return offset.isValid();
  }
  IntType StartOffset() const { return mOffset; }
  IntType Length() const {
    CheckedInt<IntType> endOffset(CheckedInt<IntType>(mOffset) +
                                  mData.Length());
    return endOffset.isValid() ? mData.Length() : MaxOffset() - mOffset;
  }
  IntType EndOffset() const { return mOffset + Length(); }
  StartAndEndOffsets<IntType> CreateStartAndEndOffsets() const {
    return StartAndEndOffsets<IntType>(StartOffset(), EndOffset());
  }
  const nsString& DataRef() const {
    // In strictly speaking, we should return substring which may be shrunken
    // for rounding to the max offset.  However, it's unrealistic edge case,
    // and creating new string is not so cheap job in a hot path.  Therefore,
    // this just returns the data as-is.
    return mData;
  }
  bool IsDataEmpty() const { return mData.IsEmpty(); }

  bool IsOffsetInRange(IntType aOffset) const {
    return aOffset >= mOffset && aOffset < EndOffset();
  }
  bool IsOffsetInRangeOrEndOffset(IntType aOffset) const {
    return aOffset >= mOffset && aOffset <= EndOffset();
  }

  void Collapse(IntType aOffset) {
    mOffset = aOffset;
    mData.Truncate();
  }
  void MoveTo(IntType aNewOffset) { mOffset = aNewOffset; }
  void SetOffsetAndData(IntType aStartOffset, const nsAString& aData) {
    mOffset = aStartOffset;
    mData = aData;
  }
  void SetData(const nsAString& aData) { mData = aData; }
  void TruncateData(uint32_t aLength = 0) { mData.Truncate(aLength); }
  void ReplaceData(nsAString::size_type aCutStart,
                   nsAString::size_type aCutLength,
                   const nsAString& aNewString) {
    mData.Replace(aCutStart, aCutLength, aNewString);
  }

  friend std::ostream& operator<<(
      std::ostream& aStream, const OffsetAndData<IntType>& aOffsetAndData) {
    const auto maxDataLength =
        aOffsetAndData.mFor == OffsetAndDataFor::CompositionString
            ? PrintStringDetail::kMaxLengthForCompositionString
            : (aOffsetAndData.mFor == OffsetAndDataFor::SelectedString
                   ? PrintStringDetail::kMaxLengthForSelectedString
                   : PrintStringDetail::kMaxLengthForEditor);
    aStream << "{ mOffset=" << aOffsetAndData.mOffset << ", mData="
            << PrintStringDetail(aOffsetAndData.mData, maxDataLength).get()
            << ", Length()=" << aOffsetAndData.Length()
            << ", EndOffset()=" << aOffsetAndData.EndOffset() << " }";
    return aStream;
  }

 private:
  nsString mData;
  IntType mOffset;
  OffsetAndDataFor mFor;
};

namespace widget {

/**
 * Preference for receiving IME updates
 *
 * If mWantUpdates is not empty, IMEContentObserver will observe text
 * change and/or selection change and call nsIWidget::NotifyIME() with
 * NOTIFY_IME_OF_SELECTION_CHANGE and/or NOTIFY_IME_OF_TEXT_CHANGE.
 * Please note that the text change observing cost is very expensive especially
 * on an HTML editor has focus.
 * If the IME implementation on a particular platform doesn't care about
 * NOTIFY_IME_OF_SELECTION_CHANGE and/or NOTIFY_IME_OF_TEXT_CHANGE,
 * they should set mWantUpdates to empty to avoid the cost.
 * If the IME implementation needs notifications even while our process is
 * inactive, it should also set NotifyDuringInactive.
 */
enum class IMENotificationRequest : uint8_t {
  TextChange,
  PositionChange,
  // NOTIFY_MOUSE_BUTTON_EVENT_ON_CHAR is used when mouse button is pressed
  // or released on a character in the focused editor.  The notification is
  // notified to IME as a mouse event.  If it's consumed by IME, NotifyIME()
  // returns NS_SUCCESS_EVENT_CONSUMED.  Otherwise, it returns NS_OK if it's
  // handled without any error.
  MouseEventOnChar,
  // NOTE: NotifyDuringInactive isn't supported in environments where two
  //       or more compositions are possible.  E.g., Mac and Linux (GTK).
  NotifyDuringInactive,
};

}  // namespace widget

template <>
struct MaxEnumValue<widget::IMENotificationRequest> {
  static constexpr uint8_t value = static_cast<uint8_t>(
      widget::IMENotificationRequest::NotifyDuringInactive);
};

namespace widget {

using IMENotificationRequests = EnumSet<IMENotificationRequest>;
inline constexpr const IMENotificationRequests AllIMENotificationRequests = {
    IMENotificationRequest::TextChange, IMENotificationRequest::PositionChange,
    IMENotificationRequest::MouseEventOnChar};

/**
 * IME enabled states.
 *
 * WARNING: If you change these values, you also need to edit:
 *   nsIDOMWindowUtils.idl
 */
enum class IMEEnabled {
  /**
   * 'Disabled' means the user cannot use IME. So, the IME open state should
   * be 'closed' during 'disabled'.
   */
  Disabled,
  /**
   * 'Enabled' means the user can use IME.
   */
  Enabled,
  /**
   * 'Password' state is a special case for the password editors.
   * E.g., on mac, the password editors should disable the non-Roman
   * keyboard layouts at getting focus. Thus, the password editor may have
   * special rules on some platforms.
   */
  Password,
  /**
   * 'Unknown' is useful when you cache this enum.  So, this shouldn't be
   * used with nsIWidget::SetInputContext().
   */
  Unknown,
};

/**
 * Contains IMEStatus plus information about the current
 * input context that the IME can use as hints if desired.
 */

struct IMEState final {
  IMEEnabled mEnabled;

  /**
   * IME open states the mOpen value of SetInputContext() should be one value of
   * OPEN, CLOSE or DONT_CHANGE_OPEN_STATE.  GetInputContext() should return
   * OPEN, CLOSE or OPEN_STATE_NOT_SUPPORTED.
   */
  enum Open {
    /**
     * 'Unsupported' means the platform cannot return actual IME open state.
     * This value is used only by GetInputContext().
     */
    OPEN_STATE_NOT_SUPPORTED,
    /**
     * 'Don't change' means the widget shouldn't change IME open state when
     * SetInputContext() is called.
     */
    DONT_CHANGE_OPEN_STATE = OPEN_STATE_NOT_SUPPORTED,
    /**
     * 'Open' means that IME should compose in its primary language (or latest
     * input mode except direct ASCII character input mode).  Even if IME is
     * opened by this value, users should be able to close IME by theirselves.
     * Web contents can specify this value by |ime-mode: active;|.
     */
    OPEN,
    /**
     * 'Closed' means that IME shouldn't handle key events (or should handle
     * as ASCII character inputs on mobile device).  Even if IME is closed by
     * this value, users should be able to open IME by theirselves.
     * Web contents can specify this value by |ime-mode: inactive;|.
     */
    CLOSED
  };
  Open mOpen;

  IMEState() : mEnabled(IMEEnabled::Enabled), mOpen(DONT_CHANGE_OPEN_STATE) {}

  explicit IMEState(IMEEnabled aEnabled, Open aOpen = DONT_CHANGE_OPEN_STATE)
      : mEnabled(aEnabled), mOpen(aOpen) {}

  // Returns true if the user can input characters.
  // This means that a plain text editor, an HTML editor, a password editor or
  // a plain text editor whose ime-mode is "disabled".
  bool IsEditable() const {
    return mEnabled == IMEEnabled::Enabled || mEnabled == IMEEnabled::Password;
  }
};

// NS_ONLY_ONE_NATIVE_IME_CONTEXT is a special value of native IME context.
// If there can be only one IME composition in a process, this can be used.
#define NS_ONLY_ONE_NATIVE_IME_CONTEXT \
  (reinterpret_cast<void*>(static_cast<intptr_t>(-1)))

struct NativeIMEContext final {
  // Pointer to native IME context.  Typically this is the result of
  // nsIWidget::GetNativeData(NS_RAW_NATIVE_IME_CONTEXT) in the parent process.
  // See also NS_ONLY_ONE_NATIVE_IME_CONTEXT.
  uintptr_t mRawNativeIMEContext;
  // Process ID of the origin of mNativeIMEContext.
  // static_cast<uint64_t>(-1) if the instance is not initialized properly.
  // 0 if the instance is originated in the parent process.
  // 1 or greater if the instance is originated in a content process.
  uint64_t mOriginProcessID;

  NativeIMEContext() : mRawNativeIMEContext(0), mOriginProcessID(0) {
    Init(nullptr);
  }

  explicit NativeIMEContext(nsIWidget* aWidget)
      : mRawNativeIMEContext(0), mOriginProcessID(0) {
    Init(aWidget);
  }

  bool IsValid() const {
    return mRawNativeIMEContext &&
           mOriginProcessID != static_cast<uint64_t>(-1);
  }

  bool IsOriginatedInParentProcess() const {
    return mOriginProcessID != 0 &&
           mOriginProcessID != static_cast<uint64_t>(-1);
  }

  void Init(nsIWidget* aWidget);
  void InitWithRawNativeIMEContext(const void* aRawNativeIMEContext) {
    InitWithRawNativeIMEContext(const_cast<void*>(aRawNativeIMEContext));
  }
  void InitWithRawNativeIMEContext(void* aRawNativeIMEContext);

  bool operator==(const NativeIMEContext& aOther) const {
    return mRawNativeIMEContext == aOther.mRawNativeIMEContext &&
           mOriginProcessID == aOther.mOriginProcessID;
  }
  bool operator!=(const NativeIMEContext& aOther) const {
    return !(*this == aOther);
  }

  friend auto format_as(const NativeIMEContext& aContext) {
    return fmt::format("{{ mRawNativeIMEContext={}, mOriginProcessID={} }}",
                       reinterpret_cast<void*>(aContext.mRawNativeIMEContext),
                       aContext.mOriginProcessID);
  }
  friend std::ostream& operator<<(std::ostream& aStream,
                                  const NativeIMEContext& aContext) {
    return aStream << format_as(aContext);
  }
};

struct InputContext final {
  InputContext()
      : mOrigin(XRE_IsParentProcess() ? ORIGIN_MAIN : ORIGIN_CONTENT),
        mHasHandledUserInput(false),
        mInPrivateBrowsing(false) {}

  // If InputContext instance is a static variable, any heap allocated stuff
  // of its members need to be deleted at XPCOM shutdown.  Otherwise, it's
  // detected as memory leak.
  void ShutDown() {
    mURI = nullptr;
    mHTMLInputType.Truncate();
    mHTMLInputMode.Truncate();
    mActionHint.Truncate();
    mAutocapitalize.Truncate();
    mAutocorrect = true;
  }

  bool IsPasswordEditor() const {
    return mHTMLInputType.LowerCaseEqualsLiteral("password");
  }

  NativeKeyBindingsType GetNativeKeyBindingsType() const {
    MOZ_DIAGNOSTIC_ASSERT(mIMEState.IsEditable());
    // See GetInputType in IMEStateManager.cpp
    if (mHTMLInputType.IsEmpty()) {
      return NativeKeyBindingsType::RichTextEditor;
    }
    return mHTMLInputType.EqualsLiteral("textarea")
               ? NativeKeyBindingsType::MultiLineEditor
               : NativeKeyBindingsType::SingleLineEditor;
  }

  // https://html.spec.whatwg.org/dev/interaction.html#autocapitalization
  bool IsAutocapitalizeSupported() const {
    return !mHTMLInputType.EqualsLiteral("password") &&
           !mHTMLInputType.EqualsLiteral("url") &&
           !mHTMLInputType.EqualsLiteral("email");
  }

  bool IsInputAttributeChanged(const InputContext& aOldContext) const {
    return mIMEState.mEnabled != aOldContext.mIMEState.mEnabled ||
#if defined(ANDROID) || defined(MOZ_WIDGET_GTK) || defined(XP_WIN) || \
    defined(XP_IOS)
           // input type and inputmode are supported by Windows IME API, GTK
           // IME API, Android IME API and iOS API.
           mHTMLInputType != aOldContext.mHTMLInputType ||
           mHTMLInputMode != aOldContext.mHTMLInputMode ||
#endif
#if defined(ANDROID) || defined(MOZ_WIDGET_GTK) || defined(XP_IOS)
           // autocapitalize is supported by Android IME API, GTK IME API, and
           // iOS API
           mAutocapitalize != aOldContext.mAutocapitalize ||
#endif
#if defined(ANDROID) || defined(XP_IOS)
           // enterkeyhint is only supported by Android IME API and iOS API.
           mActionHint != aOldContext.mActionHint ||
#endif
#if defined(ANDROID) || defined(XP_DARWIN)
           // autocorrect is only supported by Android IME API, macOS text
           // substitution and iOS API.
           mAutocorrect != aOldContext.mAutocorrect ||
#endif
           false;
  }

  IMEState mIMEState;

  // The URI of the document which has the editable element.
  nsCOMPtr<nsIURI> mURI;

  /* The type of the input if the input is a html input field */
  nsString mHTMLInputType;

  // The value of the inputmode
  nsString mHTMLInputMode;

  /* A hint for the action that is performed when the input is submitted */
  nsString mActionHint;

  /* A hint for autocapitalize */
  nsString mAutocapitalize;

  /* A hint for autocorrect */
  bool mAutocorrect = true;  // on-by-default

  /**
   * mOrigin indicates whether this focus event refers to main or remote
   * content.
   */
  enum Origin {
    // Adjusting focus of content on the main process
    ORIGIN_MAIN,
    // Adjusting focus of content in a remote process
    ORIGIN_CONTENT
  };
  Origin mOrigin;

  /**
   * True if the document has ever received user input
   */
  bool mHasHandledUserInput;

  /* Whether the owning document of the input element has been loaded
   * in private browsing mode. */
  bool mInPrivateBrowsing;

  bool IsOriginMainProcess() const { return mOrigin == ORIGIN_MAIN; }

  bool IsOriginContentProcess() const { return mOrigin == ORIGIN_CONTENT; }

  bool IsOriginCurrentProcess() const {
    if (XRE_IsParentProcess()) {
      return IsOriginMainProcess();
    }
    return IsOriginContentProcess();
  }
};

// FYI: Implemented in nsIWidget.cpp
const char* ToChar(InputContext::Origin aOrigin);

struct InputContextAction final {
  /**
   * mCause indicates what action causes calling nsIWidget::SetInputContext().
   * It must be one of following values.
   */
  enum Cause {
    // The cause is unknown but originated from content. Focus might have been
    // changed by content script.
    CAUSE_UNKNOWN,
    // The cause is unknown but originated from chrome. Focus might have been
    // changed by chrome script.
    CAUSE_UNKNOWN_CHROME,
    // The cause is user's keyboard operation.
    CAUSE_KEY,
    // The cause is user's mouse operation.
    CAUSE_MOUSE,
    // The cause is user's touch operation (implies mouse)
    CAUSE_TOUCH,
    // The cause is users' long press operation.
    CAUSE_LONGPRESS,
    // The cause is unknown but it occurs during user input except keyboard
    // input.  E.g., an event handler of a user input event moves focus.
    CAUSE_UNKNOWN_DURING_NON_KEYBOARD_INPUT,
    // The cause is unknown but it occurs during keyboard input.
    CAUSE_UNKNOWN_DURING_KEYBOARD_INPUT,
  };
  Cause mCause;

  /**
   * mFocusChange indicates what happened for focus.
   */
  enum FocusChange {
    FOCUS_NOT_CHANGED,
    // A content got focus.
    GOT_FOCUS,
    // Focused content lost focus.
    LOST_FOCUS,
    // Menu got pseudo focus that means focused content isn't changed but
    // keyboard events will be handled by menu.
    MENU_GOT_PSEUDO_FOCUS,
    // Menu lost pseudo focus that means focused content will handle keyboard
    // events.
    MENU_LOST_PSEUDO_FOCUS,
    // The widget is created.  When a widget is crated, it may need to notify
    // IME module to initialize its native IME context.  In such case, this is
    // used.  I.e., this isn't used by IMEStateManager.
    WIDGET_CREATED
  };
  FocusChange mFocusChange;

  bool ContentGotFocusByTrustedCause() const {
    return (mFocusChange == GOT_FOCUS && mCause != CAUSE_UNKNOWN);
  }

  bool UserMightRequestOpenVKB() const {
    // If focus is changed, user must not request to open VKB.
    if (mFocusChange != FOCUS_NOT_CHANGED) {
      return false;
    }
    switch (mCause) {
      // If user clicks or touches focused editor, user must request to open
      // VKB.
      case CAUSE_MOUSE:
      case CAUSE_TOUCH:
      // If script does something during a user input and that causes changing
      // input context, user might request to open VKB.  E.g., user clicks
      // dummy editor and JS moves focus to an actual editable node.  However,
      // this should return false if the user input is a keyboard event since
      // physical keyboard operation shouldn't cause opening VKB.
      case CAUSE_UNKNOWN_DURING_NON_KEYBOARD_INPUT:
        return true;
      default:
        return false;
    }
  }

  /**
   * IsHandlingUserInput() returns true if it's caused by a user action directly
   * or it's caused by script or something but it occurred while we're handling
   * a user action.  E.g., when it's caused by Element.focus() in an event
   * handler of a user input, this returns true.
   */
  static bool IsHandlingUserInput(Cause aCause) {
    switch (aCause) {
      case CAUSE_KEY:
      case CAUSE_MOUSE:
      case CAUSE_TOUCH:
      case CAUSE_LONGPRESS:
      case CAUSE_UNKNOWN_DURING_NON_KEYBOARD_INPUT:
      case CAUSE_UNKNOWN_DURING_KEYBOARD_INPUT:
        return true;
      default:
        return false;
    }
  }

  bool IsHandlingUserInput() const { return IsHandlingUserInput(mCause); }

  InputContextAction()
      : mCause(CAUSE_UNKNOWN), mFocusChange(FOCUS_NOT_CHANGED) {}

  explicit InputContextAction(Cause aCause,
                              FocusChange aFocusChange = FOCUS_NOT_CHANGED)
      : mCause(aCause), mFocusChange(aFocusChange) {}
};

// IMEMessage is shared by IMEStateManager and TextComposition.
// Update values in GeckoEditable.java if you make changes here.
// XXX Negative values are used in Android...
using IMEMessageType = int8_t;
enum IMEMessage : IMEMessageType {
  // This is used by IMENotification internally.  This means that the instance
  // hasn't been initialized yet.
  NOTIFY_IME_OF_NOTHING,
  // An editable content is getting focus
  NOTIFY_IME_OF_FOCUS,
  // An editable content is losing focus
  NOTIFY_IME_OF_BLUR,
  // Selection in the focused editable content is changed
  NOTIFY_IME_OF_SELECTION_CHANGE,
  // Text in the focused editable content is changed
  NOTIFY_IME_OF_TEXT_CHANGE,
  // Notified when a dispatched composition event is handled by the
  // contents.  This must be notified after the other notifications.
  // Note that if a remote process has focus, this is notified only once when
  // all dispatched events are handled completely.  So, the receiver shouldn't
  // count number of received this notification for comparing with the number
  // of dispatched events.
  // NOTE: If a composition event causes moving focus from the focused editor,
  //       this notification may not be notified as usual.  Even in such case,
  //       NOTIFY_IME_OF_BLUR is always sent.  So, notification listeners
  //       should tread the blur notification as including this if there is
  //       pending composition events.
  NOTIFY_IME_OF_COMPOSITION_EVENT_HANDLED,
  // Position or size of focused element may be changed.
  NOTIFY_IME_OF_POSITION_CHANGE,
  // Mouse button event is fired on a character in focused editor
  NOTIFY_IME_OF_MOUSE_BUTTON_EVENT,
  // Request to commit current composition to IME
  // (some platforms may not support)
  REQUEST_TO_COMMIT_COMPOSITION,
  // Request to cancel current composition to IME
  // (some platforms may not support)
  REQUEST_TO_CANCEL_COMPOSITION
};

// FYI: Implemented in nsIWidget.cpp
const char* ToChar(IMEMessage aIMEMessage);

struct IMENotification final {
  IMENotification() : mMessage(NOTIFY_IME_OF_NOTHING), mSelectionChangeData() {}

  IMENotification(const IMENotification& aOther)
      : mMessage(NOTIFY_IME_OF_NOTHING) {
    Assign(aOther);
  }

  ~IMENotification() { Clear(); }

  MOZ_IMPLICIT IMENotification(IMEMessage aMessage)
      : mMessage(aMessage), mSelectionChangeData() {
    switch (aMessage) {
      case NOTIFY_IME_OF_SELECTION_CHANGE:
        mSelectionChangeData.mString = new nsString();
        mSelectionChangeData.Clear();
        break;
      case NOTIFY_IME_OF_TEXT_CHANGE:
        mTextChangeData.Clear();
        break;
      case NOTIFY_IME_OF_MOUSE_BUTTON_EVENT:
        mMouseButtonEventData.mEventMessage = eVoidEvent;
        mMouseButtonEventData.mOffset = UINT32_MAX;
        mMouseButtonEventData.mCursorPos.MoveTo(0, 0);
        mMouseButtonEventData.mCharRect.SetRect(0, 0, 0, 0);
        mMouseButtonEventData.mButton = -1;
        mMouseButtonEventData.mButtons = 0;
        mMouseButtonEventData.mModifiers = 0;
        break;
      default:
        break;
    }
  }

  void Assign(const IMENotification& aOther) {
    bool changingMessage = mMessage != aOther.mMessage;
    if (changingMessage) {
      Clear();
      mMessage = aOther.mMessage;
    }
    switch (mMessage) {
      case NOTIFY_IME_OF_SELECTION_CHANGE:
        if (changingMessage) {
          mSelectionChangeData.mString = new nsString();
        }
        mSelectionChangeData.Assign(aOther.mSelectionChangeData);
        break;
      case NOTIFY_IME_OF_TEXT_CHANGE:
        mTextChangeData = aOther.mTextChangeData;
        break;
      case NOTIFY_IME_OF_MOUSE_BUTTON_EVENT:
        mMouseButtonEventData = aOther.mMouseButtonEventData;
        break;
      default:
        break;
    }
  }

  IMENotification& operator=(const IMENotification& aOther) {
    Assign(aOther);
    return *this;
  }

  void Clear() {
    if (mMessage == NOTIFY_IME_OF_SELECTION_CHANGE) {
      MOZ_ASSERT(mSelectionChangeData.mString);
      delete mSelectionChangeData.mString;
      mSelectionChangeData.mString = nullptr;
    }
    mMessage = NOTIFY_IME_OF_NOTHING;
  }

  bool HasNotification() const { return mMessage != NOTIFY_IME_OF_NOTHING; }

  void MergeWith(const IMENotification& aNotification) {
    switch (mMessage) {
      case NOTIFY_IME_OF_NOTHING:
        MOZ_ASSERT(aNotification.mMessage != NOTIFY_IME_OF_NOTHING);
        Assign(aNotification);
        break;
      case NOTIFY_IME_OF_SELECTION_CHANGE:
        MOZ_ASSERT(aNotification.mMessage == NOTIFY_IME_OF_SELECTION_CHANGE);
        mSelectionChangeData.Assign(aNotification.mSelectionChangeData);
        break;
      case NOTIFY_IME_OF_TEXT_CHANGE:
        MOZ_ASSERT(aNotification.mMessage == NOTIFY_IME_OF_TEXT_CHANGE);
        mTextChangeData += aNotification.mTextChangeData;
        break;
      case NOTIFY_IME_OF_POSITION_CHANGE:
      case NOTIFY_IME_OF_COMPOSITION_EVENT_HANDLED:
        MOZ_ASSERT(aNotification.mMessage == mMessage);
        break;
      default:
        MOZ_CRASH("Merging notification isn't supported");
        break;
    }
  }

  IMEMessage mMessage;

  // NOTIFY_IME_OF_SELECTION_CHANGE specific data
  struct SelectionChangeDataBase {
    // Selection range.
    uint32_t mOffset;

    // Selected string
    nsString* mString;

    // Writing mode at the selection.
    uint8_t mWritingModeBits;

    bool mIsInitialized;
    bool mHasRange;
    bool mReversed;
    bool mCausedByComposition;
    bool mCausedBySelectionEvent;
    bool mOccurredDuringComposition;

    void SetWritingMode(const WritingMode& aWritingMode);
    WritingMode GetWritingMode() const;

    uint32_t StartOffset() const {
      MOZ_ASSERT(mHasRange);
      return mOffset;
    }
    uint32_t EndOffset() const {
      MOZ_ASSERT(mHasRange);
      return mOffset + Length();
    }
    uint32_t AnchorOffset() const {
      MOZ_ASSERT(mHasRange);
      return mOffset + (mReversed ? Length() : 0);
    }
    uint32_t FocusOffset() const {
      MOZ_ASSERT(mHasRange);
      return mOffset + (mReversed ? 0 : Length());
    }
    const nsString& String() const {
      MOZ_ASSERT(mHasRange);
      return *mString;
    }
    uint32_t Length() const {
      MOZ_ASSERT(mHasRange);
      return mString->Length();
    }
    bool IsInInt32Range() const {
      return mHasRange && mOffset <= INT32_MAX && Length() <= INT32_MAX &&
             mOffset + Length() <= INT32_MAX;
    }
    bool HasRange() const { return mIsInitialized && mHasRange; }
    bool IsCollapsed() const { return !mHasRange || mString->IsEmpty(); }
    void ClearSelectionData() {
      mIsInitialized = false;
      mHasRange = false;
      mOffset = UINT32_MAX;
      mString->Truncate();
      mWritingModeBits = 0;
      mReversed = false;
    }
    void Clear() {
      ClearSelectionData();
      mCausedByComposition = false;
      mCausedBySelectionEvent = false;
      mOccurredDuringComposition = false;
    }
    bool IsInitialized() const { return mIsInitialized; }
    void Assign(const SelectionChangeDataBase& aOther) {
      mIsInitialized = aOther.mIsInitialized;
      mHasRange = aOther.mHasRange;
      if (mIsInitialized && mHasRange) {
        mOffset = aOther.mOffset;
        *mString = aOther.String();
        mReversed = aOther.mReversed;
        mWritingModeBits = aOther.mWritingModeBits;
      } else {
        mOffset = UINT32_MAX;
        mString->Truncate();
        mReversed = false;
        // Let's keep the writing mode for avoiding temporarily changing the
        // writing mode at no selection range.
      }
      AssignReason(aOther.mCausedByComposition, aOther.mCausedBySelectionEvent,
                   aOther.mOccurredDuringComposition);
    }
    void Assign(const WidgetQueryContentEvent& aQuerySelectedTextEvent);
    void AssignReason(bool aCausedByComposition, bool aCausedBySelectionEvent,
                      bool aOccurredDuringComposition) {
      mCausedByComposition = aCausedByComposition;
      mCausedBySelectionEvent = aCausedBySelectionEvent;
      mOccurredDuringComposition = aOccurredDuringComposition;
    }

    bool EqualsRange(const SelectionChangeDataBase& aOther) const {
      if (HasRange() != aOther.HasRange()) {
        return false;
      }
      if (!HasRange()) {
        return true;
      }
      return mOffset == aOther.mOffset && mString->Equals(*aOther.mString);
    }
    bool EqualsRangeAndDirection(const SelectionChangeDataBase& aOther) const {
      return EqualsRange(aOther) &&
             (!HasRange() || mReversed == aOther.mReversed);
    }
    bool EqualsRangeAndDirectionAndWritingMode(
        const SelectionChangeDataBase& aOther) const {
      return EqualsRangeAndDirection(aOther) &&
             mWritingModeBits == aOther.mWritingModeBits;
    }

    bool EqualsRange(const ContentSelection& aContentSelection) const;
    bool EqualsRangeAndWritingMode(
        const ContentSelection& aContentSelection) const;

    OffsetAndData<uint32_t> ToUint32OffsetAndData() const {
      return OffsetAndData<uint32_t>(mOffset, *mString,
                                     OffsetAndDataFor::SelectedString);
    }
  };

  // SelectionChangeDataBase cannot have constructors because it's used in
  // the union.  Therefore, SelectionChangeData should only implement
  // constructors.  In other words, add other members to
  // SelectionChangeDataBase.
  struct SelectionChangeData final : public SelectionChangeDataBase {
    SelectionChangeData() {
      mString = &mStringInstance;
      Clear();
    }
    explicit SelectionChangeData(const SelectionChangeDataBase& aOther) {
      mString = &mStringInstance;
      Assign(aOther);
    }
    SelectionChangeData(const SelectionChangeData& aOther) {
      mString = &mStringInstance;
      Assign(aOther);
    }
    SelectionChangeData& operator=(const SelectionChangeDataBase& aOther) {
      mString = &mStringInstance;
      Assign(aOther);
      return *this;
    }
    SelectionChangeData& operator=(const SelectionChangeData& aOther) {
      mString = &mStringInstance;
      Assign(aOther);
      return *this;
    }

   private:
    // When SelectionChangeData is used outside of union, it shouldn't create
    // nsString instance in the heap as far as possible.
    nsString mStringInstance;
  };

  struct TextChangeDataBase {
    // mStartOffset is the start offset of modified or removed text in
    // original content and inserted text in new content.
    uint32_t mStartOffset;
    // mRemovalEndOffset is the end offset of modified or removed text in
    // original content.  If the value is same as mStartOffset, no text hasn't
    // been removed yet.
    uint32_t mRemovedEndOffset;
    // mAddedEndOffset is the end offset of inserted text or same as
    // mStartOffset if just removed.  The vlaue is offset in the new content.
    uint32_t mAddedEndOffset;

    // Note that TextChangeDataBase may be the result of merging two or more
    // changes especially in e10s mode.

    // mCausedOnlyByComposition is true only when *all* merged changes are
    // caused by composition.
    bool mCausedOnlyByComposition;
    // mIncludingChangesDuringComposition is true if at least one change which
    // is not caused by composition occurred during the last composition.
    // Note that if after the last composition is finished and there are some
    // changes not caused by composition, this is set to false.
    bool mIncludingChangesDuringComposition;
    // mIncludingChangesWithoutComposition is true if there is at least one
    // change which did occur when there wasn't a composition ongoing.
    bool mIncludingChangesWithoutComposition;

    uint32_t OldLength() const {
      MOZ_ASSERT(IsValid());
      return mRemovedEndOffset - mStartOffset;
    }
    uint32_t NewLength() const {
      MOZ_ASSERT(IsValid());
      return mAddedEndOffset - mStartOffset;
    }

    // Positive if text is added. Negative if text is removed.
    int64_t Difference() const { return mAddedEndOffset - mRemovedEndOffset; }

    bool IsInInt32Range() const {
      MOZ_ASSERT(IsValid());
      return mStartOffset <= INT32_MAX && mRemovedEndOffset <= INT32_MAX &&
             mAddedEndOffset <= INT32_MAX;
    }

    bool IsValid() const {
      return !(mStartOffset == UINT32_MAX && !mRemovedEndOffset &&
               !mAddedEndOffset);
    }

    void Clear() {
      mStartOffset = UINT32_MAX;
      mRemovedEndOffset = mAddedEndOffset = 0;
    }

    void MergeWith(const TextChangeDataBase& aOther);
    TextChangeDataBase& operator+=(const TextChangeDataBase& aOther) {
      MergeWith(aOther);
      return *this;
    }

    [[nodiscard]] uint32_t ComputeNewOffset(uint32_t aOldOffset) const {
      // If mStartOffset equals or greater than aOldOffset, the text after
      // aOldOffset is changed, i.e., the preceding text is not changed.  So,
      // aOldOffset is not changed.
      if (mStartOffset >= aOldOffset) {
        return aOldOffset;
      }
      // If mRemovedEndOffset equals or less than aOldOffset, only the preceding
      // text is changed.  So, add the difference to aOldOffset.
      if (mRemovedEndOffset <= aOldOffset) {
        return aOldOffset + Difference();
      }
      // If the character at aOldOffset is changed, the new offset should be the
      // end of the changed text.
      return mAddedEndOffset;
    }

#ifdef DEBUG
    void Test();
#endif  // #ifdef DEBUG
  };

  // TextChangeDataBase cannot have constructors because they are used in union.
  // Therefore, TextChangeData should only implement constructor.  In other
  // words, add other members to TextChangeDataBase.
  struct TextChangeData : public TextChangeDataBase {
    TextChangeData() { Clear(); }

    TextChangeData(uint32_t aStartOffset, uint32_t aRemovedEndOffset,
                   uint32_t aAddedEndOffset, bool aCausedByComposition,
                   bool aOccurredDuringComposition) {
      MOZ_ASSERT(aRemovedEndOffset >= aStartOffset,
                 "removed end offset must not be smaller than start offset");
      MOZ_ASSERT(aAddedEndOffset >= aStartOffset,
                 "added end offset must not be smaller than start offset");
      mStartOffset = aStartOffset;
      mRemovedEndOffset = aRemovedEndOffset;
      mAddedEndOffset = aAddedEndOffset;
      mCausedOnlyByComposition = aCausedByComposition;
      mIncludingChangesDuringComposition =
          !aCausedByComposition && aOccurredDuringComposition;
      mIncludingChangesWithoutComposition =
          !aCausedByComposition && !aOccurredDuringComposition;
    }
  };

  struct MouseButtonEventData {
    // The value of WidgetEvent::mMessage
    EventMessage mEventMessage;
    // Character offset from the start of the focused editor under the cursor
    uint32_t mOffset;
    // Cursor position in pixels relative to the widget
    LayoutDeviceIntPoint mCursorPos;
    // Character rect in pixels under the cursor relative to the widget
    LayoutDeviceIntRect mCharRect;
    // The value of WidgetMouseEventBase::button and buttons
    int16_t mButton;
    int16_t mButtons;
    // The value of WidgetInputEvent::modifiers
    Modifiers mModifiers;
  };

  union {
    // NOTIFY_IME_OF_SELECTION_CHANGE specific data
    SelectionChangeDataBase mSelectionChangeData;

    // NOTIFY_IME_OF_TEXT_CHANGE specific data
    TextChangeDataBase mTextChangeData;

    // NOTIFY_IME_OF_MOUSE_BUTTON_EVENT specific data
    MouseButtonEventData mMouseButtonEventData;
  };

  void SetData(const SelectionChangeDataBase& aSelectionChangeData) {
    MOZ_RELEASE_ASSERT(mMessage == NOTIFY_IME_OF_SELECTION_CHANGE);
    mSelectionChangeData.Assign(aSelectionChangeData);
  }

  void SetData(const TextChangeDataBase& aTextChangeData) {
    MOZ_RELEASE_ASSERT(mMessage == NOTIFY_IME_OF_TEXT_CHANGE);
    mTextChangeData = aTextChangeData;
  }
};

std::ostream& operator<<(std::ostream& aStream, const IMEEnabled& aEnabled);
std::ostream& operator<<(std::ostream& aStream, const IMEState::Open& aOpen);
std::ostream& operator<<(std::ostream& aStream, const IMEState& aState);
std::ostream& operator<<(std::ostream& aStream,
                         const InputContext::Origin& aOrigin);
std::ostream& operator<<(std::ostream& aStream, const InputContext& aContext);
std::ostream& operator<<(std::ostream& aStream,
                         const InputContextAction::Cause& aCause);
std::ostream& operator<<(std::ostream& aStream,
                         const InputContextAction::FocusChange& aFocusChange);
std::ostream& operator<<(std::ostream& aStream,
                         const IMENotification::SelectionChangeDataBase& aData);
std::ostream& operator<<(std::ostream& aStream,
                         const IMENotification::TextChangeDataBase& aData);

}  // namespace widget
}  // namespace mozilla

#endif  // #ifndef mozilla_widget_IMEData_h_
