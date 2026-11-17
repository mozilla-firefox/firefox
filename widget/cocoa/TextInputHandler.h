/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef TextInputHandler_h_
#define TextInputHandler_h_

#include "nsCocoaUtils.h"

#import <Carbon/Carbon.h>
#import <Cocoa/Cocoa.h>
#include "WritingModes.h"
#include "mozView.h"
#include "mozilla/BasicEvents.h"
#include "mozilla/EventForwards.h"
#include "mozilla/Maybe.h"
#include "mozilla/TextEventDispatcherListener.h"
#include "nsCOMPtr.h"
#include "nsITimer.h"
#include "nsString.h"
#include "nsTArray.h"

class nsCocoaWindow;

namespace mozilla {
namespace widget {

// Key code constants
enum {
  kVK_PC_PrintScreen = kVK_F13,
  kVK_PC_ScrollLock = kVK_F14,
  kVK_PC_Pause = kVK_F15,

  kVK_PC_Insert = kVK_Help,
  kVK_PC_Backspace = kVK_Delete,
  kVK_PC_Delete = kVK_ForwardDelete,

  kVK_PC_ContextMenu = 0x6E,

  kVK_Powerbook_KeypadEnter =
      0x34  // Enter on Powerbook's keyboard is different
};

// Whether aCharCode is an ASCII control character, including DEL.
bool IsControlChar(uint32_t aCharCode);

/**
 * TISInputSourceWrapper is a wrapper for the TISInputSourceRef.  If we get the
 * TISInputSourceRef from InputSourceID, we need to release the CFArray instance
 * which is returned by TISCreateInputSourceList.  However, when we release the
 * list, we cannot access the TISInputSourceRef.  So, it's not usable, and it
 * may cause the memory leak bugs.  nsTISInputSource automatically releases the
 * list when the instance is destroyed.
 */
class TISInputSourceWrapper {
 public:
  static TISInputSourceWrapper& CurrentInputSource();

  /**
   * The character that aKeyCode produces on this input source with the given
   * native modifier flags, or 0 when it produces none or more than one.
   */
  uint32_t CharForKeyCode(UInt32 aKeyCode, UInt32 aModifiers) {
    return TranslateToChar(aKeyCode, aModifiers, GetKbdType());
  }
  /**
   * Shutdown() should be called when nobody doesn't need to use this class.
   */
  static void Shutdown();

  TISInputSourceWrapper()
      : mInputSource{nullptr},
        mKeyboardLayout{nullptr},
        mUCKeyboardLayout{nullptr},
        mIsRTL{0},
        mOverrideKeyboard{false} {
    mInputSourceList = nullptr;
    Clear();
  }

  explicit TISInputSourceWrapper(const char* aID)
      : mInputSource{nullptr},
        mKeyboardLayout{nullptr},
        mUCKeyboardLayout{nullptr},
        mIsRTL{0},
        mOverrideKeyboard{false} {
    mInputSourceList = nullptr;
    InitByInputSourceID(aID);
  }

  explicit TISInputSourceWrapper(SInt32 aLayoutID)
      : mInputSource{nullptr},
        mKeyboardLayout{nullptr},
        mUCKeyboardLayout{nullptr},
        mIsRTL{0},
        mOverrideKeyboard{false} {
    mInputSourceList = nullptr;
    InitByLayoutID(aLayoutID);
  }

  explicit TISInputSourceWrapper(TISInputSourceRef aInputSource)
      : mInputSource{nullptr},
        mKeyboardLayout{nullptr},
        mUCKeyboardLayout{nullptr},
        mIsRTL{0},
        mOverrideKeyboard{false} {
    mInputSourceList = nullptr;
    InitByTISInputSourceRef(aInputSource);
  }

  ~TISInputSourceWrapper() { Clear(); }

  void InitByInputSourceID(const char* aID);
  void InitByInputSourceID(const nsString& aID);
  void InitByInputSourceID(const CFStringRef aID);
  /**
   * InitByLayoutID() initializes the keyboard layout by the layout ID.
   *
   * @param aLayoutID             An ID of keyboard layout.
   *                                0: US
   *                                1: Greek
   *                                2: German
   *                                3: Swedish-Pro
   *                                4: Dvorak-Qwerty Cmd
   *                                5: Thai
   *                                6: Arabic
   *                                7: French
   *                                8: Hebrew
   *                                9: Lithuanian
   *                               10: Norwegian
   *                               11: Spanish
   * @param aOverrideKeyboard     When testing set to TRUE, otherwise, set to
   *                              FALSE.  When TRUE, we use an ANSI keyboard
   *                              instead of the actual keyboard.
   */
  void InitByLayoutID(SInt32 aLayoutID, bool aOverrideKeyboard = false);
  void InitByCurrentInputSource();
  void InitByCurrentKeyboardLayout();
  void InitByCurrentASCIICapableInputSource();
  void InitByCurrentASCIICapableKeyboardLayout();
  void InitByCurrentInputMethodKeyboardLayoutOverride();
  void InitByTISInputSourceRef(TISInputSourceRef aInputSource);
  void InitByLanguage(CFStringRef aLanguage);

  /**
   * If the instance is initialized with a keyboard layout input source,
   * returns it.
   * If the instance is initialized with an IME mode input source, the result
   * references the keyboard layout for the IME mode.  However, this can be
   * initialized only when the IME mode is actually selected.  I.e, if IME mode
   * input source is initialized with LayoutID or SourceID, this returns null.
   */
  TISInputSourceRef GetKeyboardLayoutInputSource() const {
    return mKeyboardLayout;
  }
  const UCKeyboardLayout* GetUCKeyboardLayout();

  bool IsOpenedIMEMode();
  bool IsIMEMode();
  bool IsKeyboardLayout();

  bool IsASCIICapable() {
    NS_ENSURE_TRUE(mInputSource, false);
    return GetBoolProperty(kTISPropertyInputSourceIsASCIICapable);
  }

  bool IsEnabled() {
    NS_ENSURE_TRUE(mInputSource, false);
    return GetBoolProperty(kTISPropertyInputSourceIsEnabled);
  }

  bool GetLanguageList(CFArrayRef& aLanguageList);
  bool GetPrimaryLanguage(CFStringRef& aPrimaryLanguage);
  bool GetPrimaryLanguage(nsAString& aPrimaryLanguage);

  bool GetLocalizedName(CFStringRef& aName) {
    NS_ENSURE_TRUE(mInputSource, false);
    return GetStringProperty(kTISPropertyLocalizedName, aName);
  }

  bool GetLocalizedName(nsAString& aName) {
    NS_ENSURE_TRUE(mInputSource, false);
    return GetStringProperty(kTISPropertyLocalizedName, aName);
  }

  bool GetInputSourceID(CFStringRef& aID) {
    NS_ENSURE_TRUE(mInputSource, false);
    return GetStringProperty(kTISPropertyInputSourceID, aID);
  }

