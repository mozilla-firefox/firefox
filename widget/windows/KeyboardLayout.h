/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef KeyboardLayout_h_
#define KeyboardLayout_h_

#include <windows.h>

#include "mozilla/Attributes.h"
#include "mozilla/EventForwards.h"
#include "mozilla/RefPtr.h"
#include "mozilla/StaticPtr.h"
#include "mozilla/TextEventDispatcher.h"
#include "mozilla/widget/WinMessages.h"
#include "mozilla/widget/WinModifierKeyState.h"
#include "nsString.h"
#include "nsTArray.h"
#include "nsWindow.h"
#include "nsWindowDefs.h"
#include "nscore.h"

#define NS_NUM_OF_KEYS 70

#define VK_OEM_1 0xBA     // ';:' for US
#define VK_OEM_PLUS 0xBB  // '+' any country
#define VK_OEM_COMMA 0xBC
#define VK_OEM_MINUS 0xBD  // '-' any country
#define VK_OEM_PERIOD 0xBE
#define VK_OEM_2 0xBF
#define VK_OEM_3 0xC0
// '/?' for Brazilian (ABNT)
#define VK_ABNT_C1 0xC1
// Separator in Numpad for Brazilian (ABNT) or JIS keyboard for Mac.
#define VK_ABNT_C2 0xC2
#define VK_OEM_4 0xDB
#define VK_OEM_5 0xDC
#define VK_OEM_6 0xDD
#define VK_OEM_7 0xDE
#define VK_OEM_8 0xDF
#define VK_OEM_102 0xE2
#define VK_OEM_CLEAR 0xFE

class nsIUserIdleServiceInternal;

namespace mozilla {
namespace widget {

enum ScanCode : uint16_t {
  eCapsLock = 0x003A,
  eNumLock = 0xE045,
  eShiftLeft = 0x002A,
  eShiftRight = 0x0036,
  eControlLeft = 0x001D,
  eControlRight = 0xE01D,
  eAltLeft = 0x0038,
  eAltRight = 0xE038,
};

// 0: nsIWidget's native modifier flag
// 1: Virtual keycode which does not distinguish whether left or right location.
// 2: Virtual keycode which distinguishes whether left or right location.
// 3: Scan code.
static const uint32_t sModifierKeyMap[][4] = {
    {static_cast<uint32_t>(nsIWidget::NativeModifiers::CAPS_LOCK), VK_CAPITAL,
     0, ScanCode::eCapsLock},
    {static_cast<uint32_t>(nsIWidget::NativeModifiers::NUM_LOCK), VK_NUMLOCK, 0,
     ScanCode::eNumLock},
    {static_cast<uint32_t>(nsIWidget::NativeModifiers::SHIFT_L), VK_SHIFT,
     VK_LSHIFT, ScanCode::eShiftLeft},
    {static_cast<uint32_t>(nsIWidget::NativeModifiers::SHIFT_R), VK_SHIFT,
     VK_RSHIFT, ScanCode::eShiftRight},
    {static_cast<uint32_t>(nsIWidget::NativeModifiers::CTRL_L), VK_CONTROL,
     VK_LCONTROL, ScanCode::eControlLeft},
    {static_cast<uint32_t>(nsIWidget::NativeModifiers::CTRL_R), VK_CONTROL,
     VK_RCONTROL, ScanCode::eControlRight},
    {static_cast<uint32_t>(nsIWidget::NativeModifiers::ALT_L), VK_MENU,
     VK_LMENU, ScanCode::eAltLeft},
    {static_cast<uint32_t>(nsIWidget::NativeModifiers::ALT_R), VK_MENU,
     VK_RMENU, ScanCode::eAltRight}};

class KeyboardLayout;

class MOZ_STACK_CLASS UniCharsAndModifiers final {
 public:
  UniCharsAndModifiers() = default;
  UniCharsAndModifiers operator+(const UniCharsAndModifiers& aOther) const;
  UniCharsAndModifiers& operator+=(const UniCharsAndModifiers& aOther);

  /**
   * Append a pair of unicode character and the final modifier.
   */
  void Append(char16_t aUniChar, Modifiers aModifiers);
  void Clear() {
    mChars.Truncate();
    mModifiers.Clear();
  }
  bool IsEmpty() const {
    MOZ_ASSERT(mChars.Length() == mModifiers.Length());
    return mChars.IsEmpty();
  }

  char16_t CharAt(size_t aIndex) const {
    MOZ_ASSERT(aIndex < Length());
    return mChars[aIndex];
  }
  Modifiers ModifiersAt(size_t aIndex) const {
    MOZ_ASSERT(aIndex < Length());
    return mModifiers[aIndex];
  }
  size_t Length() const {
    MOZ_ASSERT(mChars.Length() == mModifiers.Length());
    return mChars.Length();
  }

  bool IsProducingCharsWithAltGr() const {
    return !IsEmpty() && (ModifiersAt(0) & MODIFIER_ALTGRAPH) != 0;
  }

  void FillModifiers(Modifiers aModifiers);
  /**
   * OverwriteModifiersIfBeginsWith() assigns mModifiers with aOther between
   * [0] and [aOther.mLength - 1] only when mChars begins with aOther.mChars.
   */
  void OverwriteModifiersIfBeginsWith(const UniCharsAndModifiers& aOther);

  bool UniCharsEqual(const UniCharsAndModifiers& aOther) const;
  bool UniCharsCaseInsensitiveEqual(const UniCharsAndModifiers& aOther) const;
  bool BeginsWith(const UniCharsAndModifiers& aOther) const;

  const nsString& ToString() const { return mChars; }

 private:
  nsAutoString mChars;
  // 5 is enough number for normal keyboard layout handling.  On Windows,
  // a dead key sequence may cause inputting up to 5 characters per key press.
  CopyableAutoTArray<Modifiers, 5> mModifiers;
};

struct DeadKeyEntry {
  char16_t BaseChar;
  char16_t CompositeChar;

