/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/KeyboardEvent.h"

#include "mozilla/BasicEvents.h"
#include "mozilla/LookAndFeel.h"
#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/TextEvents.h"
#include "mozilla/Utf16.h"
#include "mozilla/dom/Document.h"
#include "nsContentUtils.h"
#include "nsIPrincipal.h"
#include "nsRFPService.h"
#include "prtime.h"

namespace mozilla::dom {

KeyboardEvent::KeyboardEvent(EventTarget* aOwner, nsPresContext* aPresContext,
                             WidgetKeyboardEvent* aEvent)
    : UIEvent(aOwner, aPresContext,
              aEvent ? aEvent
                     : new WidgetKeyboardEvent(false, eVoidEvent, nullptr)),
      mInitializedByJS(false),
      mInitializedByCtor(false),
      mInitializedWhichValue(0) {
  if (aEvent) {
    mEventIsInternal = false;
  } else {
    mEventIsInternal = true;
    mEvent->AsKeyboardEvent()->mKeyNameIndex = KEY_NAME_INDEX_USE_STRING;
  }
}

bool KeyboardEvent::IsMenuAccessKeyPressed() const {
  Modifiers mask = LookAndFeel::GetMenuAccessKeyModifiers();
  Modifiers modifiers = GetModifiersForMenuAccessKey();
  return mask != MODIFIER_SHIFT && (modifiers & mask) &&
         (modifiers & ~(mask | MODIFIER_SHIFT)) == 0;
}

static constexpr Modifiers kPossibleModifiersForAccessKey =
    MODIFIER_SHIFT | MODIFIER_CONTROL | MODIFIER_ALT | MODIFIER_META;

Modifiers KeyboardEvent::GetModifiersForMenuAccessKey() const {
  const WidgetInputEvent* inputEvent = WidgetEventPtr()->AsInputEvent();
  MOZ_ASSERT(inputEvent);
  return inputEvent->mModifiers & kPossibleModifiersForAccessKey;
}

bool KeyboardEvent::AltKey(CallerType aCallerType) {
  bool altState = mEvent->AsKeyboardEvent()->IsAlt();

  if (!ShouldResistFingerprinting(aCallerType)) {
    return altState;
  }

  // We need to give a spoofed state for Alt key since it could be used as a
  // modifier key in certain keyboard layout. For example, the '@' key for
  // German keyboard for MAC is Alt+L.
  return GetSpoofedModifierStates(Modifier::MODIFIER_ALT, altState);
}

bool KeyboardEvent::CtrlKey(CallerType aCallerType) {
  // We don't spoof this key when privacy.resistFingerprinting
  // is enabled, because it is often used for command key
  // combinations in web apps.
  return mEvent->AsKeyboardEvent()->IsControl();
}

bool KeyboardEvent::ShiftKey(CallerType aCallerType) {
  bool shiftState = mEvent->AsKeyboardEvent()->IsShift();

  if (!ShouldResistFingerprinting(aCallerType)) {
    return shiftState;
  }

  return GetSpoofedModifierStates(Modifier::MODIFIER_SHIFT, shiftState);
}

bool KeyboardEvent::MetaKey() {
  // We don't spoof this key when privacy.resistFingerprinting
  // is enabled, because it is often used for command key
  // combinations in web apps.
  return mEvent->AsKeyboardEvent()->IsMeta();
}

bool KeyboardEvent::Repeat() { return mEvent->AsKeyboardEvent()->mIsRepeat; }

bool KeyboardEvent::IsComposing() {
  return mEvent->AsKeyboardEvent()->mIsComposing;
}

void KeyboardEvent::GetKey(nsAString& aKeyName) const {
  mEvent->AsKeyboardEvent()->GetDOMKeyName(aKeyName);
}

void KeyboardEvent::GetCode(nsAString& aCodeName, CallerType aCallerType) {
  if (!ShouldResistFingerprinting(aCallerType)) {
    mEvent->AsKeyboardEvent()->GetDOMCodeName(aCodeName);
    return;
  }

  // When fingerprinting resistance is enabled, we will give a spoofed code
  // according to the content-language of the document.
  nsCOMPtr<Document> doc = GetDocument();

  nsRFPService::GetSpoofedCode(doc, mEvent->AsKeyboardEvent(), aCodeName);
}

void KeyboardEvent::GetInitDict(KeyboardEventInit& aParam) {
  GetKey(aParam.mKey);
  GetCode(aParam.mCode);
  aParam.mLocation = Location();
  aParam.mRepeat = Repeat();
  aParam.mIsComposing = IsComposing();

  // legacy attributes
  aParam.mKeyCode = KeyCode();
  aParam.mCharCode = CharCode();
  aParam.mWhich = Which();

  // modifiers from EventModifierInit
  aParam.mCtrlKey = CtrlKey();
  aParam.mShiftKey = ShiftKey();
  aParam.mAltKey = AltKey();
  aParam.mMetaKey = MetaKey();

  WidgetKeyboardEvent* internalEvent = mEvent->AsKeyboardEvent();
  aParam.mModifierAltGraph = internalEvent->IsAltGraph();
  aParam.mModifierCapsLock = internalEvent->IsCapsLocked();
  aParam.mModifierFn = internalEvent->IsFn();
  aParam.mModifierFnLock = internalEvent->IsFnLocked();
  aParam.mModifierNumLock = internalEvent->IsNumLocked();
  aParam.mModifierScrollLock = internalEvent->IsScrollLocked();
  aParam.mModifierSymbol = internalEvent->IsSymbol();
  aParam.mModifierSymbolLock = internalEvent->IsSymbolLocked();

  // EventInit
  aParam.mBubbles = internalEvent->mFlags.mBubbles;
  aParam.mCancelable = internalEvent->mFlags.mCancelable;
}

static uint32_t ConvertCharCodeToUpperCaseIfSafe(uint32_t aCharCode) {
  if (!IsInBMP(aCharCode)) {
    return aCharCode;
  }
  const uint32_t upperCh = ToUpperCase(aCharCode);
  if (upperCh == aCharCode) {
    return aCharCode;
  }
  const uint32_t lowerCh = ToLowerCase(upperCh);
  return lowerCh == aCharCode ? upperCh : aCharCode;
}

void KeyboardEvent::GetRegisterableShortcutKeys(
    nsTArray<RegisterableShortcutKey>& aCandidates) const {
  if (!IsTrusted() || !mEvent ||
      (mEvent->mMessage != eKeyDown && mEvent->mMessage != eKeyPress)) {
    return;
  }
  MOZ_DIAGNOSTIC_ASSERT(mEvent->AsKeyboardEvent());
  const WidgetKeyboardEvent& widgetKeyboardEvent =
      static_cast<WidgetKeyboardEvent&>(*mEvent);
  // We cannot register a key combination as a shortcut key if it introduces a
  // dead character or this event was processed by IME.
  if (widgetKeyboardEvent.mKeyNameIndex == KEY_NAME_INDEX_Dead ||
      widgetKeyboardEvent.mKeyNameIndex == KEY_NAME_INDEX_Process) {
    return;
  }
  // If the event is a non-printable key like `Backspace`, `Enter`, F1`, etc,
  // we should set only `mKeyCode`.
  if (!widgetKeyboardEvent.PseudoCharCode()) {
    if (widgetKeyboardEvent.IsModifierKeyEvent() ||
        !widgetKeyboardEvent.mKeyCode) {
      return;
    }
    RegisterableShortcutKey& shortcutKey = *aCandidates.AppendElement();
    shortcutKey.mKeyCode = widgetKeyboardEvent.mKeyCode;
    shortcutKey.mAltKey = widgetKeyboardEvent.IsAlt();
    shortcutKey.mCtrlKey = widgetKeyboardEvent.IsControl();
    shortcutKey.mMetaKey = widgetKeyboardEvent.IsMeta();
    shortcutKey.mShiftKey = widgetKeyboardEvent.IsShift();
    return;
  }
  // Otherwise, we need to return candidates with checking the alternative char
  // codes because the character introduced with current keyboard layout may not
  // be available with other keyboard layouts. E.g., the key introduces a
  // Unicode character like Arabic, Cyrillic, Greek or Hebrew, but the user may
  // want to use the key combination when they change the layout to ASCII
  // capable keyboard layout like QWERTY en-US layout.
  using ShiftState = ShortcutKeyCandidate::ShiftState;
  AutoShortcutKeyCandidateArray candidates;
  widgetKeyboardEvent.GetRegisterableShortcutKeys(candidates);
  for (const auto& candidate : candidates) {
    MOZ_DIAGNOSTIC_ASSERT(candidate.mCharCode);
    RegisterableShortcutKey& shortcutKey = *aCandidates.AppendElement();
    AppendUCS4ToUTF16(
        // UI should display the character in uppercase if it can be converted
        // back to the original lowercase character (i.e., candidate.mCharCode).
        // To ensure shortcut character matching uses the same logic as
        // KeyEventHandler, perform the conversion here rather than in JS.
        ConvertCharCodeToUpperCaseIfSafe(candidate.mCharCode),
        shortcutKey.mKey);
    shortcutKey.mAltKey = widgetKeyboardEvent.IsAlt();
    shortcutKey.mCtrlKey = widgetKeyboardEvent.IsControl();
    shortcutKey.mMetaKey = widgetKeyboardEvent.IsMeta();
    shortcutKey.mShiftKey = widgetKeyboardEvent.IsShift() &&
                            candidate.mShiftState == ShiftState::MatchExactly;
  }
}

bool KeyboardEvent::ShouldUseSameValueForCharCodeAndKeyCode(
    const WidgetKeyboardEvent& aWidgetKeyboardEvent,
    CallerType aCallerType) const {
  // - If this event is initialized by JS, we don't need to return same value
  //   for keyCode and charCode since they can be initialized separately.
  // - If this is not a keypress event, we shouldn't return same value for
  //   keyCode and charCode.
  // - If we need to return legacy keyCode and charCode values for the web
  //   app due to in the blacklist.
  // - If this event is referred by default handler, i.e., the caller is
  //   system or this event is now in the system group, we don't need to use
  //   hack for web-compat.
  if (mInitializedByJS || aWidgetKeyboardEvent.mMessage != eKeyPress ||
      aWidgetKeyboardEvent.mUseLegacyKeyCodeAndCharCodeValues ||
      aCallerType == CallerType::System ||
      aWidgetKeyboardEvent.mFlags.mInSystemGroup) {
    return false;
  }

  MOZ_ASSERT(aCallerType == CallerType::NonSystem);

  return StaticPrefs::
      dom_keyboardevent_keypress_set_keycode_and_charcode_to_same_value();
}

uint32_t KeyboardEvent::CharCode(CallerType aCallerType) {
  WidgetKeyboardEvent* widgetKeyboardEvent = mEvent->AsKeyboardEvent();
  if (mInitializedByJS) {
    // If this is initialized by Ctor, we should return the initialized value.
    if (mInitializedByCtor) {
      return widgetKeyboardEvent->mCharCode;
    }
    // Otherwise, i.e., initialized by InitKey*Event(), we should return the
    // initialized value only when eKeyPress or eAccessKeyNotFound event.
    // Although this is odd, but our traditional behavior.
    return widgetKeyboardEvent->mMessage == eKeyPress ||
                   widgetKeyboardEvent->mMessage == eAccessKeyNotFound
               ? widgetKeyboardEvent->mCharCode
               : 0;
  }

  // If the key is a function key, we should return the result of KeyCode()
  // even from CharCode().  Otherwise, i.e., the key may be a printable
  // key or actually a printable key, we should return the given charCode
  // value.

  if (widgetKeyboardEvent->mKeyNameIndex != KEY_NAME_INDEX_USE_STRING &&
      ShouldUseSameValueForCharCodeAndKeyCode(*widgetKeyboardEvent,
                                              aCallerType)) {
    return ComputeTraditionalKeyCode(*widgetKeyboardEvent, aCallerType);
  }

  return widgetKeyboardEvent->mCharCode;
}

uint32_t KeyboardEvent::KeyCode(CallerType aCallerType) {
  WidgetKeyboardEvent* widgetKeyboardEvent = mEvent->AsKeyboardEvent();
  if (mInitializedByJS) {
    // If this is initialized by Ctor, we should return the initialized value.
    if (mInitializedByCtor) {
      return widgetKeyboardEvent->mKeyCode;
    }
    // Otherwise, i.e., initialized by InitKey*Event(), we should return the
    // initialized value only when the event message is a valid keyboard event
    // message.  Although this is odd, but our traditional behavior.
    // NOTE: The fix of bug 1222285 changed the behavior temporarily if
    //       spoofing is enabled.  However, the behavior does not make sense
    //       since if the event is generated by JS, the behavior shouldn't
    //       be changed by whether spoofing is enabled or not.  Therefore,
    //       we take back the original behavior.
    return widgetKeyboardEvent->HasKeyEventMessage()
               ? widgetKeyboardEvent->mKeyCode
               : 0;
  }

  // If the key is not a function key, i.e., the key may be a printable key
  // or a function key mapped as a printable key, we should use charCode value
  // for keyCode value if this is a "keypress" event.

  if (widgetKeyboardEvent->mKeyNameIndex == KEY_NAME_INDEX_USE_STRING &&
      ShouldUseSameValueForCharCodeAndKeyCode(*widgetKeyboardEvent,
                                              aCallerType)) {
    return widgetKeyboardEvent->mCharCode;
  }

  return ComputeTraditionalKeyCode(*widgetKeyboardEvent, aCallerType);
}

uint32_t KeyboardEvent::ComputeTraditionalKeyCode(
    WidgetKeyboardEvent& aKeyboardEvent, CallerType aCallerType) {
  if (!ShouldResistFingerprinting(aCallerType)) {
    return aKeyboardEvent.mKeyCode;
  }

  // In Netscape style (i.e., traditional behavior of Gecko), the keyCode
  // should be zero if the char code is given.
  if ((mEvent->mMessage == eKeyPress ||
       mEvent->mMessage == eAccessKeyNotFound) &&
      aKeyboardEvent.mCharCode) {
    return 0;
  }

  // When fingerprinting resistance is enabled, we will give a spoofed keyCode
  // according to the content-language of the document.
  nsCOMPtr<Document> doc = GetDocument();
  uint32_t spoofedKeyCode;

  if (nsRFPService::GetSpoofedKeyCode(doc, &aKeyboardEvent, spoofedKeyCode)) {
    return spoofedKeyCode;
  }

  return 0;
}

uint32_t KeyboardEvent::Which(CallerType aCallerType) {
  // If this event is initialized with ctor, which can have independent value.
  if (mInitializedByCtor) {
    return mInitializedWhichValue;
  }

  switch (mEvent->mMessage) {
    case eKeyDown:
    case eKeyUp:
      return KeyCode(aCallerType);
    case eKeyPress:
      // Special case for 4xp bug 62878.  Try to make value of which
      // more closely mirror the values that 4.x gave for RETURN and BACKSPACE
      {
        uint32_t keyCode = mEvent->AsKeyboardEvent()->mKeyCode;
        if (keyCode == NS_VK_RETURN || keyCode == NS_VK_BACK) {
          return keyCode;
        }
        return CharCode();
      }
    default:
      break;
  }

  return 0;
}

uint32_t KeyboardEvent::Location() {
  return mEvent->AsKeyboardEvent()->mLocation;
}

// static
already_AddRefed<KeyboardEvent> KeyboardEvent::ConstructorJS(
    const GlobalObject& aGlobal, const nsAString& aType,
    const KeyboardEventInit& aParam) {
  nsCOMPtr<EventTarget> target = do_QueryInterface(aGlobal.GetAsSupports());
  RefPtr<KeyboardEvent> newEvent = new KeyboardEvent(target, nullptr, nullptr);
  bool trusted = newEvent->Init(target);
  newEvent->InitUIEvent(aType, aParam.mBubbles, aParam.mCancelable,
                        aParam.mView, aParam.mDetail);
  newEvent->InitModifiers(aParam);
  newEvent->SetTrusted(trusted);
  newEvent->mInitializedByJS = true;
  newEvent->mInitializedByCtor = true;
  newEvent->mInitializedWhichValue = aParam.mWhich;

  WidgetKeyboardEvent* internalEvent =
      newEvent->WidgetEventPtr()->AsKeyboardEvent();
  internalEvent->mLocation = aParam.mLocation;
  internalEvent->mIsRepeat = aParam.mRepeat;
  internalEvent->mIsComposing = aParam.mIsComposing;
  internalEvent->mKeyNameIndex =
      WidgetKeyboardEvent::GetKeyNameIndex(aParam.mKey);
  if (internalEvent->mKeyNameIndex == KEY_NAME_INDEX_USE_STRING) {
    internalEvent->mKeyValue = aParam.mKey;
  }
  internalEvent->mCodeNameIndex =
      WidgetKeyboardEvent::GetCodeNameIndex(aParam.mCode);
  if (internalEvent->mCodeNameIndex == CODE_NAME_INDEX_USE_STRING) {
    internalEvent->mCodeValue = aParam.mCode;
  }
  internalEvent->mCharCode = aParam.mCharCode;
  internalEvent->mKeyCode = aParam.mKeyCode;

  return newEvent.forget();
}

void KeyboardEvent::InitKeyboardEventJS(
    const nsAString& aType, bool aCanBubble, bool aCancelable,
    nsGlobalWindowInner* aView, const nsAString& aKey, uint32_t aLocation,
    bool aCtrlKey, bool aAltKey, bool aShiftKey, bool aMetaKey) {
  NS_ENSURE_TRUE_VOID(!mEvent->mFlags.mIsBeingDispatched);
  mInitializedByJS = true;
  mInitializedByCtor = false;

  UIEvent::InitUIEvent(aType, aCanBubble, aCancelable, aView, 0);

  WidgetKeyboardEvent* keyEvent = mEvent->AsKeyboardEvent();
  keyEvent->InitBasicModifiers(aCtrlKey, aAltKey, aShiftKey, aMetaKey);
  keyEvent->mLocation = aLocation;
  keyEvent->mKeyNameIndex = KEY_NAME_INDEX_USE_STRING;
  keyEvent->mKeyValue = aKey;
}

bool KeyboardEvent::ShouldResistFingerprinting(CallerType aCallerType) {
  // There are five situations we don't need to spoof this keyboard event.
  //   1. The pref privcy.resistFingerprinting' is false, we fast return here.
  //   2. This event is initialized by scripts.
  //   3. This event is from Numpad.
  //   4. This event is in the system group.
  //   5. The caller type is system.
  if (!nsContentUtils::ShouldResistFingerprinting("Efficiency Check",
                                                  RFPTarget::KeyboardEvents) ||
      mInitializedByJS || aCallerType == CallerType::System ||
      mEvent->mFlags.mInSystemGroup ||
      mEvent->AsKeyboardEvent()->mLocation ==
          KeyboardEvent_Binding::DOM_KEY_LOCATION_NUMPAD) {
    return false;
  }

  nsCOMPtr<Document> doc = GetDocument();
  // We've checked the pref above, so use true as fallback if doc is null.
  return doc ? doc->ShouldResistFingerprinting(RFPTarget::KeyboardEvents)
             : true;
}

bool KeyboardEvent::GetSpoofedModifierStates(const Modifiers aModifierKey,
                                             const bool aRawModifierState) {
  bool spoofedState;
  nsCOMPtr<Document> doc = GetDocument();

  if (nsRFPService::GetSpoofedModifierStates(doc, mEvent->AsKeyboardEvent(),
                                             aModifierKey, spoofedState)) {
    return spoofedState;
  }

  return aRawModifierState;
}

}  // namespace mozilla::dom

using namespace mozilla;
using namespace mozilla::dom;

already_AddRefed<KeyboardEvent> NS_NewDOMKeyboardEvent(
    EventTarget* aOwner, nsPresContext* aPresContext,
    WidgetKeyboardEvent* aEvent) {
  RefPtr<KeyboardEvent> it = new KeyboardEvent(aOwner, aPresContext, aEvent);
  return it.forget();
}