  bool GetInputSourceID(nsAString& aID) {
    NS_ENSURE_TRUE(mInputSource, false);
    return GetStringProperty(kTISPropertyInputSourceID, aID);
  }

  bool GetBundleID(CFStringRef& aBundleID) {
    NS_ENSURE_TRUE(mInputSource, false);
    return GetStringProperty(kTISPropertyBundleID, aBundleID);
  }

  bool GetBundleID(nsAString& aBundleID) {
    NS_ENSURE_TRUE(mInputSource, false);
    return GetStringProperty(kTISPropertyBundleID, aBundleID);
  }

  bool GetInputSourceType(CFStringRef& aType) {
    NS_ENSURE_TRUE(mInputSource, false);
    return GetStringProperty(kTISPropertyInputSourceType, aType);
  }

  bool GetInputSourceType(nsAString& aType) {
    NS_ENSURE_TRUE(mInputSource, false);
    return GetStringProperty(kTISPropertyInputSourceType, aType);
  }

  bool IsForRTLLanguage();
  bool IsForJapaneseLanguage();
  bool IsInitializedByCurrentInputSource();

  enum {
    // 40 is an actual result of the ::LMGetKbdType() when we connect an
    // unknown keyboard and set the keyboard type to ANSI manually on the
    // set up dialog.
    eKbdType_ANSI = 40
  };

  void Select();
  void Clear();

  /**
   * InitKeyEvent() initializes aKeyEvent for aNativeKeyEvent.
   *
   * @param aNativeKeyEvent       A native key event for which you want to
   *                              dispatch a Gecko key event.
   * @param aKeyEvent             The result -- a Gecko key event initialized
   *                              from the native key event.
   * @param aIsProcessedByIME     true if aNativeKeyEvent has been handled
   *                              by IME (but except if the composition was
   *                              started with dead key).
   * @param aInsertString         If caller expects that the event will cause
   *                              a character to be input (say in an editor),
   *                              the caller should set this.  Otherwise,
   *                              if caller sets null to this, this method will
   *                              compute the character to be input from
   *                              characters of aNativeKeyEvent.
   */
  void InitKeyEvent(NSEvent* aNativeKeyEvent, WidgetKeyboardEvent& aKeyEvent,
                    bool aIsProcessedByIME,
                    const nsAString* aInsertString = nullptr);

  /**
   * WillDispatchKeyboardEvent() computes aKeyEvent.mAlternativeCharCodes and
   * recompute aKeyEvent.mCharCode if it's necessary.
   *
   * @param aNativeKeyEvent       A native key event for which you want to
   *                              dispatch a Gecko key event.
   * @param aInsertString         If caller expects that the event will cause
   *                              a character to be input (say in an editor),
   *                              the caller should set this.  Otherwise,
   *                              if caller sets null to this, this method will
   *                              compute the character to be input from
   *                              characters of aNativeKeyEvent.
   * @param aIndexOfKeypress      Index of the eKeyPress event.  If a key
   *                              inputs 2 or more characters, eKeyPress events
   *                              are dispatched for each character.  This is
   *                              0 for the first eKeyPress event.
   * @param aKeyEvent             The result -- a Gecko key event initialized
   *                              from the native key event.  This must be
   *                              eKeyPress event.
   */
  void WillDispatchKeyboardEvent(NSEvent* aNativeKeyEvent,
                                 const nsAString* aInsertString,
                                 uint32_t aIndexOfKeypress,
                                 WidgetKeyboardEvent& aKeyEvent);

  /**
   * ComputeGeckoKeyCode() returns Gecko keycode for aNativeKeyCode on current
   * keyboard layout.
   *
   * @param aNativeKeyCode        A native keycode.
   * @param aKbType               A native Keyboard Type value.  Typically,
   *                              this is a result of ::LMGetKbdType().
   * @param aCmdIsPressed         TRUE if Cmd key is pressed.  Otherwise, FALSE.
   * @return                      The computed Gecko keycode.
   */
  uint32_t ComputeGeckoKeyCode(UInt32 aNativeKeyCode, UInt32 aKbType,
                               bool aCmdIsPressed);

  /**
   * ComputeGeckoKeyNameIndex() returns Gecko key name index for the key.
   *
   * @param aNativeKeyCode        A native keycode.
   */
  static KeyNameIndex ComputeGeckoKeyNameIndex(UInt32 aNativeKeyCode);

  /**
   * ComputeGeckoCodeNameIndex() returns Gecko code name index for the key.
   *
   * @param aNativeKeyCode        A native keycode.
   * @param aKbType               A native Keyboard Type value.  Typically,
   *                              this is a result of ::LMGetKbdType().
   */
  static CodeNameIndex ComputeGeckoCodeNameIndex(UInt32 aNativeKeyCode,
                                                 UInt32 aKbType);

  /**
   * TranslateToChar() checks if aNativeKeyEvent is a dead key.
   *
   * @param aNativeKeyEvent       A native key event.
   * @return                      Returns true if the key event is a dead key
   *                              event.  Otherwise, false.
   */
  bool IsDeadKey(NSEvent* aNativeKeyEvent);

 protected:
  /**
   * TranslateToString() computes the inputted text from the native keyCode,
   * modifier flags and keyboard type.
   *
   * @param aKeyCode              A native keyCode.
   * @param aModifiers            Combination of native modifier flags.
   * @param aKbType               A native Keyboard Type value.  Typically,
   *                              this is a result of ::LMGetKbdType().
   * @param aStr                  Result, i.e., inputted text.
   *                              The result can be two or more characters.
   * @return                      If succeeded, TRUE.  Otherwise, FALSE.
   *                              Even if TRUE, aStr can be empty string.
   */
  bool TranslateToString(UInt32 aKeyCode, UInt32 aModifiers, UInt32 aKbType,
                         nsAString& aStr);

  /**
   * TranslateToChar() computes the inputted character from the native keyCode,
   * modifier flags and keyboard type.  If two or more characters would be
   * input, this returns 0.
   *
   * @param aKeyCode              A native keyCode.
   * @param aModifiers            Combination of native modifier flags.
   * @param aKbType               A native Keyboard Type value.  Typically,
   *                              this is a result of ::LMGetKbdType().
   * @return                      If succeeded and the result is one character,
   *                              returns the charCode of it.  Otherwise,
   *                              returns 0.
   */
  uint32_t TranslateToChar(UInt32 aKeyCode, UInt32 aModifiers, UInt32 aKbType);

  /**
   * TranslateToChar() checks if aKeyCode with aModifiers is a dead key.
   *
   * @param aKeyCode              A native keyCode.
   * @param aModifiers            Combination of native modifier flags.
   * @param aKbType               A native Keyboard Type value.  Typically,
   *                              this is a result of ::LMGetKbdType().
   * @return                      Returns true if the key with specified
   *                              modifier state is a dead key.  Otherwise,
   *                              false.
   */
  bool IsDeadKey(UInt32 aKeyCode, UInt32 aModifiers, UInt32 aKbType);