  DeadKeyEntry(char16_t aBaseChar, char16_t aCompositeChar)
      : BaseChar(aBaseChar), CompositeChar(aCompositeChar) {}

  bool operator<(const DeadKeyEntry& aOther) const {
    return this->BaseChar < aOther.BaseChar;
  }

  bool operator==(const DeadKeyEntry& aOther) const {
    return this->BaseChar == aOther.BaseChar;
  }
};

using DeadKeyTable = nsTArray<DeadKeyEntry>;

class VirtualKey {
 public:
  enum ShiftStateIndex : uint8_t {
    //  0 - Normal
    eNormal = 0,
    //  1 - Shift
    eShift,
    //  2 - Control
    eControl,
    //  3 - Control + Shift
    eControlShift,
    //  4 - Alt
    eAlt,
    //  5 - Alt + Shift
    eAltShift,
    //  6 - Alt + Control (AltGr)
    eAltGr,
    //  7 - Alt + Control + Shift (AltGr + Shift)
    eAltGrShift,
    //  8 - CapsLock
    eWithCapsLock,
    //  9 - CapsLock + Shift
    eShiftWithCapsLock,
    // 10 - CapsLock + Control
    eControlWithCapsLock,
    // 11 - CapsLock + Control + Shift
    eControlShiftWithCapsLock,
    // 12 - CapsLock + Alt
    eAltWithCapsLock,
    // 13 - CapsLock + Alt + Shift
    eAltShiftWithCapsLock,
    // 14 - CapsLock + Alt + Control (CapsLock + AltGr)
    eAltGrWithCapsLock,
    // 15 - CapsLock + Alt + Control + Shift (CapsLock + AltGr + Shift)
    eAltGrShiftWithCapsLock,
  };

  enum ShiftStateFlag {
    STATE_SHIFT = 0x01,
    STATE_CONTROL = 0x02,
    STATE_ALT = 0x04,
    STATE_CAPSLOCK = 0x08,
    // ShiftState needs to have AltGr state separately since this is necessary
    // for lossless conversion with Modifiers.
    STATE_ALTGRAPH = 0x80,
    // Useful to remove or check Ctrl and Alt flags.
    STATE_CONTROL_ALT = STATE_CONTROL | STATE_ALT,
  };

  typedef uint8_t ShiftState;

  static ShiftState ModifiersToShiftState(Modifiers aModifiers);
  static ShiftState ModifierKeyStateToShiftState(
      const ModifierKeyState& aModKeyState) {
    return ModifiersToShiftState(aModKeyState.GetModifiers());
  }
  static Modifiers ShiftStateToModifiers(ShiftState aShiftState);
  static bool IsAltGrIndex(uint8_t aIndex) {
    return (aIndex & STATE_CONTROL_ALT) == STATE_CONTROL_ALT;
  }

 private:
  union KeyShiftState {
    struct {
      char16_t Chars[4];
    } Normal;
    struct {
      const DeadKeyTable* Table;
      char16_t DeadChar;
    } DeadKey;
  };

  KeyShiftState mShiftStates[16];
  uint16_t mIsDeadKey;

  static uint8_t ToShiftStateIndex(ShiftState aShiftState) {
    if (!(aShiftState & STATE_ALTGRAPH)) {
      MOZ_ASSERT(aShiftState <= eAltGrShiftWithCapsLock);
      return static_cast<uint8_t>(aShiftState);
    }
    uint8_t index = aShiftState & ~STATE_ALTGRAPH;
    index |= (STATE_ALT | STATE_CONTROL);
    MOZ_ASSERT(index <= eAltGrShiftWithCapsLock);
    return index;
  }

  void SetDeadKey(ShiftState aShiftState, bool aIsDeadKey) {
    if (aIsDeadKey) {
      mIsDeadKey |= 1 << ToShiftStateIndex(aShiftState);
    } else {
      mIsDeadKey &= ~(1 << ToShiftStateIndex(aShiftState));
    }
  }

 public:
  static void FillKbdState(PBYTE aKbdState, const ShiftState aShiftState);

  bool IsDeadKey(ShiftState aShiftState) const {
    return (mIsDeadKey & (1 << ToShiftStateIndex(aShiftState))) != 0;
  }

  /**
   * IsChangedByAltGr() is useful to check if a key with AltGr produces
   * different character(s) from the key without AltGr.
   * Note that this is designed for checking if a keyboard layout has AltGr
   * key.  So, this result may not exactly correct for the key since it's
   * okay to fails in some edge cases when we check all keys which produce
   * character(s) in a layout.
   */
  bool IsChangedByAltGr(ShiftState aShiftState) const {
    MOZ_ASSERT(aShiftState == ToShiftStateIndex(aShiftState));
    MOZ_ASSERT(IsAltGrIndex(aShiftState));
    MOZ_ASSERT(IsDeadKey(aShiftState) ||
               mShiftStates[aShiftState].Normal.Chars[0]);
    const ShiftState kShiftStateWithoutAltGr =
        aShiftState - ShiftStateIndex::eAltGr;
    if (IsDeadKey(aShiftState) != IsDeadKey(kShiftStateWithoutAltGr)) {
      return false;
    }
    if (IsDeadKey(aShiftState)) {
      return mShiftStates[aShiftState].DeadKey.DeadChar !=
             mShiftStates[kShiftStateWithoutAltGr].DeadKey.DeadChar;
    }
    for (size_t i = 0; i < 4; i++) {
      if (mShiftStates[aShiftState].Normal.Chars[i] !=
          mShiftStates[kShiftStateWithoutAltGr].Normal.Chars[i]) {
        return true;
      }
      if (!mShiftStates[aShiftState].Normal.Chars[i] &&
          !mShiftStates[kShiftStateWithoutAltGr].Normal.Chars[i]) {
        return false;
      }
    }
    return false;
  }