  /**
   * ComputeInsertString() computes string to be inserted with the key event.
   *
   * @param aNativeKeyEvent     The native key event which causes our keyboard
   *                            event(s).
   * @param aKeyEvent           A Gecko key event which was partially
   *                            initialized with aNativeKeyEvent.
   * @param aInsertString       The string to be inputting by aNativeKeyEvent.
   *                            This should be specified by InsertText().
   *                            In other words, if the key event doesn't cause
   *                            a call of InsertText(), this can be nullptr.
   * @param aResult             The string which should be set to charCode of
   *                            keypress event(s).
   */
  void ComputeInsertStringForCharCode(NSEvent* aNativeKeyEvent,
                                      const WidgetKeyboardEvent& aKeyEvent,
                                      const nsAString* aInsertString,
                                      nsAString& aResult);

  /**
   * IsPrintableKeyEvent() returns true if aNativeKeyEvent is caused by
   * a printable key.  Otherwise, returns false.
   */
  bool IsPrintableKeyEvent(NSEvent* aNativeKeyEvent) const;

  /**
   * GetKbdType() returns physical keyboard type.
   */
  UInt32 GetKbdType() const;

  bool GetBoolProperty(const CFStringRef aKey);
  bool GetStringProperty(const CFStringRef aKey, CFStringRef& aStr);
  bool GetStringProperty(const CFStringRef aKey, nsAString& aStr);

  TISInputSourceRef mInputSource;
  TISInputSourceRef mKeyboardLayout;
  CFArrayRef mInputSourceList;
  const UCKeyboardLayout* mUCKeyboardLayout;
  int8_t mIsRTL;

  bool mOverrideKeyboard;

  static TISInputSourceWrapper* sCurrentInputSource;
};

/**
 * TextInputHandlerBase is a base class of IMEInputHandler and TextInputHandler.
 * Utility methods should be implemented this level.
 */

class TextInputHandlerBase : public TextEventDispatcherListener {
 public:
  /**
   * Other TextEventDispatcherListener methods should be implemented in
   * IMEInputHandler.
   */
  NS_DECL_ISUPPORTS

  /**
   * DispatchEvent() dispatches aEvent on mWidget.
   *
   * @param aEvent                An event which you want to dispatch.
   * @return                      TRUE if the event is consumed by web contents
   *                              or chrome contents.  Otherwise, FALSE.
   */
  bool DispatchEvent(WidgetGUIEvent& aEvent);

  /**
   * SetSelection() dispatches eSetSelection event for the aRange.
   *
   * @param aRange                The range which will be selected.
   * @return                      TRUE if setting selection is succeeded and
   *                              the widget hasn't been destroyed.
   *                              Otherwise, FALSE.
   */
  bool SetSelection(NSRange& aRange);

  /**
   * InitKeyEvent() initializes aKeyEvent for aNativeKeyEvent.
   *
   * @param aNativeKeyEvent       A native key event for which you want to
   *                              dispatch a Gecko key event.
   * @param aKeyEvent             The result -- a Gecko key event initialized
   *                              from the native key event.
   * @param aIsProcessedByIME     true if aNativeKeyEvent has been handled
   *                              by IME (but except if the composition was
   *                              started with dead key).
   * @param aInsertString         If caller expects that the event will cause
   *                              a character to be input (say in an editor),
   *                              the caller should set this.  Otherwise,
   *                              if caller sets null to this, this method will
   *                              compute the character to be input from
   *                              characters of aNativeKeyEvent.
   */
  void InitKeyEvent(NSEvent* aNativeKeyEvent, WidgetKeyboardEvent& aKeyEvent,
                    bool aIsProcessedByIME,
                    const nsAString* aInsertString = nullptr);

  /**
   * SynthesizeNativeKeyEvent() is an implementation of
   * nsIWidget::SynthesizeNativeKeyEvent().  See the document in nsIWidget.h
   * for the detail.
   */
  nsresult SynthesizeNativeKeyEvent(int32_t aNativeKeyboardLayout,
                                    int32_t aNativeKeyCode,
                                    nsIWidget::NativeModifiers aModifierFlags,
                                    const nsAString& aCharacters,
                                    const nsAString& aUnmodifiedCharacters);

  /**
   * Utility method intended for testing. Attempts to construct a native key
   * event that would have been generated during an actual key press. This
   * *does not dispatch* the native event. Instead, it is attached to the
   * |mNativeKeyEvent| field of the Gecko event that is passed in.
   * @param aKeyEvent  Gecko key event to attach the native event to
   */
  NS_IMETHOD AttachNativeKeyEvent(WidgetKeyboardEvent& aKeyEvent);

  /**
   * GetWindowLevel() returns the window level of current focused (in Gecko)
   * window.  E.g., if an <input> element in XUL panel has focus, this returns
   * the XUL panel's window level.
   */
  NSInteger GetWindowLevel();

  /**
   * IsSpecialGeckoKey() checks whether aNativeKeyCode is mapped to a special
   * Gecko keyCode.  A key is "special" if it isn't used for text input.
   *
   * @param aNativeKeyCode        A native keycode.
   * @return                      If the keycode is mapped to a special key,
   *                              TRUE.  Otherwise, FALSE.
   */
  static bool IsSpecialGeckoKey(UInt32 aNativeKeyCode);

  /**
   * EnableSecureEventInput() and DisableSecureEventInput() wrap the Carbon
   * Event Manager APIs with the same names.  In addition they keep track of
   * how many times we've called them (in the same process) -- unlike the
   * Carbon Event Manager APIs, which only keep track of how many times they've
   * been called from any and all processes.
   *
   * The Carbon Event Manager's IsSecureEventInputEnabled() returns whether
   * secure event input mode is enabled (in any process).  This class's
   * IsSecureEventInputEnabled() returns whether we've made any calls to
   * EnableSecureEventInput() that are not (yet) offset by the calls we've
   * made to DisableSecureEventInput().
   */
  static void EnableSecureEventInput();
  static void DisableSecureEventInput();
  static bool IsSecureEventInputEnabled();

  /**
   * EnsureSecureEventInputDisabled() calls DisableSecureEventInput() until
   * our call count becomes 0.
   */
  static void EnsureSecureEventInputDisabled();

 public:
  /**
   * mWidget must not be destroyed without OnDestroyWidget being called.
   *
   * @param aDestroyingWidget     Destroying widget. This might not be mWidget.
   * @return                      This result doesn't have any meaning for
   *                              callers.  When aDestroyingWidget isn't the
   *                              same as mWidget, FALSE.  Then, inherited
   *                              methods in sub classes should return from
   *                              this method without cleaning up.
   */
  virtual bool OnDestroyWidget(nsCocoaWindow* aDestroyingWidget);

 protected:
  // The creator of this instance, client and its text event dispatcher.
  // These members must not be nullptr after initialized until
  // OnDestroyWidget() is called.
  nsCocoaWindow* mWidget;  // [WEAK]
  RefPtr<TextEventDispatcher> mDispatcher;

  // The native view for mWidget.
  // This view handles the actual text inputting.
  NSView<mozView>* mView;  // [STRONG]

  TextInputHandlerBase(nsCocoaWindow* aWidget, NSView<mozView>* aNativeView);
  virtual ~TextInputHandlerBase();

  bool Destroyed() { return !mWidget; }

  /**
   * mCurrentKeyEvent indicates what key event we are handling.  While
   * handling a native keydown event, we need to store the event for insertText,
   * doCommandBySelector and various action message handlers of NSResponder
   * such as [NSResponder insertNewline:sender].
   */
  struct KeyEventState {
    // Handling native key event
    NSEvent* mKeyEvent;
    // String specified by InsertText().  This is not null only during a
    // call of InsertText().
    nsAString* mInsertString;
    // String which are included in [mKeyEvent characters] and already handled
    // by InsertText() call(s).
    nsString mInsertedString;
    // Unique id associated with a keydown / keypress event. It's ok if this
    // wraps over long periods.
    uint32_t mUniqueId;
    // Whether keydown event was dispatched for mKeyEvent.
    bool mKeyDownDispatched;
    // Whether keydown event was consumed by web contents or chrome contents.
    bool mKeyDownHandled;
    // Whether keypress event was dispatched for mKeyEvent.
    bool mKeyPressDispatched;
    // Whether keypress event was consumed by web contents or chrome contents.
    bool mKeyPressHandled;
    // Whether the key event causes other key events via IME or something.
    bool mCausedOtherKeyEvents;
    // Whether the key event causes composition change or committing
    // composition.  So, even if InsertText() is called, this may be false
    // if it dispatches keypress event.
    bool mCompositionDispatched;

    KeyEventState() : mKeyEvent(nullptr), mUniqueId(0) { Clear(); }

    explicit KeyEventState(NSEvent* aNativeKeyEvent, uint32_t aUniqueId = 0)
        : mKeyEvent(nullptr), mUniqueId(0) {
      Clear();
      Set(aNativeKeyEvent, aUniqueId);
    }

    KeyEventState(const KeyEventState& aOther) = delete;

    ~KeyEventState() { Clear(); }

    void Set(NSEvent* aNativeKeyEvent, uint32_t aUniqueId = 0) {
      MOZ_ASSERT(aNativeKeyEvent, "aNativeKeyEvent must not be NULL");
      Clear();
      mKeyEvent = [aNativeKeyEvent retain];
      mUniqueId = aUniqueId;
    }

    void Clear() {
      if (mKeyEvent) {
        [mKeyEvent release];
        mKeyEvent = nullptr;
        mUniqueId = 0;
      }
      mInsertString = nullptr;
      mInsertedString.Truncate();
      mKeyDownDispatched = false;
      mKeyDownHandled = false;
      mKeyPressDispatched = false;
      mKeyPressHandled = false;
      mCausedOtherKeyEvents = false;
      mCompositionDispatched = false;
    }

    bool IsDefaultPrevented() const {
      return mKeyDownHandled || mKeyPressHandled || mCausedOtherKeyEvents ||
             mCompositionDispatched;
    }

    bool CanDispatchKeyDownEvent() const { return !mKeyDownDispatched; }

    bool CanDispatchKeyPressEvent() const {
      return !mKeyPressDispatched && !IsDefaultPrevented();
    }

    bool CanHandleCommand() const {
      return !mKeyDownHandled && !mKeyPressHandled;
    }