  void AttachDeadKeyTable(ShiftState aShiftState,
                          const DeadKeyTable* aDeadKeyTable) {
    MOZ_ASSERT(aShiftState == ToShiftStateIndex(aShiftState));
    mShiftStates[aShiftState].DeadKey.Table = aDeadKeyTable;
  }

  void SetNormalChars(ShiftState aShiftState, const char16_t* aChars,
                      uint32_t aNumOfChars);
  void SetDeadChar(ShiftState aShiftState, char16_t aDeadChar);
  const DeadKeyTable* MatchingDeadKeyTable(const DeadKeyTable&) const;

  static char16_t GetCompositeChar(const DeadKeyTable* aDeadKeyTable,
                                   char16_t aBaseChar) {
    // Dead-key table is sorted by BaseChar in ascending order.
    // Usually they are too small to use binary search.
    for (const auto& entry : *aDeadKeyTable) {
      if (entry.BaseChar == aBaseChar) {
        return entry.CompositeChar;
      }
      if (entry.BaseChar > aBaseChar) {
        break;
      }
    }
    return 0;
  }

  inline char16_t GetCompositeChar(ShiftState aShiftState,
                                   char16_t aBaseChar) const {
    return VirtualKey::GetCompositeChar(
        mShiftStates[ToShiftStateIndex(aShiftState)].DeadKey.Table, aBaseChar);
  }

  char16_t GetCompositeChar(const ModifierKeyState& aModKeyState,
                            char16_t aBaseChar) const {
    return GetCompositeChar(ModifierKeyStateToShiftState(aModKeyState),
                            aBaseChar);
  }

  /**
   * GetNativeUniChars() returns character(s) which is produced by the
   * key with given modifiers.  This does NOT return proper MODIFIER_ALTGRAPH
   * state because this is raw accessor of the database of this key.
   */
  UniCharsAndModifiers GetNativeUniChars(ShiftState aShiftState) const;
  UniCharsAndModifiers GetNativeUniChars(
      const ModifierKeyState& aModKeyState) const {
    return GetNativeUniChars(ModifierKeyStateToShiftState(aModKeyState));
  }

  /**
   * GetUniChars() returns characters and modifiers which are not consumed
   * to input the character.
   * For example, if you specify Ctrl key but the key produces no character
   * with Ctrl, this returns character(s) which is produced by the key
   * without Ctrl.  So, the result is useful to decide KeyboardEvent.key
   * value.
   * Another example is, if you specify Ctrl key and the key produces
   * different character(s) from the case without Ctrl key, this returns
   * the character(s) *without* MODIFIER_CONTROL.  This modifier information
   * is useful for eKeyPress since TextEditor does not treat eKeyPress events
   * whose modifier includes MODIFIER_ALT and/or MODIFIER_CONTROL.
   *
   * @param aShiftState         Modifiers which you want to retrieve
   *                            KeyboardEvent.key value for the key with.
   *                            If AltGr key is pressed, this should include
   *                            STATE_ALTGRAPH and should NOT include
   *                            STATE_ALT nor STATE_CONTROL.
   *                            If both Alt and Ctrl are pressed to emulate
   *                            AltGr, this should include both STATE_ALT and
   *                            STATE_CONTROL but should NOT include
   *                            MODIFIER_ALTGRAPH.
   *                            Then, this returns proper modifiers when
   *                            this key produces no character with AltGr.
   */
  UniCharsAndModifiers GetUniChars(ShiftState aShiftState) const;
  UniCharsAndModifiers GetUniChars(const ModifierKeyState& aModKeyState) const {
    return GetUniChars(ModifierKeyStateToShiftState(aModKeyState));
  }
};

class MOZ_STACK_CLASS NativeKey final {
  friend class KeyboardLayout;

 public:
  struct FakeCharMsg {
    UINT mCharCode;
    UINT mScanCode;
    bool mIsSysKey;
    bool mIsDeadKey;
    bool mConsumed;

    FakeCharMsg()
        : mCharCode(0),
          mScanCode(0),
          mIsSysKey(false),
          mIsDeadKey(false),
          mConsumed(false) {}

    MSG GetCharMsg(HWND aWnd) const {
      MSG msg;
      msg.hwnd = aWnd;
      msg.message = mIsDeadKey && mIsSysKey ? WM_SYSDEADCHAR
                    : mIsDeadKey            ? WM_DEADCHAR
                    : mIsSysKey             ? WM_SYSCHAR
                                            : WM_CHAR;
      msg.wParam = static_cast<WPARAM>(mCharCode);
      msg.lParam = static_cast<LPARAM>(mScanCode << 16);
      msg.time = 0;
      msg.pt.x = msg.pt.y = 0;
      return msg;
    }
  };

  NativeKey(nsWindow* aWidget, const MSG& aMessage,
            const ModifierKeyState& aModKeyState,
            HKL aOverrideKeyboardLayout = 0,
            nsTArray<FakeCharMsg>* aFakeCharMsgs = nullptr);

  ~NativeKey();

  /**
   * Handle WM_KEYDOWN message or WM_SYSKEYDOWN message.  The instance must be
   * initialized with WM_KEYDOWN or WM_SYSKEYDOWN.
   * Returns true if dispatched keydown event or keypress event is consumed.
   * Otherwise, false.
   */
  bool HandleKeyDownMessage(bool* aEventDispatched = nullptr) const;

  /**
   * Handles WM_CHAR message or WM_SYSCHAR message.  The instance must be
   * initialized with them.
   * Returns true if dispatched keypress event is consumed.  Otherwise, false.
   */
  bool HandleCharMessage(bool* aEventDispatched = nullptr) const;

  /**
   * Handles keyup message.  Returns true if the event is consumed.
   * Otherwise, false.
   */
  bool HandleKeyUpMessage(bool* aEventDispatched = nullptr) const;

  /**
   * Handles WM_APPCOMMAND message.  Returns true if the event is consumed.
   * Otherwise, false.
   */
  bool HandleAppCommandMessage() const;