    bool IsProperKeyEvent(Command aCommand) const {
      if (NS_WARN_IF(!mKeyEvent)) {
        return false;
      }
      KeyNameIndex keyNameIndex =
          TISInputSourceWrapper::ComputeGeckoKeyNameIndex([mKeyEvent keyCode]);
      Modifiers modifiers =
          nsCocoaUtils::ModifiersForEvent(mKeyEvent) &
          (MODIFIER_SHIFT | MODIFIER_CONTROL | MODIFIER_ALT | MODIFIER_META);
      switch (aCommand) {
        case Command::InsertLineBreak:
          return keyNameIndex == KEY_NAME_INDEX_Enter &&
                 modifiers == MODIFIER_CONTROL;
        case Command::InsertParagraph:
          return keyNameIndex == KEY_NAME_INDEX_Enter &&
                 modifiers == MODIFIER_NONE;
        case Command::DeleteCharBackward:
          return keyNameIndex == KEY_NAME_INDEX_Backspace &&
                 modifiers == MODIFIER_NONE;
        case Command::DeleteToBeginningOfLine:
          return keyNameIndex == KEY_NAME_INDEX_Backspace &&
                 modifiers == MODIFIER_META;
        case Command::DeleteWordBackward:
          return keyNameIndex == KEY_NAME_INDEX_Backspace &&
                 modifiers == MODIFIER_ALT;
        case Command::DeleteCharForward:
          return keyNameIndex == KEY_NAME_INDEX_Delete &&
                 modifiers == MODIFIER_NONE;
        case Command::DeleteWordForward:
          return keyNameIndex == KEY_NAME_INDEX_Delete &&
                 modifiers == MODIFIER_ALT;
        case Command::InsertTab:
          return keyNameIndex == KEY_NAME_INDEX_Tab &&
                 modifiers == MODIFIER_NONE;
        case Command::InsertBacktab:
          return keyNameIndex == KEY_NAME_INDEX_Tab &&
                 modifiers == MODIFIER_SHIFT;
        case Command::CharNext:
          return keyNameIndex == KEY_NAME_INDEX_ArrowRight &&
                 modifiers == MODIFIER_NONE;
        case Command::SelectCharNext:
          return keyNameIndex == KEY_NAME_INDEX_ArrowRight &&
                 modifiers == MODIFIER_SHIFT;
        case Command::WordNext:
          return keyNameIndex == KEY_NAME_INDEX_ArrowRight &&
                 modifiers == MODIFIER_ALT;
        case Command::SelectWordNext:
          return keyNameIndex == KEY_NAME_INDEX_ArrowRight &&
                 modifiers == (MODIFIER_ALT | MODIFIER_SHIFT);
        case Command::EndLine:
        case Command::MoveRight3:
          return keyNameIndex == KEY_NAME_INDEX_ArrowRight &&
                 modifiers == MODIFIER_META;
        case Command::SelectEndLine:
        case Command::SelectRight3:
          return keyNameIndex == KEY_NAME_INDEX_ArrowRight &&
                 modifiers == (MODIFIER_META | MODIFIER_SHIFT);
        case Command::CharPrevious:
          return keyNameIndex == KEY_NAME_INDEX_ArrowLeft &&
                 modifiers == MODIFIER_NONE;
        case Command::SelectCharPrevious:
          return keyNameIndex == KEY_NAME_INDEX_ArrowLeft &&
                 modifiers == MODIFIER_SHIFT;
        case Command::WordPrevious:
          return keyNameIndex == KEY_NAME_INDEX_ArrowLeft &&
                 modifiers == MODIFIER_ALT;
        case Command::SelectWordPrevious:
          return keyNameIndex == KEY_NAME_INDEX_ArrowLeft &&
                 modifiers == (MODIFIER_ALT | MODIFIER_SHIFT);
        case Command::BeginLine:
        case Command::MoveLeft3:
          return keyNameIndex == KEY_NAME_INDEX_ArrowLeft &&
                 modifiers == MODIFIER_META;
        case Command::SelectBeginLine:
        case Command::SelectLeft3:
          return keyNameIndex == KEY_NAME_INDEX_ArrowLeft &&
                 modifiers == (MODIFIER_META | MODIFIER_SHIFT);
        case Command::LinePrevious:
          return keyNameIndex == KEY_NAME_INDEX_ArrowUp &&
                 modifiers == MODIFIER_NONE;
        case Command::SelectLinePrevious:
          return keyNameIndex == KEY_NAME_INDEX_ArrowUp &&
                 modifiers == MODIFIER_SHIFT;
        case Command::MoveTop:
          return keyNameIndex == KEY_NAME_INDEX_ArrowUp &&
                 modifiers == MODIFIER_META;
        case Command::SelectTop:
          return (keyNameIndex == KEY_NAME_INDEX_ArrowUp &&
                  modifiers == (MODIFIER_META | MODIFIER_SHIFT)) ||
                 (keyNameIndex == KEY_NAME_INDEX_Home &&
                  modifiers == MODIFIER_SHIFT);
        case Command::LineNext:
          return keyNameIndex == KEY_NAME_INDEX_ArrowDown &&
                 modifiers == MODIFIER_NONE;
        case Command::SelectLineNext:
          return keyNameIndex == KEY_NAME_INDEX_ArrowDown &&
                 modifiers == MODIFIER_SHIFT;
        case Command::MoveBottom:
          return keyNameIndex == KEY_NAME_INDEX_ArrowDown &&
                 modifiers == MODIFIER_META;
        case Command::SelectBottom:
          return (keyNameIndex == KEY_NAME_INDEX_ArrowDown &&
                  modifiers == (MODIFIER_META | MODIFIER_SHIFT)) ||
                 (keyNameIndex == KEY_NAME_INDEX_End &&
                  modifiers == MODIFIER_SHIFT);
        case Command::ScrollPageUp:
          return keyNameIndex == KEY_NAME_INDEX_PageUp &&
                 modifiers == MODIFIER_NONE;
        case Command::SelectPageUp:
          return keyNameIndex == KEY_NAME_INDEX_PageUp &&
                 modifiers == MODIFIER_SHIFT;
        case Command::ScrollPageDown:
          return keyNameIndex == KEY_NAME_INDEX_PageDown &&
                 modifiers == MODIFIER_NONE;
        case Command::SelectPageDown:
          return keyNameIndex == KEY_NAME_INDEX_PageDown &&
                 modifiers == MODIFIER_SHIFT;
        case Command::ScrollBottom:
          return keyNameIndex == KEY_NAME_INDEX_End &&
                 modifiers == MODIFIER_NONE;
        case Command::ScrollTop:
          return keyNameIndex == KEY_NAME_INDEX_Home &&
                 modifiers == MODIFIER_NONE;
        case Command::CancelOperation:
          return (keyNameIndex == KEY_NAME_INDEX_Escape &&
                  (modifiers == MODIFIER_NONE ||
                   modifiers == MODIFIER_SHIFT)) ||
                 ([mKeyEvent keyCode] == kVK_ANSI_Period &&
                  modifiers == MODIFIER_META);
        case Command::Complete:
          return keyNameIndex == KEY_NAME_INDEX_Escape &&
                 (modifiers == MODIFIER_ALT ||
                  modifiers == (MODIFIER_ALT | MODIFIER_SHIFT));
        default:
          return false;
      }
    }

    void InitKeyEvent(TextInputHandlerBase* aHandler,
                      WidgetKeyboardEvent& aKeyEvent, bool aIsProcessedByIME);

    /**
     * GetUnhandledString() returns characters of the event which have not been
     * handled with InsertText() yet. For example, if there is a composition
     * caused by a dead key press like '`' and it's committed by some key
     * combinations like |Cmd+v|, then, the |v|'s KeyDown event's |characters|
     * is |`v|.  Then, after |`| is committed with a call of InsertString(),
     * this returns only 'v'.
     */
    void GetUnhandledString(nsAString& aUnhandledString) const;
  };

  /**
   * Helper classes for guaranteeing cleaning mCurrentKeyEvent
   */
  class AutoKeyEventStateCleaner {
   public:
    explicit AutoKeyEventStateCleaner(TextInputHandlerBase* aHandler)
        : mHandler(aHandler) {}

    ~AutoKeyEventStateCleaner() { mHandler->RemoveCurrentKeyEvent(); }

   private:
    RefPtr<TextInputHandlerBase> mHandler;
  };

  class MOZ_STACK_CLASS AutoInsertStringClearer {
   public:
    explicit AutoInsertStringClearer(KeyEventState* aState) : mState(aState) {}
    ~AutoInsertStringClearer();

   private:
    KeyEventState* mState;
  };

  /**
   * mCurrentKeyEvents stores all key events which are being processed.
   * When we call interpretKeyEvents, IME may generate other key events.
   * mCurrentKeyEvents[0] is the latest key event.
   */
  nsTArray<KeyEventState*> mCurrentKeyEvents;

  /**
   * mFirstKeyEvent must be used for first key event.  This member prevents
   * memory fragmentation for most key events.
   */
  KeyEventState mFirstKeyEvent;

  /**
   * PushKeyEvent() adds the current key event to mCurrentKeyEvents.
   */
  KeyEventState* PushKeyEvent(NSEvent* aNativeKeyEvent,
                              uint32_t aUniqueId = 0) {
    uint32_t nestCount = mCurrentKeyEvents.Length();
    for (uint32_t i = 0; i < nestCount; i++) {
      // When the key event is caused by another key event, all key events
      // which are being handled should be marked as "consumed".
      mCurrentKeyEvents[i]->mCausedOtherKeyEvents = true;
    }

    KeyEventState* keyEvent = nullptr;
    if (nestCount == 0) {
      mFirstKeyEvent.Set(aNativeKeyEvent, aUniqueId);
      keyEvent = &mFirstKeyEvent;
    } else {
      keyEvent = new KeyEventState(aNativeKeyEvent, aUniqueId);
    }
    return *mCurrentKeyEvents.AppendElement(keyEvent);
  }

  /**
   * RemoveCurrentKeyEvent() removes the current key event from
   * mCurrentKeyEvents.
   */
  void RemoveCurrentKeyEvent() {
    NS_ASSERTION(mCurrentKeyEvents.Length() > 0,
                 "RemoveCurrentKeyEvent() is called unexpectedly");
    KeyEventState* keyEvent = mCurrentKeyEvents.PopLastElement();
    if (keyEvent == &mFirstKeyEvent) {
      keyEvent->Clear();
    } else {
      delete keyEvent;
    }
  }

  /**
   * GetCurrentKeyEvent() returns current processing key event.
   */
  KeyEventState* GetCurrentKeyEvent() {
    if (mCurrentKeyEvents.Length() == 0) {
      return nullptr;
    }
    return mCurrentKeyEvents[mCurrentKeyEvents.Length() - 1];
  }

  struct KeyboardLayoutOverride final {
    int32_t mKeyboardLayout;
    bool mOverrideEnabled;

    KeyboardLayoutOverride() : mKeyboardLayout(0), mOverrideEnabled(false) {}
  };

  const KeyboardLayoutOverride& KeyboardLayoutOverrideRef() const {
    return mKeyboardOverride;
  }

  /**
   * IsPrintableChar() checks whether the unicode character is
   * a non-printable ASCII character or not.  Note that this returns
   * TRUE even if aChar is a non-printable UNICODE character.
   *
   * @param aChar                 A unicode character.
   * @return                      TRUE if aChar is a printable ASCII character
   *                              or a unicode character.  Otherwise, i.e,
   *                              if aChar is a non-printable ASCII character,
   *                              FALSE.
   */
  static bool IsPrintableChar(char16_t aChar);

  /**
   * IsNormalCharInputtingEvent() checks whether aNativeEvent causes text input.
   *
   * @param aNativeEvent          A key event.
   * @return                      TRUE if the key event causes text input.
   *                              Otherwise, FALSE.
   */
  static bool IsNormalCharInputtingEvent(NSEvent* aNativeEvent);

  /**
   * IsModifierKey() checks whether the native keyCode is for a modifier key.
   *
   * @param aNativeKeyCode        A native keyCode.
   * @return                      TRUE if aNativeKeyCode is for a modifier key.
   *                              Otherwise, FALSE.
   */
  static bool IsModifierKey(UInt32 aNativeKeyCode);

 private:
  KeyboardLayoutOverride mKeyboardOverride;

  static int32_t sSecureEventInputCount;
};

/**
 * IMEInputHandler manages:
 *   1. The IME/keyboard layout statement of nsCocoaWindow.
 *   2. The IME composition statement of nsCocoaWindow.
 * And also provides the methods which controls the current IME transaction of
 * the instance.
 *
 * Note that an nsCocoaWindow handles one or more NSView's events.  E.g., even
 * if a text editor on XUL panel element, the input events handled on the parent
 * (or its ancestor) widget handles it (the native focus is set to it).  The
 * actual focused view is notified by OnFocusChangeInGecko.
 */

class IMEInputHandler : public TextInputHandlerBase {
 public:
  // TextEventDispatcherListener methods
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
  virtual bool OnDestroyWidget(nsCocoaWindow* aDestroyingWidget) override;

  virtual void OnFocusChangeInGecko(bool aFocus);

  void OnSelectionChange(const IMENotification& aIMENotification);
  void OnLayoutChange();
  void OnTextChange(const IMENotification& aIMENotification);

  /**
   * Call [NSTextInputContext handleEvent] for mouse event support of IME
   */
  bool OnHandleEvent(NSEvent* aEvent);

  /**
   * SetMarkedText() is a handler of setMarkedText of NSTextInput.
   *
   * @param aAttrString           This must be an instance of
   *                              NSAttributedString. If the aString parameter
   *                              to ChildView's
   *                              setMarkedText:setSelectedRange: isn't an
   *                              instance of NSAttributedString, create an
   *                              NSAttributedString from it and pass that
   *                              instead.
   * @param aSelectedRange        Current selected range (or caret position).
   * @param aReplacementRange     The range which will be replaced with the
   *                              aAttrString instead of current marked range.
   */
  void SetMarkedText(NSAttributedString* aAttrString, NSRange& aSelectedRange,
                     NSRange* aReplacementRange = nullptr);

  /**
   * GetAttributedSubstringFromRange() returns an NSAttributedString instance
   * which is allocated as autorelease for aRange.
   *
   * @param aRange                The range of string which you want.
   * @param aActualRange          The actual range of the result.
   * @return                      The string in aRange.  If the string is empty,
   *                              this returns nil.  If succeeded, this returns
   *                              an instance which is allocated as autorelease.
   *                              If this has some troubles, returns nil.
   */
  NSAttributedString* GetAttributedSubstringFromRange(
      NSRange& aRange, NSRange* aActualRange = nullptr);

  /**
   * SelectedRange() returns current selected range.
   *
   * @return                      If an editor has focus, this returns selection
   *                              range in the editor.  Otherwise, this returns
   *                              selection range  in the focused document.
   */
  NSRange SelectedRange();

  /**
   * DrawsVerticallyForCharacterAtIndex() returns whether the character at
   * the given index is being rendered vertically.
   *
   * @param aCharIndex            The character offset to query.
   *
   * @return                      True if writing-mode is vertical at the given
   *                              character offset; otherwise false.
   */
  bool DrawsVerticallyForCharacterAtIndex(uint32_t aCharIndex);

  /**
   * FirstRectForCharacterRange() returns first *character* rect in the range.
   * Cocoa needs the first line rect in the range, but we cannot compute it
   * on current implementation.
   *
   * @param aRange                A range of text to examine.  Its position is
   *                              an offset from the beginning of the focused
   *                              editor or document.
   * @param aActualRange          If this is not null, this returns the actual
   *                              range used for computing the result.
   * @return                      An NSRect containing the first character in
   *                              aRange, in screen coordinates.
   *                              If the length of aRange is 0, the width will
   *                              be 0.
   */
  NSRect FirstRectForCharacterRange(NSRange& aRange,
                                    NSRange* aActualRange = nullptr);

  /**
   * CharacterIndexForPoint() returns an offset of a character at aPoint.
   * XXX This isn't implemented, always returns 0.
   *
   * @param                       The point in screen coordinates.
   * @return                      The offset of the character at aPoint from
   *                              the beginning of the focused editor or
   *                              document.
   */
  NSUInteger CharacterIndexForPoint(NSPoint& aPoint);

  /**
   * GetValidAttributesForMarkedText() returns attributes which we support.
   *
   * @return                      Always empty array for now.
   */
  NSArray* GetValidAttributesForMarkedText();