  /**
   * Callback of TextEventDispatcherListener::WillDispatchKeyboardEvent().
   * This method sets alternative char codes of aKeyboardEvent.
   */
  void WillDispatchKeyboardEvent(WidgetKeyboardEvent& aKeyboardEvent,
                                 uint32_t aIndex);

  /**
   * Returns true if aChar is a control character which shouldn't be inputted
   * into focused text editor.
   */
  static bool IsControlChar(char16_t aChar);

  bool IsShift() const { return mModKeyState.IsShift(); }
  bool IsControl() const { return mModKeyState.IsControl(); }
  bool IsAlt() const { return mModKeyState.IsAlt(); }
  bool IsMouseButton() const {
    return GET_DEVICE_LPARAM(mMsg.lParam) == FAPPCOMMAND_MOUSE;
  }

  bool MaybeEmulatingAltGraph() const;
  Modifiers GetModifiers() const { return mModKeyState.GetModifiers(); }
  const ModifierKeyState& ModifierKeyStateRef() const { return mModKeyState; }
  VirtualKey::ShiftState GetShiftState() const {
    return VirtualKey::ModifierKeyStateToShiftState(mModKeyState);
  }

  /**
   * GenericVirtualKeyCode() returns virtual keycode which cannot distinguish
   * position of modifier keys.  E.g., VK_CONTROL for both ControlLeft and
   * ControlRight.
   */
  uint8_t GenericVirtualKeyCode() const { return mOriginalVirtualKeyCode; }

  /**
   * SpecificVirtualKeyCode() returns virtual keycode which can distinguish
   * position of modifier keys.  E.g., returns VK_LCONTROL or VK_RCONTROL
   * instead of VK_CONTROL.  If the key message is synthesized with not
   * enough information, this prefers left position's keycode.
   */
  uint8_t SpecificVirtualKeyCode() const { return mVirtualKeyCode; }

 private:
  NativeKey* mLastInstance;
  // mRemovingMsg is set at removing a char message from
  // GetFollowingCharMessage().
  MSG mRemovingMsg;
  // mReceivedMsg is set when another instance starts to handle the message
  // unexpectedly.
  MSG mReceivedMsg;
  RefPtr<nsWindow> mWidget;
  RefPtr<TextEventDispatcher> mDispatcher;
  HKL mKeyboardLayout;
  MSG mMsg;
  // mFollowingCharMsgs stores WM_CHAR, WM_SYSCHAR, WM_DEADCHAR or
  // WM_SYSDEADCHAR message which follows WM_KEYDOWN.
  // Note that the stored messaged are already removed from the queue.
  // FYI: 5 is enough number for usual keyboard layout handling.  On Windows,
  // a dead key sequence may cause inputting up to 5 characters per key press.
  AutoTArray<MSG, 5> mFollowingCharMsgs;
  // mRemovedOddCharMsgs stores WM_CHAR messages which are caused by ATOK or
  // WXG (they are Japanese IME) when the user tries to do "Kakutei-undo"
  // (it means "undo the last commit").
  nsTArray<MSG> mRemovedOddCharMsgs;
  // If dispatching eKeyDown or eKeyPress event causes focus change,
  // the instance shouldn't handle remaning char messages.  For checking it,
  // this should store first focused window.
  HWND mFocusedWndBeforeDispatch;

  uint32_t mDOMKeyCode;
  KeyNameIndex mKeyNameIndex;
  CodeNameIndex mCodeNameIndex;

  ModifierKeyState mModKeyState;

  // mVirtualKeyCode distinguishes left key or right key of modifier key.
  uint8_t mVirtualKeyCode;
  // mOriginalVirtualKeyCode doesn't distinguish left key or right key of
  // modifier key.  However, if the given keycode is VK_PROCESS, it's resolved
  // to a keycode before it's handled by IME.
  uint8_t mOriginalVirtualKeyCode;

  // mCommittedChars indicates the inputted characters which is committed by
  // the key.  If dead key fail to composite a character, mCommittedChars
  // indicates both the dead characters and the base characters.
  UniCharsAndModifiers mCommittedCharsAndModifiers;

  // Following strings are computed by
  // ComputeInputtingStringWithKeyboardLayout() which is typically called
  // before dispatching keydown event.
  // mInputtingStringAndModifiers's string is the string to be
  // inputted into the focused editor and its modifier state is proper
  // modifier state for inputting the string into the editor.
  UniCharsAndModifiers mInputtingStringAndModifiers;
  // mShiftedString is the string to be inputted into the editor with
  // current modifier state with active shift state.
  UniCharsAndModifiers mShiftedString;
  // mUnshiftedString is the string to be inputted into the editor with
  // current modifier state without shift state.
  UniCharsAndModifiers mUnshiftedString;
  // Following integers are computed by
  // ComputeInputtingStringWithKeyboardLayout() which is typically called
  // before dispatching keydown event.  The meaning of these values is same
  // as charCode.
  uint32_t mShiftedLatinChar;
  uint32_t mUnshiftedLatinChar;

  WORD mScanCode;
  bool mIsExtended;
  // mIsRepeat is true if the key message is caused by the auto-repeat
  // feature.
  bool mIsRepeat;
  bool mIsDeadKey;
  // mIsPrintableKey is true if the key may be a printable key without
  // any modifier keys.  Otherwise, false.
  // Please note that the event may not cause any text input even if this
  // is true.  E.g., it might be dead key state or Ctrl key may be pressed.
  bool mIsPrintableKey;
  // mIsSkippableInRemoteProcess is false if the key event shouldn't be
  // skipped in the remote process even if it's too old event.
  bool mIsSkippableInRemoteProcess;
  // mCharMessageHasGone is true if the message is a keydown message and
  // it's followed by at least one char message but it's gone at removing
  // from the queue.  This could occur if PeekMessage() or something is
  // hooked by odd tool.
  bool mCharMessageHasGone;
  // mIsOverridingKeyboardLayout is true if the instance temporarily overriding
  // keyboard layout with specified by the constructor.
  bool mIsOverridingKeyboardLayout;
  // mCanIgnoreModifierStateAtKeyPress is true if it's allowed to remove
  // Ctrl or Alt modifier state at dispatching eKeyPress.
  bool mCanIgnoreModifierStateAtKeyPress;