  bool HasMarkedText();
  NSRange MarkedRange();

  bool IsIMEComposing() { return mIsIMEComposing; }
  bool IsDeadKeyComposing() { return mIsDeadKeyComposing; }
  bool IsIMEOpened();
  bool IsIMEEnabled() { return mIsIMEEnabled; }
  bool IsASCIICapableOnly() { return mIsASCIICapableOnly; }
  bool IsEditableContent() const {
    return mIsIMEEnabled || mIsASCIICapableOnly;
  }
  bool IgnoreIMECommit() { return mIgnoreIMECommit; }

  void CommitIMEComposition();
  void CancelIMEComposition();

  void EnableIME(bool aEnableIME);
  void SetIMEOpenState(bool aOpen);
  void SetASCIICapableOnly(bool aASCIICapableOnly);

  /**
   * True if macOS believes that our view has keyboard focus.
   */
  bool IsFocused();

  static CFArrayRef CreateAllIMEModeList();
  static void DebugPrintAllIMEModes();

  // Don't use ::TSMGetActiveDocument() API directly, the document may not
  // be what you want.
  static TSMDocumentID GetCurrentTSMDocumentID();

  void EnableTextSubstitution(bool aEnableTextSubstitution) {
    mEnableTextSubstitution = aEnableTextSubstitution;
  }

 protected:
  // We cannot do some jobs in the given stack by some reasons.
  // Following flags and the timer provide the execution pending mechanism,
  // See the comment in nsCocoaTextInputHandler.mm.
  nsCOMPtr<nsITimer> mTimer;
  enum { kNotifyIMEOfFocusChangeInGecko = 1, kSyncASCIICapableOnly = 2 };
  uint32_t mPendingMethods;

  // Used by text substitution.
  nsString mOriginalTextForTextSubstitution;
  NSTextCheckingResult* mCandidatedTextSubstitutionResult;
  bool mProcessTextSubstitution;
  bool mBlockDismissTextSubstitutionPanel = false;
  bool mPendingDismissTextSubstitution = false;

  IMEInputHandler(nsCocoaWindow* aWidget, NSView<mozView>* aNativeView);
  virtual ~IMEInputHandler();

  void ResetTimer();

  virtual void ExecutePendingMethods();

  /**
   * InsertTextAsCommittingComposition() commits current composition.  If there
   * is no composition, this starts a composition and commits it immediately.
   *
   * @param aString               A string which is committed.
   * @param aReplacementRange     The range which will be replaced with the
   *                              aAttrString instead of current selection.
   */
  void InsertTextAsCommittingComposition(NSString* aString,
                                         NSRange* aReplacementRange);

  /**
   * MaybeDispatchCurrentKeydownEvent() dispatches eKeyDown event for current
   * key event.  If eKeyDown for current key event has already been dispatched,
   * this does nothing.
   *
   * @param aIsProcessedByIME   true if current key event is handled by IME.
   * @return                    true if the caller can continue to handle
   *                            current key event.  Otherwise, false.  E.g.,
   *                            focus is moved, the widget has been destroyed
   *                            or something.
   */
  bool MaybeDispatchCurrentKeydownEvent(bool aIsProcessedByIME);

  /**
   * ShowTextSubstitutionPanel shows text replacement panel by text substitution
   * using current candidate strings.
   */
  void ShowTextSubstitutionPanel();
  void DismissTextSubstitutionPanel();

 private:
  // If mIsIMEComposing is true, the composition string is stored here.
  NSString* mIMECompositionString = nullptr;
  // Store the composition start offset which is considered before dispatching
  // eCompositionStart.
  Maybe<uint32_t> mIMECompositionStartBeforeStart;
  // Store the composition start in content.  This may be different from
  // mIMECompositionBeforeStart if the web app changed the text after
  // dispatching eCompositionStart.
  Maybe<uint32_t> mIMECompositionStartInContent;

  NSRange mMarkedRange;
  NSRange mSelectedRange;

  // Store the override of mSelectedRange during a composition.  For avoiding
  // IME to be confused at text changes before the composition, this keeps the
  // selected range as in the composition string.
  Maybe<NSRange> mSelectedRangeOverride;

  NSRange mRangeForWritingMode;  // range within which mWritingMode applies
  mozilla::WritingMode mWritingMode;

  bool mIsIMEComposing;
  // If the composition started with dead key, mIsDeadKeyComposing is set to
  // true.
  bool mIsDeadKeyComposing;
  bool mIsIMEEnabled;
  bool mIsASCIICapableOnly;
  bool mIgnoreIMECommit;
  bool mIMEHasFocus;
  bool mEnableTextSubstitution;

  void KillIMEComposition();
  void SendCommittedText(NSString* aString);
  void OpenSystemPreferredLanguageIME();

  // Pending methods
  void NotifyIMEOfFocusChangeInGecko();
  void SyncASCIICapableOnly();

  static bool sStaticMembersInitialized;
  static CFStringRef sLatestIMEOpenedModeInputSourceID;
  static void InitStaticMembers();
  static void OnCurrentTextInputSourceChange(CFNotificationCenterRef aCenter,
                                             void* aObserver, CFStringRef aName,
                                             const void* aObject,
                                             CFDictionaryRef aUserInfo);

  static void FlushPendingMethods(nsITimer* aTimer, void* aClosure);

  /**
   * ConvertToTextRangeStyle converts the given native underline style to
   * our defined text range type.
   *
   * @param aUnderlineStyle       NSUnderlineStyleSingle or
   *                              NSUnderlineStyleThick.
   * @param aSelectedRange        Current selected range (or caret position).
   * @return                      NS_TEXTRANGE_*.
   */
  TextRangeType ConvertToTextRangeType(uint32_t aUnderlineStyle,
                                       NSRange& aSelectedRange);

  /**
   * GetRangeCount() computes the range count of aAttrString.
   *
   * @param aAttrString           An NSAttributedString instance whose number of
   *                              NSUnderlineStyleAttributeName ranges you with
   *                              to know.
   * @return                      The count of NSUnderlineStyleAttributeName
   *                              ranges in aAttrString.
   */
  uint32_t GetRangeCount(NSAttributedString* aString);

  /**
   * CreateTextRangeArray() returns text ranges for clauses and/or caret.
   *
   * @param aAttrString           An NSAttributedString instance which indicates
   *                              current composition string.
   * @param aSelectedRange        Current selected range (or caret position).
   * @return                      The result is set to the
   *                              NSUnderlineStyleAttributeName ranges in
   *                              aAttrString.
   */
  already_AddRefed<mozilla::TextRangeArray> CreateTextRangeArray(
      NSAttributedString* aAttrString, NSRange& aSelectedRange);

  /**
   * DispatchCompositionStartEvent() dispatches a compositionstart event and
   * initializes the members indicating composition state.
   *
   * @return                      true if it can continues handling composition.
   *                              Otherwise, e.g., canceled by the web page,
   *                              this returns false.
   */
  bool DispatchCompositionStartEvent();

  /**
   * DispatchCompositionChangeEvent() dispatches a compositionchange event on
   * mWidget and modifies the members indicating composition state.
   *
   * @param aText                 User text input.
   * @param aAttrString           An NSAttributedString instance which indicates
   *                              current composition string.
   * @param aSelectedRange        Current selected range (or caret position).
   *
   * @return                      true if it can continues handling composition.
   *                              Otherwise, e.g., canceled by the web page,
   *                              this returns false.
   */
  bool DispatchCompositionChangeEvent(const nsString& aText,
                                      NSAttributedString* aAttrString,
                                      NSRange& aSelectedRange);

  /**
   * DispatchCompositionCommitEvent() dispatches a compositioncommit event or
   * compositioncommitasis event.  If aCommitString is null, dispatches
   * compositioncommitasis event.  I.e., if aCommitString is null, this
   * commits the composition with the last data.  Otherwise, commits the
   * composition with aCommitString value.
   *
   * @return                      true if the widget isn't destroyed.
   *                              Otherwise, false.
   */
  bool DispatchCompositionCommitEvent(const nsAString* aCommitString = nullptr);

  /*
   * HandleTextSubstitution handles text substitution features such as text
   * replacement.
   */
  void HandleTextSubstitution(const IMENotification& aIMENotification);

  /*
   * The followings are the helper methods for text substitution.
   */
  void OnTextSubstitution(uint32_t aStartOffset);

  void ReplaceTextForTextSubstitution(const nsAString& aOriginalString,
                                      NSString* aString, const NSRange& aRange,
                                      PreventSetSelection aPreventSetSelection);

  // The focused IME handler.  Please note that the handler might lose the
  // actual focus by deactivating the application.  If we are active, this
  // must have the actual focused handle.
  // We cannot access the NSInputManager while we aren't active, so the
  // focused handler can have an IME transaction even if we are deactivated.
  static IMEInputHandler* sFocusedIMEHandler;

  static bool sCachedIsForRTLLanguage;
};

/**
 * TextInputHandler implements the NSTextInput protocol.
 */
class TextInputHandler : public IMEInputHandler {
 public:
  static NSUInteger sLastModifierState;

  static CFArrayRef CreateAllKeyboardLayoutList();
  static void DebugPrintAllKeyboardLayouts();

  TextInputHandler(nsCocoaWindow* aWidget, NSView<mozView>* aNativeView);
  virtual ~TextInputHandler();

  /**
   * KeyDown event handler.
   *
   * @param aNativeEvent          A native NSEventTypeKeyDown event.
   * @param aUniqueId             A unique ID for the event.
   * @return                      TRUE if the event is dispatched to web
   *                              contents or chrome contents. Otherwise, FALSE.
   */
  bool HandleKeyDownEvent(NSEvent* aNativeEvent, uint32_t aUniqueId);

  /**
   * KeyUp event handler.
   *
   * @param aNativeEvent          A native NSEventTypeKeyUp event.
   */
  void HandleKeyUpEvent(NSEvent* aNativeEvent);

  /**
   * FlagsChanged event handler.
   *
   * @param aNativeEvent          A native NSEventTypeFlagsChanged event.
   */
  void HandleFlagsChanged(NSEvent* aNativeEvent);

  /**
   * Insert the string to content.  I.e., this is a text input event handler.
   * If this is called during keydown event handling, this may dispatch a
   * eKeyPress event.  If this is called during composition, this commits
   * the composition by the aAttrString.
   *
   * @param aString               An inserted string.
   * @param aReplacementRange     The range which will be replaced with the
   *                              aAttrString instead of current selection.
   */
  void InsertText(NSString* aString, NSRange* aReplacementRange = nullptr);

  /**
   * Handles aCommand.  This may cause dispatching an eKeyPress event.
   *
   * @param aCommand    The command which receives from Cocoa.
   * @return            true if this handles the command even if it does
   *                    nothing actually.  Otherwise, false.
   */
  bool HandleCommand(Command aCommand);

  /**
   * doCommandBySelector event handler.
   *
   * @param aSelector             A selector of the command.
   * @return                      TRUE if the command is consumed.  Otherwise,
   *                              FALSE.
   */
  bool DoCommandBySelector(const char* aSelector);

  /**
   * KeyPressWasHandled() checks whether keypress event was handled or not.
   *
   * @return                      TRUE if keypress event for latest native key
   *                              event was handled.  Otherwise, FALSE.
   *                              If this handler isn't handling any key events,
   *                              always returns FALSE.
   */
  bool KeyPressWasHandled() {
    KeyEventState* currentKeyEvent = GetCurrentKeyEvent();
    return currentKeyEvent && currentKeyEvent->mKeyPressHandled;
  }

 protected:
  // Stores the association of device dependent modifier flags with a modifier
  // keyCode.  Being device dependent, this association may differ from one kind
  // of hardware to the next.
  struct ModifierKey {
    NSUInteger flags;
    unsigned short keyCode;

    ModifierKey(NSUInteger aFlags, unsigned short aKeyCode)
        : flags(aFlags), keyCode(aKeyCode) {}

    NSUInteger GetDeviceDependentFlags() const {
      return (flags & ~NSEventModifierFlagDeviceIndependentFlagsMask);
    }

    NSUInteger GetDeviceIndependentFlags() const {
      return (flags & NSEventModifierFlagDeviceIndependentFlagsMask);
    }
  };
  typedef nsTArray<ModifierKey> ModifierKeyArray;
  ModifierKeyArray mModifierKeys;

  /**
   * GetModifierKeyForNativeKeyCode() returns the stored ModifierKey for
   * the key.
   */
  const ModifierKey* GetModifierKeyForNativeKeyCode(
      unsigned short aKeyCode) const;

  /**
   * GetModifierKeyForDeviceDependentFlags() returns the stored ModifierKey for
   * the device dependent flags.
   */
  const ModifierKey* GetModifierKeyForDeviceDependentFlags(
      NSUInteger aFlags) const;

  /**
   * DispatchKeyEventForFlagsChanged() dispatches keydown event or keyup event
   * for the aNativeEvent.
   *
   * @param aNativeEvent          A native flagschanged event which you want to
   *                              dispatch our key event for.
   * @param aDispatchKeyDown      TRUE if you want to dispatch a keydown event.
   *                              Otherwise, i.e., to dispatch keyup event,
   *                              FALSE.
   */
  void DispatchKeyEventForFlagsChanged(NSEvent* aNativeEvent,
                                       bool aDispatchKeyDown);
};

}  // namespace widget
}  // namespace mozilla

#endif  // TextInputHandler_h_