  nsTArray<FakeCharMsg>* mFakeCharMsgs;

  // When a keydown event is dispatched at handling WM_APPCOMMAND, the computed
  // virtual keycode is set to this.  Even if we consume WM_APPCOMMAND message,
  // Windows may send WM_KEYDOWN and WM_KEYUP message for them.
  // At that time, we should not dispatch key events for them.
  static uint8_t sDispatchedKeyOfAppCommand;

  NativeKey() {
    MOZ_CRASH("The default constructor of NativeKey isn't available");
  }

  void InitWithAppCommand();
  void InitWithKeyOrChar();

  /**
   * InitIsSkippableForKeyOrChar() initializes mIsSkippableInRemoteProcess with
   * mIsRepeat and previous key message information.  So, this must be called
   * after mIsRepeat is initialized.
   */
  void InitIsSkippableForKeyOrChar(const MSG& aLastKeyMSG);

  /**
   * InitCommittedCharsAndModifiersWithFollowingCharMessages() initializes
   * mCommittedCharsAndModifiers with mFollowingCharMsgs and mModKeyState.
   * If mFollowingCharMsgs includes non-printable char messages, they are
   * ignored (skipped).
   */
  void InitCommittedCharsAndModifiersWithFollowingCharMessages();

  UINT GetScanCodeWithExtendedFlag() const;

  // The result is one of eKeyLocation*.
  uint32_t GetKeyLocation() const;

  /**
   * RemoveFollowingOddCharMessages() removes odd WM_CHAR messages from the
   * queue when IsIMEDoingKakuteiUndo() returns true.
   */
  void RemoveFollowingOddCharMessages();

  /**
   * "Kakutei-Undo" of ATOK or WXG (both of them are Japanese IME) causes
   * strange WM_KEYDOWN/WM_KEYUP/WM_CHAR message pattern.  So, when this
   * returns true, the caller needs to be careful for processing the messages.
   */
  bool IsIMEDoingKakuteiUndo() const;

  /**
   * This returns true if user types a number key in numpad with Alt key
   * to input a Unicode character from its scalar value.
   * Note that inputting Unicode scalar value is available without NumLock.
   * Therefore, this returns true even if user presses a function key on
   * numpad without NumLock, but that may be intended to perform a shortcut
   * key like Alt + Home.
   */
  bool MaybeTypingUnicodeScalarValue() const {
    return !mIsExtended && IsSysKeyDownOrKeyUpMessage() && IsAlt() &&
           !IsControl() && !IsShift() &&
           ((mScanCode >= 0x004F && mScanCode <= 0x0052) ||  // Numpad0-3
            (mScanCode >= 0x004B && mScanCode <= 0x004D) ||  // Numpad4-6
            (mScanCode >= 0x0047 && mScanCode <= 0x0049));   // Numpad7-9
  }

  bool IsKeyDownMessage() const {
    return mMsg.message == WM_KEYDOWN || mMsg.message == WM_SYSKEYDOWN;
  }
  bool IsSysKeyDownMessage() const { return mMsg.message == WM_SYSKEYDOWN; }
  bool IsKeyUpMessage() const {
    return mMsg.message == WM_KEYUP || mMsg.message == WM_SYSKEYUP;
  }
  bool IsSysKeyDownOrKeyUpMessage() const {
    return mMsg.message == WM_SYSKEYDOWN || mMsg.message == WM_SYSKEYUP;
  }
  bool IsCharOrSysCharMessage(const MSG& aMSG) const {
    return IsCharOrSysCharMessage(aMSG.message);
  }
  bool IsCharOrSysCharMessage(UINT aMessage) const {
    return (aMessage == WM_CHAR || aMessage == WM_SYSCHAR);
  }
  bool IsCharMessage(const MSG& aMSG) const {
    return IsCharMessage(aMSG.message);
  }
  bool IsCharMessage(UINT aMessage) const {
    return (IsCharOrSysCharMessage(aMessage) || IsDeadCharMessage(aMessage));
  }
  bool IsDeadCharMessage(const MSG& aMSG) const {
    return IsDeadCharMessage(aMSG.message);
  }
  bool IsDeadCharMessage(UINT aMessage) const {
    return (aMessage == WM_DEADCHAR || aMessage == WM_SYSDEADCHAR);
  }
  bool IsSysCharMessage(const MSG& aMSG) const {
    return IsSysCharMessage(aMSG.message);
  }
  bool IsSysCharMessage(UINT aMessage) const {
    return (aMessage == WM_SYSCHAR || aMessage == WM_SYSDEADCHAR);
  }
  bool MayBeSameCharMessage(const MSG& aCharMsg1, const MSG& aCharMsg2) const;
  bool IsSamePhysicalKeyMessage(const MSG& aKeyOrCharMsg1,
                                const MSG& aKeyOrCharMsg2) const;
  bool IsFollowedByPrintableCharMessage() const;
  bool IsFollowedByPrintableCharOrSysCharMessage() const;
  bool IsFollowedByDeadCharMessage() const;
  bool IsPrintableCharMessage(const MSG& aMSG) const {
    return aMSG.message == WM_CHAR &&
           !IsControlChar(static_cast<char16_t>(aMSG.wParam));
  }
  bool IsEnterKeyPressCharMessage(const MSG& aMSG) const {
    return aMSG.message == WM_CHAR && aMSG.wParam == '\r';
  }
  bool IsPrintableCharOrSysCharMessage(const MSG& aMSG) const {
    return IsCharOrSysCharMessage(aMSG) &&
           !IsControlChar(static_cast<char16_t>(aMSG.wParam));
  }
  bool IsControlCharMessage(const MSG& aMSG) const {
    return IsCharMessage(aMSG.message) &&
           IsControlChar(static_cast<char16_t>(aMSG.wParam));
  }

  /**
   * IsReservedBySystem() returns true if the key combination is reserved by
   * the system.  Even if it's consumed by web apps, the message should be
   * sent to next wndproc.
   */
  bool IsReservedBySystem() const;

  /**
   * GetFollowingCharMessage() returns following char message of handling
   * keydown event.  If the message is found, this method returns true.
   * Otherwise, returns false.
   *
   * WARNING: Even if this returns true, aCharMsg may be WM_NULL or its
   *          hwnd may be different window.
   */
  bool GetFollowingCharMessage(MSG& aCharMsg);

  /**
   * Wraps MapVirtualKeyEx() with MAPVK_VSC_TO_VK.
   */
  uint8_t ComputeVirtualKeyCodeFromScanCode() const;

  /**
   * Wraps MapVirtualKeyEx() with MAPVK_VSC_TO_VK_EX.
   */
  uint8_t ComputeVirtualKeyCodeFromScanCodeEx() const;

  /**
   * Wraps MapVirtualKeyEx() with MAPVK_VK_TO_VSC_EX or MAPVK_VK_TO_VSC.
   */
  uint16_t ComputeScanCodeExFromVirtualKeyCode(UINT aVirtualKeyCode) const;

  /**
   * Wraps MapVirtualKeyEx() with MAPVK_VSC_TO_VK and MAPVK_VK_TO_CHAR.
   */
  char16_t ComputeUnicharFromScanCode() const;

  /**
   * Initializes the aKeyEvent with the information stored in the instance.
   */
  nsEventStatus InitKeyEvent(WidgetKeyboardEvent& aKeyEvent,
                             const ModifierKeyState& aModKeyState) const;
  nsEventStatus InitKeyEvent(WidgetKeyboardEvent& aKeyEvent) const;

  /**
   * Dispatches a command event for aEventCommand.
   * Returns true if the event is consumed.  Otherwise, false.
   */
  bool DispatchCommandEvent(uint32_t aEventCommand) const;

  /**
   * DispatchKeyPressEventsWithRetrievedCharMessages() dispatches keypress
   * event(s) with retrieved char messages.
   */
  bool DispatchKeyPressEventsWithRetrievedCharMessages() const;

  /**
   * DispatchKeyPressEventsWithoutCharMessage() dispatches keypress event(s)
   * without char messages.  So, this should be used only when there are no
   * following char messages.
   */
  bool DispatchKeyPressEventsWithoutCharMessage() const;

  /**
   * Checkes whether the key event down message is handled without following
   * WM_CHAR messages.  For example, if following WM_CHAR message indicates
   * control character input, the WM_CHAR message is unclear whether it's
   * caused by a printable key with Ctrl or just a function key such as Enter
   * or Backspace.
   */
  bool NeedsToHandleWithoutFollowingCharMessages() const;

  /**
   * ComputeInputtingStringWithKeyboardLayout() computes string to be inputted
   * with the key and the modifier state, without shift state and with shift
   * state.
   */
  void ComputeInputtingStringWithKeyboardLayout();

  /**
   * IsFocusedWindowChanged() returns true if focused window is changed
   * after the instance is created.
   */
  bool IsFocusedWindowChanged() const {
    return mFocusedWndBeforeDispatch != ::GetFocus();
  }

  /**
   * Handles WM_CHAR message or WM_SYSCHAR message.  The instance must be
   * initialized with WM_KEYDOWN, WM_SYSKEYDOWN or them.
   * Returns true if dispatched keypress event is consumed.  Otherwise, false.
   */
  bool HandleCharMessage(const MSG& aCharMsg,
                         bool* aEventDispatched = nullptr) const;

  // Calls of PeekMessage() from NativeKey might cause nested message handling
  // due to (perhaps) odd API hook.  NativeKey should do nothing if given
  // message is tried to be retrieved by another instance.

  /**
   * sLatestInstacne is a pointer to the newest instance of NativeKey which is
   * handling a key or char message(s).
   */
  static NativeKey* sLatestInstance;

  static const MSG sEmptyMSG;

  static MSG sLastKeyOrCharMSG;

  static MSG sLastKeyMSG;

  // Set to non-zero if we receive a WM_KEYDOWN message which introduces only
  // a high surrogate.  Then, it'll be cleared when next keydown or char message
  // is received.
  static char16_t sPendingHighSurrogate;

  static bool IsEmptyMSG(const MSG& aMSG) {
    return !memcmp(&aMSG, &sEmptyMSG, sizeof(MSG));
  }

  bool IsAnotherInstanceRemovingCharMessage() const {
    return mLastInstance && !IsEmptyMSG(mLastInstance->mRemovingMsg);
  }

 public:
  /**
   * Returns last key or char MSG.  If no MSG has been received yet, the result
   * is empty MSG (i.e., .message is WM_NULL).
   */
  static const MSG& LastKeyOrCharMSG() { return sLastKeyOrCharMSG; }
};

class KeyboardLayout {
 public:
  static KeyboardLayout* GetInstance();
  static void Shutdown();

  /**
   * GetLayout() returns a keyboard layout which has already been loaded in the
   * singleton instance or active keyboard layout.
   */
  static HKL GetLayout() {
    if (!sInstance || sInstance->mIsPendingToRestoreKeyboardLayout) {
      return ::GetKeyboardLayout(0);
    }
    return sInstance->mKeyboardLayout;
  }

  /**
   * GetLoadedLayout() returns a keyboard layout which was loaded in the
   * singleton instance.  This may be different from the active keyboard layout
   * on the system if we override the keyboard layout for synthesizing native
   * key events for tests.
   */
  HKL GetLoadedLayout() { return mKeyboardLayout; }

  /**
   * GetLoadedLayoutName() returns the name of the loaded keyboard layout in the
   * singleton instance.
   */
  nsCString GetLoadedLayoutName() {
    return KeyboardLayout::GetLayoutName(mKeyboardLayout);
  }

  static void NotifyIdleServiceOfUserActivity();

  static bool IsPrintableCharKey(uint8_t aVirtualKey);

  /**
   * HasAltGr() returns true if the keyboard layout's AltRight key is AltGr
   * key.
   */
  bool HasAltGr() const { return mHasAltGr; }

  /**
   * IsDeadKey() returns true if aVirtualKey is a dead key with aModKeyState.
   * This method isn't stateful.
   */
  bool IsDeadKey(uint8_t aVirtualKey,
                 const ModifierKeyState& aModKeyState) const;
  bool IsDeadKey(const NativeKey& aNativeKey) const {
    return IsDeadKey(aNativeKey.GenericVirtualKeyCode(),
                     aNativeKey.ModifierKeyStateRef());
  }

  /**
   * IsInDeadKeySequence() returns true when it's in a dead key sequence.
   * It starts when a dead key is down and ends when another key down causes
   * inactivating the dead key state.
   */
  bool IsInDeadKeySequence() const { return !mActiveDeadKeys.IsEmpty(); }

  /**
   * IsSysKey() returns true if aVirtualKey with aModKeyState causes WM_SYSKEY*
   * or WM_SYS*CHAR messages.
   */
  bool IsSysKey(uint8_t aVirtualKey,
                const ModifierKeyState& aModKeyState) const;
  bool IsSysKey(const NativeKey& aNativeKey) const {
    return IsSysKey(aNativeKey.GenericVirtualKeyCode(),
                    aNativeKey.ModifierKeyStateRef());
  }

  /**
   * GetUniCharsAndModifiers() returns characters which are inputted by
   * aVirtualKey with aModKeyState.  This method isn't stateful.
   * Note that if the combination causes text input, the result's Ctrl and
   * Alt key state are never active.
   */
  UniCharsAndModifiers GetUniCharsAndModifiers(
      uint8_t aVirtualKey, const ModifierKeyState& aModKeyState) const {
    VirtualKey::ShiftState shiftState =
        VirtualKey::ModifierKeyStateToShiftState(aModKeyState);
    return GetUniCharsAndModifiers(aVirtualKey, shiftState);
  }
  UniCharsAndModifiers GetUniCharsAndModifiers(
      const NativeKey& aNativeKey) const {
    return GetUniCharsAndModifiers(aNativeKey.GenericVirtualKeyCode(),
                                   aNativeKey.GetShiftState());
  }

  /**
   * OnLayoutChange() must be called before the first keydown message is
   * received.  LoadLayout() changes the keyboard state, that causes breaking
   * dead key state.  Therefore, we need to load the layout before the first
   * keydown message.
   */
  void OnLayoutChange(HKL aKeyboardLayout) {
    MOZ_ASSERT(!mIsOverridden);
    LoadLayout(aKeyboardLayout);
  }

  /**
   * OverrideLayout() loads the specified keyboard layout.
   */
  void OverrideLayout(HKL aLayout) {
    mIsOverridden = true;
    LoadLayout(aLayout);
  }

  /**
   * RestoreLayout() loads the current keyboard layout of the thread.
   */
  void RestoreLayout() {
    mIsOverridden = false;
    mIsPendingToRestoreKeyboardLayout = true;
  }

  uint32_t ConvertNativeKeyCodeToDOMKeyCode(UINT aNativeKeyCode) const;

  /**
   * ConvertNativeKeyCodeToKeyNameIndex() returns KeyNameIndex value for
   * non-printable keys (except some special keys like space key).
   */
  KeyNameIndex ConvertNativeKeyCodeToKeyNameIndex(uint8_t aVirtualKey) const;

  /**
   * ConvertScanCodeToCodeNameIndex() returns CodeNameIndex value for
   * the given scan code.  aScanCode can be over 0xE000 since this method
   * doesn't use Windows API.
   */
  static CodeNameIndex ConvertScanCodeToCodeNameIndex(UINT aScanCode);

  /**
   * This wraps MapVirtualKeyEx() API with MAPVK_VK_TO_VSC.
   */
  WORD ComputeScanCodeForVirtualKeyCode(uint8_t aVirtualKeyCode) const;

  /**
   * Implementation of nsIWidget::SynthesizeNativeKeyEvent().
   */
  nsresult SynthesizeNativeKeyEvent(nsWindow* aWidget,
                                    int32_t aNativeKeyboardLayout,
                                    int32_t aNativeKeyCode,
                                    nsIWidget::NativeModifiers aModifierFlags,
                                    const nsAString& aCharacters,
                                    const nsAString& aUnmodifiedCharacters);

 private:
  KeyboardLayout();
  ~KeyboardLayout();

  static KeyboardLayout* sInstance;
  static StaticRefPtr<nsIUserIdleServiceInternal> sIdleService;

  HKL mKeyboardLayout = nullptr;

  VirtualKey mVirtualKeys[NS_NUM_OF_KEYS] = {};
  nsTArray<DeadKeyTable*> mDeadKeyTableList;
  // When mActiveDeadKeys is empty, it's not in dead key sequence.
  // Otherwise, it contains virtual keycodes which are pressed in current
  // dead key sequence.
  nsTArray<uint8_t> mActiveDeadKeys;
  // mDeadKeyShiftStates is always same length as mActiveDeadKeys.
  // This stores shift states at pressing each dead key stored in
  // mActiveDeadKeys.
  nsTArray<VirtualKey::ShiftState> mDeadKeyShiftStates;

  bool mIsOverridden = false;
  bool mIsPendingToRestoreKeyboardLayout = false;
  bool mHasAltGr = false;

  static inline int32_t GetKeyIndex(uint8_t aVirtualKey);
  static void AddDeadKeyEntry(char16_t aBaseChar, char16_t aCompositeChar,
                              DeadKeyTable&);
  bool EnsureDeadKeyActive(bool aIsActive, uint8_t aDeadKey,
                           const PBYTE aDeadKeyKbdState);
  void GetDeadKeyCombinations(uint8_t aDeadKey, const PBYTE aDeadKeyKbdState,
                              uint16_t aShiftStatesWithBaseChars,
                              DeadKeyTable&);
  /**
   * Activates or deactivates dead key state.
   */
  void ActivateDeadKeyState(const NativeKey& aNativeKey);
  void DeactivateDeadKeyState();

  const DeadKeyTable* AddDeadKeyTable(const DeadKeyTable&);
  void ReleaseDeadKeyTables();

  /**
   * Loads the specified keyboard layout. This method always clear the dead key
   * state.
   */
  void LoadLayout(HKL aLayout);

  /**
   * Gets the keyboard layout name of aLayout.  Be careful, this may be too
   * slow to call at handling user input.
   */
  static nsCString GetLayoutName(HKL aLayout);

  /**
   * InitNativeKey() must be called when actually widget receives WM_KEYDOWN or
   * WM_KEYUP.  This method is stateful.  This saves current dead key state at
   * WM_KEYDOWN.  Additionally, computes current inputted character(s) and set
   * them to the aNativeKey.
   */
  void InitNativeKey(NativeKey& aNativeKey);

  /**
   * MaybeInitNativeKeyAsDeadKey() initializes aNativeKey only when aNativeKey
   * is a dead key's event.
   * When it's not in a dead key sequence, this activates the dead key state.
   * When it's in a dead key sequence, this initializes aNativeKey with a
   * composite character or a preceding dead char and a dead char which should
   * be caused by aNativeKey.
   * Returns true when this initializes aNativeKey.  Otherwise, false.
   */
  bool MaybeInitNativeKeyAsDeadKey(NativeKey& aNativeKey);

  /**
   * MaybeInitNativeKeyWithCompositeChar() may initialize aNativeKey with
   * proper composite character when dead key produces a composite character.
   * Otherwise, just returns false.
   */
  bool MaybeInitNativeKeyWithCompositeChar(NativeKey& aNativeKey);

  /**
   * See the comment of GetUniCharsAndModifiers() below.
   */
  UniCharsAndModifiers GetUniCharsAndModifiers(
      uint8_t aVirtualKey, VirtualKey::ShiftState aShiftState) const;

  /**
   * GetDeadUniCharsAndModifiers() returns dead chars which are stored in
   * current dead key sequence.  So, this is stateful.
   */
  UniCharsAndModifiers GetDeadUniCharsAndModifiers() const;

  /**
   * GetCompositeChar() returns a composite character with dead character
   * caused by mActiveDeadKeys, mDeadKeyShiftStates and a base character
   * (aBaseChar).
   * If the combination of the dead character and the base character doesn't
   * cause a composite character, this returns 0.
   */
  char16_t GetCompositeChar(char16_t aBaseChar) const;

  // NativeKey class should access InitNativeKey() directly, but it shouldn't
  // be available outside of NativeKey.  So, let's make NativeKey a friend
  // class of this.
  friend class NativeKey;
};

class RedirectedKeyDownMessageManager {
 public:
  /*
   * If a window receives WM_KEYDOWN message or WM_SYSKEYDOWM message which is
   * a redirected message, NativeKey::DispatchKeyDownAndKeyPressEvent()
   * prevents to dispatch eKeyDown event because it has been dispatched
   * before the message was redirected.  However, in some cases, WM_*KEYDOWN
   * message handler may not handle actually.  Then, the message handler needs
   * to forget the redirected message and remove WM_CHAR message or WM_SYSCHAR
   * message for the redirected keydown message.  AutoFlusher class is a helper
   * class for doing it.  This must be created in the stack.
   */
  class MOZ_STACK_CLASS AutoFlusher final {
   public:
    AutoFlusher(nsWindow* aWidget, const MSG& aMsg)
        : mCancel(!RedirectedKeyDownMessageManager::IsRedirectedMessage(aMsg)),
          mWidget(aWidget),
          mMsg(aMsg) {}

    ~AutoFlusher() {
      if (mCancel) {
        return;
      }
      // Prevent unnecessary keypress event
      if (!mWidget->Destroyed()) {
        RedirectedKeyDownMessageManager::RemoveNextCharMessage(mMsg.hwnd);
      }
      // Foreget the redirected message
      RedirectedKeyDownMessageManager::Forget();
    }

    void Cancel() { mCancel = true; }

   private:
    bool mCancel;
    RefPtr<nsWindow> mWidget;
    const MSG& mMsg;
  };

  static void WillRedirect(const MSG& aMsg, bool aDefaultPrevented) {
    sRedirectedKeyDownMsg = aMsg;
    sDefaultPreventedOfRedirectedMsg = aDefaultPrevented;
  }

  static void Forget() { sRedirectedKeyDownMsg.message = WM_NULL; }

  static void PreventDefault() { sDefaultPreventedOfRedirectedMsg = true; }
  static bool DefaultPrevented() { return sDefaultPreventedOfRedirectedMsg; }

  static bool IsRedirectedMessage(const MSG& aMsg);

  /**
   * RemoveNextCharMessage() should be called by WM_KEYDOWN or WM_SYSKEYDOWM
   * message handler.  If there is no WM_(SYS)CHAR message for it, this
   * method does nothing.
   * NOTE: WM_(SYS)CHAR message is posted by TranslateMessage() API which is
   * called in message loop.  So, WM_(SYS)KEYDOWN message should have
   * WM_(SYS)CHAR message in the queue if the keydown event causes character
   * input.
   */
  static void RemoveNextCharMessage(HWND aWnd);

 private:
  // sRedirectedKeyDownMsg is WM_KEYDOWN message or WM_SYSKEYDOWN message which
  // is reirected with SendInput() API by
  // widget::NativeKey::DispatchKeyDownAndKeyPressEvent()
  static MSG sRedirectedKeyDownMsg;
  static bool sDefaultPreventedOfRedirectedMsg;
};

}  // namespace widget
}  // namespace mozilla

#endif
