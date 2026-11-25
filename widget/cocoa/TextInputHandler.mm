/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "TextInputHandler.h"
#include <Foundation/Foundation.h>
#include <Foundation/NSObjCRuntime.h>
#include <fmt/format.h>

#include "mozilla/Logging.h"

#include "mozilla/AutoRestore.h"
#include "mozilla/CheckedInt.h"
#include "mozilla/MacStringHelpers.h"
#include "mozilla/MiscEvents.h"
#include "mozilla/MouseEvents.h"
#include "mozilla/StaticPrefs_intl.h"
#include "mozilla/StaticPrefs_widget.h"
#include "mozilla/TextEventDispatcher.h"
#include "mozilla/TextEvents.h"
#include "mozilla/ToString.h"
#include "mozilla/Utf16.h"
#include "mozilla/glean/WidgetCocoaMetrics.h"

#include "WidgetUtils.h"
#include "nsBidiUtils.h"
#include "nsCocoaUtils.h"
#include "nsCocoaWindow.h"
#include "nsObjCExceptions.h"
#include "nsPrintfCString.h"
#include "nsToolkit.h"

using namespace mozilla;
using namespace mozilla::widget;

// For collecting other people's log, tell them `MOZ_LOG=IMEHandler:4,sync`
// rather than `MOZ_LOG=IMEHandler:5,sync` since using `5` may create too
// big file.
// Therefore you shouldn't use `LogLevel::Verbose` for logging usual behavior.
LazyLogModule gIMELog("IMEHandler");

// For collecting other people's log, tell them `MOZ_LOG=KeyboardHandler:4,sync`
// rather than `MOZ_LOG=KeyboardHandler:5,sync` since using `5` may create too
// big file.
// Therefore you shouldn't use `LogLevel::Verbose` for logging usual behavior.
LazyLogModule gKeyLog("KeyboardHandler");

// The behavior of `TextInputHandler` class is important both for logging
// keyboard handler and IME handler.  Therefore, the behavior is logged when
// either `IMEHandler` or `KeyboardHandler` is set to `MOZ_LOG`.  Therefore,
// you may not need to tell people
// `MOZ_LOG=IMEHandler:4,KeyboardHandler:4,sync`.
#define MOZ_LOG_KEY_OR_IME(aLogLevel, aArgs)                               \
  MOZ_LOG(MOZ_LOG_TEST(gIMELog, aLogLevel) ? gIMELog : gKeyLog, aLogLevel, \
          aArgs)

static const char* GetKeyNameForNativeKeyCode(unsigned short aNativeKeyCode) {
  switch (aNativeKeyCode) {
    case kVK_Escape:
      return "Escape";
    case kVK_RightCommand:
      return "Right-Command";
    case kVK_Command:
      return "Command";
    case kVK_Shift:
      return "Shift";
    case kVK_CapsLock:
      return "CapsLock";
    case kVK_Option:
      return "Option";
    case kVK_Control:
      return "Control";
    case kVK_RightShift:
      return "Right-Shift";
    case kVK_RightOption:
      return "Right-Option";
    case kVK_RightControl:
      return "Right-Control";
    case kVK_ANSI_KeypadClear:
      return "Clear";

    case kVK_F1:
      return "F1";
    case kVK_F2:
      return "F2";
    case kVK_F3:
      return "F3";
    case kVK_F4:
      return "F4";
    case kVK_F5:
      return "F5";
    case kVK_F6:
      return "F6";
    case kVK_F7:
      return "F7";
    case kVK_F8:
      return "F8";
    case kVK_F9:
      return "F9";
    case kVK_F10:
      return "F10";
    case kVK_F11:
      return "F11";
    case kVK_F12:
      return "F12";
    case kVK_F13:
      return "F13/PrintScreen";
    case kVK_F14:
      return "F14/ScrollLock";
    case kVK_F15:
      return "F15/Pause";

    case kVK_ANSI_Keypad0:
      return "NumPad-0";
    case kVK_ANSI_Keypad1:
      return "NumPad-1";
    case kVK_ANSI_Keypad2:
      return "NumPad-2";
    case kVK_ANSI_Keypad3:
      return "NumPad-3";
    case kVK_ANSI_Keypad4:
      return "NumPad-4";
    case kVK_ANSI_Keypad5:
      return "NumPad-5";
    case kVK_ANSI_Keypad6:
      return "NumPad-6";
    case kVK_ANSI_Keypad7:
      return "NumPad-7";
    case kVK_ANSI_Keypad8:
      return "NumPad-8";
    case kVK_ANSI_Keypad9:
      return "NumPad-9";

    case kVK_ANSI_KeypadMultiply:
      return "NumPad-*";
    case kVK_ANSI_KeypadPlus:
      return "NumPad-+";
    case kVK_ANSI_KeypadMinus:
      return "NumPad--";
    case kVK_ANSI_KeypadDecimal:
      return "NumPad-.";
    case kVK_ANSI_KeypadDivide:
      return "NumPad-/";
    case kVK_ANSI_KeypadEquals:
      return "NumPad-=";
    case kVK_ANSI_KeypadEnter:
      return "NumPad-Enter";
    case kVK_Return:
      return "Return";
    case kVK_Powerbook_KeypadEnter:
      return "NumPad-EnterOnPowerBook";

    case kVK_PC_Insert:
      return "Insert/Help";
    case kVK_PC_Delete:
      return "Delete";
    case kVK_Tab:
      return "Tab";
    case kVK_PC_Backspace:
      return "Backspace";
    case kVK_Home:
      return "Home";
    case kVK_End:
      return "End";
    case kVK_PageUp:
      return "PageUp";
    case kVK_PageDown:
      return "PageDown";
    case kVK_LeftArrow:
      return "LeftArrow";
    case kVK_RightArrow:
      return "RightArrow";
    case kVK_UpArrow:
      return "UpArrow";
    case kVK_DownArrow:
      return "DownArrow";
    case kVK_PC_ContextMenu:
      return "ContextMenu";

    case kVK_Function:
      return "Function";
    case kVK_VolumeUp:
      return "VolumeUp";
    case kVK_VolumeDown:
      return "VolumeDown";
    case kVK_Mute:
      return "Mute";

    case kVK_ISO_Section:
      return "ISO_Section";

    case kVK_JIS_Yen:
      return "JIS_Yen";
    case kVK_JIS_Underscore:
      return "JIS_Underscore";
    case kVK_JIS_KeypadComma:
      return "JIS_KeypadComma";
    case kVK_JIS_Eisu:
      return "JIS_Eisu";
    case kVK_JIS_Kana:
      return "JIS_Kana";

    case kVK_ANSI_A:
      return "A";
    case kVK_ANSI_B:
      return "B";
    case kVK_ANSI_C:
      return "C";
    case kVK_ANSI_D:
      return "D";
    case kVK_ANSI_E:
      return "E";
    case kVK_ANSI_F:
      return "F";
    case kVK_ANSI_G:
      return "G";
    case kVK_ANSI_H:
      return "H";
    case kVK_ANSI_I:
      return "I";
    case kVK_ANSI_J:
      return "J";
    case kVK_ANSI_K:
      return "K";
    case kVK_ANSI_L:
      return "L";
    case kVK_ANSI_M:
      return "M";
    case kVK_ANSI_N:
      return "N";
    case kVK_ANSI_O:
      return "O";
    case kVK_ANSI_P:
      return "P";
    case kVK_ANSI_Q:
      return "Q";
    case kVK_ANSI_R:
      return "R";
    case kVK_ANSI_S:
      return "S";
    case kVK_ANSI_T:
      return "T";
    case kVK_ANSI_U:
      return "U";
    case kVK_ANSI_V:
      return "V";
    case kVK_ANSI_W:
      return "W";
    case kVK_ANSI_X:
      return "X";
    case kVK_ANSI_Y:
      return "Y";
    case kVK_ANSI_Z:
      return "Z";

    case kVK_ANSI_1:
      return "1";
    case kVK_ANSI_2:
      return "2";
    case kVK_ANSI_3:
      return "3";
    case kVK_ANSI_4:
      return "4";
    case kVK_ANSI_5:
      return "5";
    case kVK_ANSI_6:
      return "6";
    case kVK_ANSI_7:
      return "7";
    case kVK_ANSI_8:
      return "8";
    case kVK_ANSI_9:
      return "9";
    case kVK_ANSI_0:
      return "0";
    case kVK_ANSI_Equal:
      return "Equal";
    case kVK_ANSI_Minus:
      return "Minus";
    case kVK_ANSI_RightBracket:
      return "RightBracket";
    case kVK_ANSI_LeftBracket:
      return "LeftBracket";
    case kVK_ANSI_Quote:
      return "Quote";
    case kVK_ANSI_Semicolon:
      return "Semicolon";
    case kVK_ANSI_Backslash:
      return "Backslash";
    case kVK_ANSI_Comma:
      return "Comma";
    case kVK_ANSI_Slash:
      return "Slash";
    case kVK_ANSI_Period:
      return "Period";
    case kVK_ANSI_Grave:
      return "Grave";

    default:
      return "undefined";
  }
}

// Escapes a UTF-16 string for logging. Each character is written as itself
// followed by its value, except that a control character, and a surrogate with
// no pair, are written as their value alone.
static nsCString GetCharacters(const nsAString& aString) {
  nsAutoString escapedStr;
  for (uint32_t i = 0; i < aString.Length(); i++) {
    const char16_t ch = aString.CharAt(i);
    if (ch < 0x20) {
      escapedStr.AppendPrintf("(U+%04X)", ch);
      continue;
    }
    if (ch <= 0x7E) {
      escapedStr.Append(ch);
      continue;
    }
    if (i + 1 < aString.Length() &&
        IsSurrogatePair(ch, aString.CharAt(i + 1))) {
      const char16_t low = aString.CharAt(i + 1);
      escapedStr.Append(ch);
      escapedStr.Append(low);
      escapedStr.AppendPrintf("(U+%04X)",
                              static_cast<uint32_t>(SurrogateToUCS4(ch, low)));
      i++;
      continue;
    }
    if (!IsSurrogate(ch)) {
      escapedStr.Append(ch);
    }
    escapedStr.AppendPrintf("(U+%04X)", ch);
  }

  return nsCString(NS_ConvertUTF16toUTF8(escapedStr));
}

static nsCString GetCharacters(const NSString* aString) {
  nsAutoString str;
  CopyNSStringToXPCOMString(aString, str);
  return GetCharacters(str);
}

static nsCString GetCharacters(const CFStringRef aString) {
  const NSString* str = reinterpret_cast<const NSString*>(aString);
  return GetCharacters(str);
}

static const char* GetNativeKeyEventType(NSEvent* aNativeEvent) {
  switch ([aNativeEvent type]) {
    case NSEventTypeKeyDown:
      return "NSEventTypeKeyDown";
    case NSEventTypeKeyUp:
      return "NSEventTypeKeyUp";
    case NSEventTypeFlagsChanged:
      return "NSEventTypeFlagsChanged";
    default:
      return "not key event";
  }
}

static const char* GetGeckoKeyEventType(const WidgetEvent& aEvent) {
  switch (aEvent.mMessage) {
    case eKeyDown:
      return "eKeyDown";
    case eKeyUp:
      return "eKeyUp";
    case eKeyPress:
      return "eKeyPress";
    default:
      return "not key event";
  }
}

static const char* GetWindowLevelName(NSInteger aWindowLevel) {
  switch (aWindowLevel) {
    case kCGBaseWindowLevelKey:
      return "kCGBaseWindowLevelKey (NSNormalWindowLevel)";
    case kCGMinimumWindowLevelKey:
      return "kCGMinimumWindowLevelKey";
    case kCGDesktopWindowLevelKey:
      return "kCGDesktopWindowLevelKey";
    case kCGBackstopMenuLevelKey:
      return "kCGBackstopMenuLevelKey";
    case kCGNormalWindowLevelKey:
      return "kCGNormalWindowLevelKey";
    case kCGFloatingWindowLevelKey:
      return "kCGFloatingWindowLevelKey (NSFloatingWindowLevel)";
    case kCGTornOffMenuWindowLevelKey:
      return "kCGTornOffMenuWindowLevelKey (NSSubmenuWindowLevel, "
             "NSTornOffMenuWindowLevel)";
    case kCGDockWindowLevelKey:
      return "kCGDockWindowLevelKey (NSDockWindowLevel)";
    case kCGMainMenuWindowLevelKey:
      return "kCGMainMenuWindowLevelKey (NSMainMenuWindowLevel)";
    case kCGStatusWindowLevelKey:
      return "kCGStatusWindowLevelKey (NSStatusWindowLevel)";
    case kCGModalPanelWindowLevelKey:
      return "kCGModalPanelWindowLevelKey (NSModalPanelWindowLevel)";
    case kCGPopUpMenuWindowLevelKey:
      return "kCGPopUpMenuWindowLevelKey (NSPopUpMenuWindowLevel)";
    case kCGDraggingWindowLevelKey:
      return "kCGDraggingWindowLevelKey";
    case kCGScreenSaverWindowLevelKey:
      return "kCGScreenSaverWindowLevelKey (NSScreenSaverWindowLevel)";
    case kCGMaximumWindowLevelKey:
      return "kCGMaximumWindowLevelKey";
    case kCGOverlayWindowLevelKey:
      return "kCGOverlayWindowLevelKey";
    case kCGHelpWindowLevelKey:
      return "kCGHelpWindowLevelKey";
    case kCGUtilityWindowLevelKey:
      return "kCGUtilityWindowLevelKey";
    case kCGDesktopIconWindowLevelKey:
      return "kCGDesktopIconWindowLevelKey";
    case kCGCursorWindowLevelKey:
      return "kCGCursorWindowLevelKey";
    case kCGNumberOfWindowLevelKeys:
      return "kCGNumberOfWindowLevelKeys";
    default:
      return "unknown window level";
  }
}

bool mozilla::widget::IsControlChar(uint32_t aCharCode) {
  return aCharCode < ' ' || aCharCode == NSDeleteCharacter;
}

inline auto format_as(const NSRange& aRange) {
  return fmt::format("{{ location={}, length={} }}",
                     aRange.location == NSNotFound
                         ? "NSNotFound"
                         : std::to_string(aRange.location),
                     aRange.length);
}

inline std::ostream& operator<<(std::ostream& aStream, const NSRange& aRange) {
  return aStream << format_as(aRange);
}

inline auto format_as(const NSPoint& aPoint) {
  return fmt::format("{{ x={}, y={} }}", aPoint.x, aPoint.y);
}

inline std::ostream& operator<<(std::ostream& aStream, const NSPoint& aPoint) {
  return aStream << format_as(aPoint);
}

inline auto format_as(const NSSize& aSize) {
  return fmt::format("{{ width={}, height={} }}", aSize.width, aSize.height);
}

inline std::ostream& operator<<(std::ostream& aStream, const NSSize& aSize) {
  return aStream << format_as(aSize);
}

inline auto format_as(const NSRect& aRect) {
  return fmt::format("{{ origin={}, size={} }}", aRect.origin, aRect.size);
}

inline std::ostream& operator<<(std::ostream& aStream, const NSRect& aRect) {
  return aStream << format_as(aRect);
}

static uint32_t gHandlerInstanceCount = 0;

static void EnsureToLogAllKeyboardLayoutsAndIMEs() {
  static bool sDone = false;
  if (!sDone) {
    sDone = true;
    TextInputHandler::DebugPrintAllKeyboardLayouts();
    IMEInputHandler::DebugPrintAllIMEModes();
  }
}

inline NSRange MakeNSRangeFrom(
    const Maybe<OffsetAndData<uint32_t>>& aOffsetAndData) {
  if (aOffsetAndData.isNothing()) {
    return NSMakeRange(NSNotFound, 0);
  }
  return NSMakeRange(aOffsetAndData->StartOffset(), aOffsetAndData->Length());
}

#pragma mark -

/******************************************************************************
 *
 *  TISInputSourceWrapper implementation
 *
 ******************************************************************************/

TISInputSourceWrapper* TISInputSourceWrapper::sCurrentInputSource = nullptr;

// static
TISInputSourceWrapper& TISInputSourceWrapper::CurrentInputSource() {
  if (!sCurrentInputSource) {
    sCurrentInputSource = new TISInputSourceWrapper();
  }
  if (!sCurrentInputSource->IsInitializedByCurrentInputSource()) {
    sCurrentInputSource->InitByCurrentInputSource();
  }
  return *sCurrentInputSource;
}

// static
void TISInputSourceWrapper::Shutdown() {
  if (!sCurrentInputSource) {
    return;
  }
  sCurrentInputSource->Clear();
  delete sCurrentInputSource;
  sCurrentInputSource = nullptr;
}

bool TISInputSourceWrapper::TranslateToString(UInt32 aKeyCode,
                                              UInt32 aModifiers, UInt32 aKbType,
                                              nsAString& aStr) {
  aStr.Truncate();

  const UCKeyboardLayout* UCKey = GetUCKeyboardLayout();

  MOZ_LOG(gKeyLog, LogLevel::Info,
          ("%p TISInputSourceWrapper::TranslateToString, aKeyCode=0x%X, "
           "aModifiers=0x%X, aKbType=0x%X UCKey=%p\n    "
           "Shift: %s, Ctrl: %s, Opt: %s, Cmd: %s, CapsLock: %s, NumLock: %s",
           this, static_cast<unsigned int>(aKeyCode),
           static_cast<unsigned int>(aModifiers),
           static_cast<unsigned int>(aKbType), UCKey,
           OnOrOff(aModifiers & shiftKey), OnOrOff(aModifiers & controlKey),
           OnOrOff(aModifiers & optionKey), OnOrOff(aModifiers & cmdKey),
           OnOrOff(aModifiers & alphaLock),
           OnOrOff(aModifiers & kEventKeyModifierNumLockMask)));

  NS_ENSURE_TRUE(UCKey, false);

  UInt32 deadKeyState = 0;
  UniCharCount len;
  UniChar chars[5];
  OSStatus err = ::UCKeyTranslate(
      UCKey, aKeyCode, kUCKeyActionDown, aModifiers >> 8, aKbType,
      kUCKeyTranslateNoDeadKeysMask, &deadKeyState, 5, &len, chars);

  MOZ_LOG(gKeyLog, LogLevel::Info,
          ("%p   TISInputSourceWrapper::TranslateToString, err=0x%X, len=%zu",
           this, static_cast<int>(err), len));

  NS_ENSURE_TRUE(err == noErr, false);
  if (len == 0) {
    return true;
  }
  if (!aStr.SetLength(len, fallible)) {
    return false;
  }
  NS_ASSERTION(sizeof(char16_t) == sizeof(UniChar),
               "size of char16_t and size of UniChar are different");
  memcpy(aStr.BeginWriting(), chars, len * sizeof(char16_t));

  MOZ_LOG(gKeyLog, LogLevel::Info,
          ("%p   TISInputSourceWrapper::TranslateToString, aStr=\"%s\"", this,
           NS_ConvertUTF16toUTF8(aStr).get()));

  return true;
}

uint32_t TISInputSourceWrapper::TranslateToChar(UInt32 aKeyCode,
                                                UInt32 aModifiers,
                                                UInt32 aKbType) {
  nsAutoString str;
  if (!TranslateToString(aKeyCode, aModifiers, aKbType, str) ||
      str.Length() != 1) {
    return 0;
  }
  return static_cast<uint32_t>(str.CharAt(0));
}

bool TISInputSourceWrapper::IsDeadKey(NSEvent* aNativeKeyEvent) {
  if ([[aNativeKeyEvent characters] length]) {
    return false;
  }

  // Assume that if the control key, command key or Fn key is pressed, it's not
  // a dead key.
  NSUInteger cocoaState = [aNativeKeyEvent modifierFlags];
  if (cocoaState & (NSEventModifierFlagControl | NSEventModifierFlagCommand |
                    NSEventModifierFlagFunction)) {
    return false;
  }

  UInt32 nativeKeyCode = [aNativeKeyEvent keyCode];
  switch (nativeKeyCode) {
    case kVK_ANSI_A:
    case kVK_ANSI_B:
    case kVK_ANSI_C:
    case kVK_ANSI_D:
    case kVK_ANSI_E:
    case kVK_ANSI_F:
    case kVK_ANSI_G:
    case kVK_ANSI_H:
    case kVK_ANSI_I:
    case kVK_ANSI_J:
    case kVK_ANSI_K:
    case kVK_ANSI_L:
    case kVK_ANSI_M:
    case kVK_ANSI_N:
    case kVK_ANSI_O:
    case kVK_ANSI_P:
    case kVK_ANSI_Q:
    case kVK_ANSI_R:
    case kVK_ANSI_S:
    case kVK_ANSI_T:
    case kVK_ANSI_U:
    case kVK_ANSI_V:
    case kVK_ANSI_W:
    case kVK_ANSI_X:
    case kVK_ANSI_Y:
    case kVK_ANSI_Z:
    case kVK_ANSI_1:
    case kVK_ANSI_2:
    case kVK_ANSI_3:
    case kVK_ANSI_4:
    case kVK_ANSI_5:
    case kVK_ANSI_6:
    case kVK_ANSI_7:
    case kVK_ANSI_8:
    case kVK_ANSI_9:
    case kVK_ANSI_0:
    case kVK_ANSI_Equal:
    case kVK_ANSI_Minus:
    case kVK_ANSI_RightBracket:
    case kVK_ANSI_LeftBracket:
    case kVK_ANSI_Quote:
    case kVK_ANSI_Semicolon:
    case kVK_ANSI_Backslash:
    case kVK_ANSI_Comma:
    case kVK_ANSI_Slash:
    case kVK_ANSI_Period:
    case kVK_ANSI_Grave:
    case kVK_JIS_Yen:
    case kVK_JIS_Underscore:
      break;
    default:
      // Let's assume that dead key can be only a printable key in standard
      // position.
      return false;
  }

  // If TranslateToChar() returns non-zero value, that means that
  // the key may input a character with different dead key state.
  UInt32 kbType = GetKbdType();
  UInt32 carbonState = nsCocoaUtils::ConvertToCarbonModifier(cocoaState);
  return IsDeadKey(nativeKeyCode, carbonState, kbType);
}

bool TISInputSourceWrapper::IsDeadKey(UInt32 aKeyCode, UInt32 aModifiers,
                                      UInt32 aKbType) {
  const UCKeyboardLayout* UCKey = GetUCKeyboardLayout();

  MOZ_LOG(gKeyLog, LogLevel::Info,
          ("%p TISInputSourceWrapper::IsDeadKey, aKeyCode=0x%X, "
           "aModifiers=0x%X, aKbType=0x%X UCKey=%p\n    "
           "Shift: %s, Ctrl: %s, Opt: %s, Cmd: %s, CapsLock: %s, NumLock: %s",
           this, static_cast<unsigned int>(aKeyCode),
           static_cast<unsigned int>(aModifiers),
           static_cast<unsigned int>(aKbType), UCKey,
           OnOrOff(aModifiers & shiftKey), OnOrOff(aModifiers & controlKey),
           OnOrOff(aModifiers & optionKey), OnOrOff(aModifiers & cmdKey),
           OnOrOff(aModifiers & alphaLock),
           OnOrOff(aModifiers & kEventKeyModifierNumLockMask)));

  if (NS_WARN_IF(!UCKey)) {
    return false;
  }

  UInt32 deadKeyState = 0;
  UniCharCount len;
  UniChar chars[5];
  OSStatus err =
      ::UCKeyTranslate(UCKey, aKeyCode, kUCKeyActionDown, aModifiers >> 8,
                       aKbType, 0, &deadKeyState, 5, &len, chars);

  MOZ_LOG(gKeyLog, LogLevel::Info,
          ("%p   TISInputSourceWrapper::IsDeadKey, err=0x%X, "
           "len=%zu, deadKeyState=%u",
           this, static_cast<int>(err), len, deadKeyState));

  if (NS_WARN_IF(err != noErr)) {
    return false;
  }

  return deadKeyState != 0;
}

void TISInputSourceWrapper::InitByInputSourceID(const char* aID) {
  Clear();
  if (!aID) return;

  CFStringRef idstr = ::CFStringCreateWithCString(kCFAllocatorDefault, aID,
                                                  kCFStringEncodingASCII);
  InitByInputSourceID(idstr);
  ::CFRelease(idstr);
}

void TISInputSourceWrapper::InitByInputSourceID(const nsString& aID) {
  Clear();
  if (aID.IsEmpty()) return;
  CFStringRef idstr = ::CFStringCreateWithCharacters(
      kCFAllocatorDefault, reinterpret_cast<const UniChar*>(aID.get()),
      aID.Length());
  InitByInputSourceID(idstr);
  ::CFRelease(idstr);
}

void TISInputSourceWrapper::InitByInputSourceID(const CFStringRef aID) {
  Clear();
  if (!aID) return;
  const void* keys[] = {kTISPropertyInputSourceID};
  const void* values[] = {aID};
  CFDictionaryRef filter = ::CFDictionaryCreate(kCFAllocatorDefault, keys,
                                                values, 1, nullptr, nullptr);
  NS_ASSERTION(filter, "failed to create the filter");
  mInputSourceList = ::TISCreateInputSourceList(filter, true);
  ::CFRelease(filter);
  if (::CFArrayGetCount(mInputSourceList) > 0) {
    mInputSource = static_cast<TISInputSourceRef>(
        const_cast<void*>(::CFArrayGetValueAtIndex(mInputSourceList, 0)));
    if (IsKeyboardLayout()) {
      mKeyboardLayout = mInputSource;
    }
  }
}

void TISInputSourceWrapper::InitByLayoutID(SInt32 aLayoutID,
                                           bool aOverrideKeyboard) {
  // NOTE: Document new layout IDs in TextInputHandler.h when you add ones.
  switch (aLayoutID) {
    case 0:
      InitByInputSourceID("com.apple.keylayout.US");
      break;
    case 1:
      InitByInputSourceID("com.apple.keylayout.Greek");
      break;
    case 2:
      InitByInputSourceID("com.apple.keylayout.German");
      break;
    case 3:
      InitByInputSourceID("com.apple.keylayout.Swedish-Pro");
      break;
    case 4:
      InitByInputSourceID("com.apple.keylayout.DVORAK-QWERTYCMD");
      break;
    case 5:
      InitByInputSourceID("com.apple.keylayout.Thai");
      break;
    case 6:
      InitByInputSourceID("com.apple.keylayout.Arabic");
      break;
    case 7:
      InitByInputSourceID("com.apple.keylayout.ArabicPC");
      break;
    case 8:
      InitByInputSourceID("com.apple.keylayout.French");
      break;
    case 9:
      InitByInputSourceID("com.apple.keylayout.Hebrew");
      break;
    case 10:
      InitByInputSourceID("com.apple.keylayout.Lithuanian");
      break;
    case 11:
      InitByInputSourceID("com.apple.keylayout.Norwegian");
      break;
    case 12:
      InitByInputSourceID("com.apple.keylayout.Spanish");
      break;
    case 13:
      InitByInputSourceID("com.apple.keylayout.French-PC");
      break;
    default:
      Clear();
      break;
  }
  mOverrideKeyboard = aOverrideKeyboard;
}

void TISInputSourceWrapper::InitByCurrentInputSource() {
  Clear();
  mInputSource = ::TISCopyCurrentKeyboardInputSource();
  mKeyboardLayout = ::TISCopyInputMethodKeyboardLayoutOverride();
  if (!mKeyboardLayout) {
    mKeyboardLayout = ::TISCopyCurrentKeyboardLayoutInputSource();
  }
  // If this causes composition, the current keyboard layout may input non-ASCII
  // characters such as Japanese Kana characters or Hangul characters.
  // However, we need to set ASCII characters to DOM key events for consistency
  // with other platforms.
  if (IsOpenedIMEMode()) {
    TISInputSourceWrapper tis(mKeyboardLayout);
    if (!tis.IsASCIICapable()) {
      mKeyboardLayout = ::TISCopyCurrentASCIICapableKeyboardLayoutInputSource();
    }
  }
}

void TISInputSourceWrapper::InitByCurrentKeyboardLayout() {
  Clear();
  mInputSource = ::TISCopyCurrentKeyboardLayoutInputSource();
  mKeyboardLayout = mInputSource;
}

void TISInputSourceWrapper::InitByCurrentASCIICapableInputSource() {
  Clear();
  mInputSource = ::TISCopyCurrentASCIICapableKeyboardInputSource();
  mKeyboardLayout = ::TISCopyInputMethodKeyboardLayoutOverride();
  if (mKeyboardLayout) {
    TISInputSourceWrapper tis(mKeyboardLayout);
    if (!tis.IsASCIICapable()) {
      mKeyboardLayout = nullptr;
    }
  }
  if (!mKeyboardLayout) {
    mKeyboardLayout = ::TISCopyCurrentASCIICapableKeyboardLayoutInputSource();
  }
}

void TISInputSourceWrapper::InitByCurrentASCIICapableKeyboardLayout() {
  Clear();
  mInputSource = ::TISCopyCurrentASCIICapableKeyboardLayoutInputSource();
  mKeyboardLayout = mInputSource;
}

void TISInputSourceWrapper::InitByCurrentInputMethodKeyboardLayoutOverride() {
  Clear();
  mInputSource = ::TISCopyInputMethodKeyboardLayoutOverride();
  mKeyboardLayout = mInputSource;
}

void TISInputSourceWrapper::InitByTISInputSourceRef(
    TISInputSourceRef aInputSource) {
  Clear();
  mInputSource = aInputSource;
  if (IsKeyboardLayout()) {
    mKeyboardLayout = mInputSource;
  }
}

void TISInputSourceWrapper::InitByLanguage(CFStringRef aLanguage) {
  Clear();
  mInputSource = ::TISCopyInputSourceForLanguage(aLanguage);
  if (IsKeyboardLayout()) {
    mKeyboardLayout = mInputSource;
  }
}

const UCKeyboardLayout* TISInputSourceWrapper::GetUCKeyboardLayout() {
  NS_ENSURE_TRUE(mKeyboardLayout, nullptr);
  if (mUCKeyboardLayout) {
    return mUCKeyboardLayout;
  }
  CFDataRef uchr = static_cast<CFDataRef>(::TISGetInputSourceProperty(
      mKeyboardLayout, kTISPropertyUnicodeKeyLayoutData));

  // We should be always able to get the layout here.
  NS_ENSURE_TRUE(uchr, nullptr);
  mUCKeyboardLayout =
      reinterpret_cast<const UCKeyboardLayout*>(CFDataGetBytePtr(uchr));
  return mUCKeyboardLayout;
}

bool TISInputSourceWrapper::GetBoolProperty(const CFStringRef aKey) {
  CFBooleanRef ret = static_cast<CFBooleanRef>(
      ::TISGetInputSourceProperty(mInputSource, aKey));
  return ::CFBooleanGetValue(ret);
}

bool TISInputSourceWrapper::GetStringProperty(const CFStringRef aKey,
                                              CFStringRef& aStr) {
  aStr =
      static_cast<CFStringRef>(::TISGetInputSourceProperty(mInputSource, aKey));
  return aStr != nullptr;
}

bool TISInputSourceWrapper::GetStringProperty(const CFStringRef aKey,
                                              nsAString& aStr) {
  CFStringRef str;
  GetStringProperty(aKey, str);
  CopyNSStringToXPCOMString((const NSString*)str, aStr);
  return !aStr.IsEmpty();
}

bool TISInputSourceWrapper::IsOpenedIMEMode() {
  NS_ENSURE_TRUE(mInputSource, false);
  if (!IsIMEMode()) return false;
  return !IsASCIICapable();
}

bool TISInputSourceWrapper::IsIMEMode() {
  NS_ENSURE_TRUE(mInputSource, false);
  CFStringRef str;
  GetInputSourceType(str);
  NS_ENSURE_TRUE(str, false);
  return ::CFStringCompare(kTISTypeKeyboardInputMode, str, 0) ==
         kCFCompareEqualTo;
}

bool TISInputSourceWrapper::IsKeyboardLayout() {
  NS_ENSURE_TRUE(mInputSource, false);
  CFStringRef str;
  GetInputSourceType(str);
  NS_ENSURE_TRUE(str, false);
  return ::CFStringCompare(kTISTypeKeyboardLayout, str, 0) == kCFCompareEqualTo;
}

bool TISInputSourceWrapper::GetLanguageList(CFArrayRef& aLanguageList) {
  NS_ENSURE_TRUE(mInputSource, false);
  aLanguageList = static_cast<CFArrayRef>(::TISGetInputSourceProperty(
      mInputSource, kTISPropertyInputSourceLanguages));
  return aLanguageList != nullptr;
}

bool TISInputSourceWrapper::GetPrimaryLanguage(CFStringRef& aPrimaryLanguage) {
  NS_ENSURE_TRUE(mInputSource, false);
  CFArrayRef langList;
  NS_ENSURE_TRUE(GetLanguageList(langList), false);
  if (::CFArrayGetCount(langList) == 0) return false;
  aPrimaryLanguage =
      static_cast<CFStringRef>(::CFArrayGetValueAtIndex(langList, 0));
  return aPrimaryLanguage != nullptr;
}

bool TISInputSourceWrapper::GetPrimaryLanguage(nsAString& aPrimaryLanguage) {
  NS_ENSURE_TRUE(mInputSource, false);
  CFStringRef primaryLanguage;
  NS_ENSURE_TRUE(GetPrimaryLanguage(primaryLanguage), false);
  CopyNSStringToXPCOMString((const NSString*)primaryLanguage, aPrimaryLanguage);
  return !aPrimaryLanguage.IsEmpty();
}

bool TISInputSourceWrapper::IsForRTLLanguage() {
  if (mIsRTL < 0) {
    // Get the input character of the 'A' key of ANSI keyboard layout.
    nsAutoString str;
    bool ret = TranslateToString(kVK_ANSI_A, 0, eKbdType_ANSI, str);
    NS_ENSURE_TRUE(ret, ret);
    char16_t ch = str.IsEmpty() ? char16_t(0) : str.CharAt(0);
    mIsRTL = UTF16_CODE_UNIT_IS_BIDI(ch);
  }
  return mIsRTL != 0;
}

bool TISInputSourceWrapper::IsForJapaneseLanguage() {
  nsAutoString lang;
  GetPrimaryLanguage(lang);
  return lang.EqualsLiteral("ja");
}

bool TISInputSourceWrapper::IsInitializedByCurrentInputSource() {
  return mInputSource == ::TISCopyCurrentKeyboardInputSource();
}

void TISInputSourceWrapper::Select() {
  if (!mInputSource) return;
  ::TISSelectInputSource(mInputSource);
}

void TISInputSourceWrapper::Clear() {
  // Clear() is always called when TISInputSourceWrapper is created.
  EnsureToLogAllKeyboardLayoutsAndIMEs();

  if (mInputSourceList) {
    ::CFRelease(mInputSourceList);
  }
  mInputSourceList = nullptr;
  mInputSource = nullptr;
  mKeyboardLayout = nullptr;
  mIsRTL = -1;
  mUCKeyboardLayout = nullptr;
  mOverrideKeyboard = false;
}

bool TISInputSourceWrapper::IsPrintableKeyEvent(
    NSEvent* aNativeKeyEvent) const {
  UInt32 nativeKeyCode = [aNativeKeyEvent keyCode];

  bool isPrintableKey = !TextInputHandler::IsSpecialGeckoKey(nativeKeyCode);
  if (isPrintableKey && [aNativeKeyEvent type] != NSEventTypeKeyDown &&
      [aNativeKeyEvent type] != NSEventTypeKeyUp) {
    NS_WARNING("Why would a printable key not be an NSEventTypeKeyDown or "
               "NSEventTypeKeyUp event?");
    isPrintableKey = false;
  }
  return isPrintableKey;
}

UInt32 TISInputSourceWrapper::GetKbdType() const {
  // If a keyboard layout override is set, we also need to force the keyboard
  // type to something ANSI to avoid test failures on machines with JIS
  // keyboards (since the pair of keyboard layout and physical keyboard type
  // form the actual key layout).  This assumes that the test setting the
  // override was written assuming an ANSI keyboard.
  return mOverrideKeyboard ? eKbdType_ANSI : ::LMGetKbdType();
}

void TISInputSourceWrapper::ComputeInsertStringForCharCode(
    NSEvent* aNativeKeyEvent, const WidgetKeyboardEvent& aKeyEvent,
    const nsAString* aInsertString, nsAString& aResult) {
  if (aInsertString) {
    // If the caller expects that the aInsertString will be input, we shouldn't
    // change it.
    aResult = *aInsertString;
  } else if (IsPrintableKeyEvent(aNativeKeyEvent)) {
    // If IME is open, [aNativeKeyEvent characters] may be a character
    // which will be appended to the composition string.  However, especially,
    // while IME is disabled, most users and developers expect the key event
    // works as IME closed.  So, we should compute the aResult with
    // the ASCII capable keyboard layout.
    // NOTE: Such keyboard layouts typically change the layout to its ASCII
    //       capable layout when Command key is pressed.  And we don't worry
    //       when Control key is pressed too because it causes inputting
    //       control characters.
    // Additionally, if the key event doesn't input any text, the event may be
    // dead key event.  In this case, the charCode value should be the dead
    // character.
    UInt32 nativeKeyCode = [aNativeKeyEvent keyCode];
    if ((!aKeyEvent.IsMeta() && !aKeyEvent.IsControl() && IsOpenedIMEMode()) ||
        ![[aNativeKeyEvent characters] length]) {
      UInt32 state = nsCocoaUtils::ConvertToCarbonModifier(
          [aNativeKeyEvent modifierFlags]);
      uint32_t ch = TranslateToChar(nativeKeyCode, state, GetKbdType());
      if (ch) {
        aResult = ch;
      }
    } else {
      // If the caller isn't sure what string will be input, let's use
      // characters of NSEvent.
      CopyNSStringToXPCOMString([aNativeKeyEvent characters], aResult);
    }

    // If control key is pressed and the eventChars is a non-printable control
    // character, we should convert it to ASCII alphabet.
    if (aKeyEvent.IsControl() && !aResult.IsEmpty() &&
        aResult[0] <= char16_t(26)) {
      aResult = (aKeyEvent.IsShift() ^ aKeyEvent.IsCapsLocked())
                    ? static_cast<char16_t>(aResult[0] + ('A' - 1))
                    : static_cast<char16_t>(aResult[0] + ('a' - 1));
    }
    // If Meta key is pressed, it may cause switching the keyboard layout like
    // Arabic, Russian, Hebrew, Greek and Dvorak-QWERTY.
    else if (aKeyEvent.IsMeta() &&
             !(aKeyEvent.IsControl() || aKeyEvent.IsAlt())) {
      UInt32 kbType = GetKbdType();
      UInt32 numLockState =
          aKeyEvent.IsNumLocked() ? kEventKeyModifierNumLockMask : 0;
      UInt32 capsLockState = aKeyEvent.IsCapsLocked() ? alphaLock : 0;
      UInt32 shiftState = aKeyEvent.IsShift() ? shiftKey : 0;
      uint32_t uncmdedChar =
          TranslateToChar(nativeKeyCode, numLockState, kbType);
      uint32_t cmdedChar =
          TranslateToChar(nativeKeyCode, cmdKey | numLockState, kbType);
      // If we can make a good guess at the characters that the user would
      // expect this key combination to produce (with and without Shift) then
      // use those characters.  This also corrects for CapsLock.
      uint32_t ch = 0;
      if (uncmdedChar == cmdedChar) {
        // The characters produced with Command seem similar to those without
        // Command.
        ch = TranslateToChar(nativeKeyCode,
                             shiftState | capsLockState | numLockState, kbType);
      } else {
        TISInputSourceWrapper USLayout("com.apple.keylayout.US");
        uint32_t uncmdedUSChar =
            USLayout.TranslateToChar(nativeKeyCode, numLockState, kbType);
        // If it looks like characters from US keyboard layout when Command key
        // is pressed, we should compute a character in the layout.
        if (uncmdedUSChar == cmdedChar) {
          ch = USLayout.TranslateToChar(
              nativeKeyCode, shiftState | capsLockState | numLockState, kbType);
        }
      }

      // If there is a more preferred character for the commanded key event,
      // we should use it.
      if (ch) {
        aResult = ch;
      }
    }
  }

  // Remove control characters which shouldn't be inputted on editor.
  // XXX Currently, we don't find any cases inserting control characters with
  //     printable character.  So, just checking first character is enough.
  if (!aResult.IsEmpty() && IsControlChar(aResult[0])) {
    aResult.Truncate();
  }
}

void TISInputSourceWrapper::InitKeyEvent(NSEvent* aNativeKeyEvent,
                                         WidgetKeyboardEvent& aKeyEvent,
                                         bool aIsProcessedByIME,
                                         const nsAString* aInsertString) {
  NS_OBJC_BEGIN_TRY_IGNORE_BLOCK;

  MOZ_ASSERT(!aIsProcessedByIME || aKeyEvent.mMessage != eKeyPress,
             "eKeyPress event should not be marked as processed by IME");

  MOZ_LOG(gKeyLog, LogLevel::Info,
          ("%p TISInputSourceWrapper::InitKeyEvent, aNativeKeyEvent=%p, "
           "aKeyEvent.mMessage=%s, aProcessedByIME=%s, aInsertString=%p, "
           "IsOpenedIMEMode()=%s",
           this, aNativeKeyEvent, GetGeckoKeyEventType(aKeyEvent),
           TrueOrFalse(aIsProcessedByIME), aInsertString,
           TrueOrFalse(IsOpenedIMEMode())));

  if (NS_WARN_IF(!aNativeKeyEvent)) {
    return;
  }

  nsCocoaUtils::InitInputEvent(aKeyEvent, aNativeKeyEvent);

  // This is used only while dispatching the event (which is a synchronous
  // call), so there is no need to retain and release this data.
  aKeyEvent.mNativeKeyEvent = aNativeKeyEvent;

  aKeyEvent.mRefPoint = LayoutDeviceIntPoint(0, 0);

  UInt32 kbType = GetKbdType();
  UInt32 nativeKeyCode = [aNativeKeyEvent keyCode];

  // macOS handles dead key as IME.  If the key is first key press of dead
  // key, we should use KEY_NAME_INDEX_Dead for first (dead) key event.
  // So, if aIsProcessedByIME is true, it may be dead key.  Let's check
  // if current key event is a dead key's keydown event.
  bool isProcessedByIME =
      aIsProcessedByIME &&
      !TISInputSourceWrapper::CurrentInputSource().IsDeadKey(aNativeKeyEvent);

  aKeyEvent.mKeyCode =
      isProcessedByIME
          ? NS_VK_PROCESSKEY
          : ComputeGeckoKeyCode(nativeKeyCode, kbType, aKeyEvent.IsMeta());

  switch (nativeKeyCode) {
    case kVK_Command:
    case kVK_Shift:
    case kVK_Option:
    case kVK_Control:
      aKeyEvent.mLocation = eKeyLocationLeft;
      break;

    case kVK_RightCommand:
    case kVK_RightShift:
    case kVK_RightOption:
    case kVK_RightControl:
      aKeyEvent.mLocation = eKeyLocationRight;
      break;

    case kVK_ANSI_Keypad0:
    case kVK_ANSI_Keypad1:
    case kVK_ANSI_Keypad2:
    case kVK_ANSI_Keypad3:
    case kVK_ANSI_Keypad4:
    case kVK_ANSI_Keypad5:
    case kVK_ANSI_Keypad6:
    case kVK_ANSI_Keypad7:
    case kVK_ANSI_Keypad8:
    case kVK_ANSI_Keypad9:
    case kVK_ANSI_KeypadMultiply:
    case kVK_ANSI_KeypadPlus:
    case kVK_ANSI_KeypadMinus:
    case kVK_ANSI_KeypadDecimal:
    case kVK_ANSI_KeypadDivide:
    case kVK_ANSI_KeypadEquals:
    case kVK_ANSI_KeypadEnter:
    case kVK_JIS_KeypadComma:
    case kVK_Powerbook_KeypadEnter:
      aKeyEvent.mLocation = eKeyLocationNumpad;
      break;

    default:
      aKeyEvent.mLocation = eKeyLocationStandard;
      break;
  }

  aKeyEvent.mIsRepeat = ([aNativeKeyEvent type] == NSEventTypeKeyDown)
                            ? [aNativeKeyEvent isARepeat]
                            : false;

  MOZ_LOG(gKeyLog, LogLevel::Info,
          ("%p   TISInputSourceWrapper::InitKeyEvent, "
           "shift=%s, ctrl=%s, alt=%s, meta=%s",
           this, OnOrOff(aKeyEvent.IsShift()), OnOrOff(aKeyEvent.IsControl()),
           OnOrOff(aKeyEvent.IsAlt()), OnOrOff(aKeyEvent.IsMeta())));

  if (isProcessedByIME) {
    aKeyEvent.mKeyNameIndex = KEY_NAME_INDEX_Process;
  } else if (IsPrintableKeyEvent(aNativeKeyEvent)) {
    aKeyEvent.mKeyNameIndex = KEY_NAME_INDEX_USE_STRING;
    // If insertText calls this method, let's use the string.
    if (aInsertString && !aInsertString->IsEmpty() &&
        !IsControlChar((*aInsertString)[0])) {
      aKeyEvent.mKeyValue = *aInsertString;
    }
    // If meta key is pressed, the printable key layout may be switched from
    // non-ASCII capable layout to ASCII capable, or from Dvorak to QWERTY.
    // KeyboardEvent.key value should be the switched layout's character.
    else if (aKeyEvent.IsMeta()) {
      CopyNSStringToXPCOMString([aNativeKeyEvent characters],
                                aKeyEvent.mKeyValue);
    }
    // If control key is pressed, some keys may produce printable character via
    // [aNativeKeyEvent characters].  Otherwise, translate input character of
    // the key without control key.
    else if (aKeyEvent.IsControl()) {
      NSUInteger cocoaState =
          [aNativeKeyEvent modifierFlags] & ~NSEventModifierFlagControl;
      UInt32 carbonState = nsCocoaUtils::ConvertToCarbonModifier(cocoaState);
      if (IsDeadKey(nativeKeyCode, carbonState, kbType)) {
        aKeyEvent.mKeyNameIndex = KEY_NAME_INDEX_Dead;
      } else {
        aKeyEvent.mKeyValue =
            TranslateToChar(nativeKeyCode, carbonState, kbType);
        if (!aKeyEvent.mKeyValue.IsEmpty() &&
            IsControlChar(aKeyEvent.mKeyValue[0])) {
          // Don't expose control character to the web.
          aKeyEvent.mKeyValue.Truncate();
        }
      }
    }
    // Otherwise, KeyboardEvent.key expose
    // [aNativeKeyEvent characters] value.  However, if IME is open and the
    // keyboard layout isn't ASCII capable, exposing the non-ASCII character
    // doesn't match with other platform's behavior.  For the compatibility
    // with other platform's Gecko, we need to set a translated character.
    else if (IsOpenedIMEMode()) {
      UInt32 state = nsCocoaUtils::ConvertToCarbonModifier(
          [aNativeKeyEvent modifierFlags]);
      aKeyEvent.mKeyValue = TranslateToChar(nativeKeyCode, state, kbType);
    } else {
      CopyNSStringToXPCOMString([aNativeKeyEvent characters],
                                aKeyEvent.mKeyValue);
      // If the key value is empty, the event may be a dead key event.
      // If TranslateToChar() returns non-zero value, that means that
      // the key may input a character with different dead key state.
      if (aKeyEvent.mKeyValue.IsEmpty()) {
        NSUInteger cocoaState = [aNativeKeyEvent modifierFlags];
        UInt32 carbonState = nsCocoaUtils::ConvertToCarbonModifier(cocoaState);
        if (TranslateToChar(nativeKeyCode, carbonState, kbType)) {
          aKeyEvent.mKeyNameIndex = KEY_NAME_INDEX_Dead;
        }
      }
    }

    // Last resort.  If .key value becomes empty string, we should use
    // charactersIgnoringModifiers, if it's available.
    if (aKeyEvent.mKeyNameIndex == KEY_NAME_INDEX_USE_STRING &&
        (aKeyEvent.mKeyValue.IsEmpty() ||
         IsControlChar(aKeyEvent.mKeyValue[0]))) {
      CopyNSStringToXPCOMString([aNativeKeyEvent charactersIgnoringModifiers],
                                aKeyEvent.mKeyValue);
      // But don't expose it if it's a control character.
      if (!aKeyEvent.mKeyValue.IsEmpty() &&
          IsControlChar(aKeyEvent.mKeyValue[0])) {
        aKeyEvent.mKeyValue.Truncate();
      }
    }
  } else {
    // Compute the key for non-printable keys and some special printable keys.
    aKeyEvent.mKeyNameIndex = ComputeGeckoKeyNameIndex(nativeKeyCode);
  }

  aKeyEvent.mCodeNameIndex = ComputeGeckoCodeNameIndex(nativeKeyCode, kbType);
  MOZ_ASSERT(aKeyEvent.mCodeNameIndex != CODE_NAME_INDEX_USE_STRING);

  NS_OBJC_END_TRY_IGNORE_BLOCK
}

void TISInputSourceWrapper::WillDispatchKeyboardEvent(
    NSEvent* aNativeKeyEvent, const nsAString* aInsertString,
    uint32_t aIndexOfKeypress, WidgetKeyboardEvent& aKeyEvent) {
  NS_OBJC_BEGIN_TRY_IGNORE_BLOCK;

  // Nothing to do here if the native key event is neither NSEventTypeKeyDown
  // nor NSEventTypeKeyUp because accessing [aNativeKeyEvent characters] causes
  // throwing an exception.
  if ([aNativeKeyEvent type] != NSEventTypeKeyDown &&
      [aNativeKeyEvent type] != NSEventTypeKeyUp) {
    return;
  }

  UInt32 kbType = GetKbdType();

  if (MOZ_LOG_TEST(gKeyLog, LogLevel::Info)) {
    nsAutoString chars;
    CopyNSStringToXPCOMString([aNativeKeyEvent characters], chars);
    NS_ConvertUTF16toUTF8 utf8Chars(chars);
    char16_t uniChar = static_cast<char16_t>(aKeyEvent.mCharCode);
    MOZ_LOG(
        gKeyLog, LogLevel::Info,
        ("%p TISInputSourceWrapper::WillDispatchKeyboardEvent, "
         "aNativeKeyEvent=%p, aInsertString=%p (\"%s\"), "
         "aIndexOfKeypress=%u, [aNativeKeyEvent characters]=\"%s\", "
         "aKeyEvent={ mMessage=%s, mCharCode=0x%X(%s) }, kbType=0x%X, "
         "IsOpenedIMEMode()=%s",
         this, aNativeKeyEvent, aInsertString,
         aInsertString ? GetCharacters(*aInsertString).get() : "",
         aIndexOfKeypress, GetCharacters([aNativeKeyEvent characters]).get(),
         GetGeckoKeyEventType(aKeyEvent), aKeyEvent.mCharCode,
         uniChar ? NS_ConvertUTF16toUTF8(&uniChar, 1).get() : "",
         static_cast<unsigned int>(kbType), TrueOrFalse(IsOpenedIMEMode())));
  }

  nsAutoString insertStringForCharCode;
  ComputeInsertStringForCharCode(aNativeKeyEvent, aKeyEvent, aInsertString,
                                 insertStringForCharCode);

  // The mCharCode was set from mKeyValue. However, for example, when Ctrl key
  // is pressed, its value should indicate an ASCII character for backward
  // compatibility rather than inputting character without the modifiers.
  // Therefore, we need to modify mCharCode value here.
  uint32_t charCode = 0;
  if (aIndexOfKeypress < insertStringForCharCode.Length()) {
    charCode = insertStringForCharCode[aIndexOfKeypress];
  }
  aKeyEvent.SetCharCode(charCode);

  MOZ_LOG(gKeyLog, LogLevel::Info,
          ("%p   TISInputSourceWrapper::WillDispatchKeyboardEvent, "
           "aKeyEvent.mKeyCode=0x%X, aKeyEvent.mCharCode=0x%X",
           this, aKeyEvent.mKeyCode, aKeyEvent.mCharCode));

  // If aInsertString is not nullptr (it means InsertText() is called)
  // and it actually inputs a character, we don't need to append alternative
  // charCode values since such keyboard event shouldn't be handled as
  // a shortcut key.
  if (aInsertString && charCode) {
    return;
  }

  TISInputSourceWrapper USLayout("com.apple.keylayout.US");
  bool isRomanKeyboardLayout = IsASCIICapable();

  UInt32 key = [aNativeKeyEvent keyCode];

  // Caps lock and num lock modifier state:
  UInt32 lockState = 0;
  if ([aNativeKeyEvent modifierFlags] & NSEventModifierFlagCapsLock) {
    lockState |= alphaLock;
  }
  if ([aNativeKeyEvent modifierFlags] & NSEventModifierFlagNumericPad) {
    lockState |= kEventKeyModifierNumLockMask;
  }

  MOZ_LOG(gKeyLog, LogLevel::Info,
          ("%p   TISInputSourceWrapper::WillDispatchKeyboardEvent, "
           "isRomanKeyboardLayout=%s, kbType=0x%X, key=0x%X",
           this, TrueOrFalse(isRomanKeyboardLayout),
           static_cast<unsigned int>(kbType), static_cast<unsigned int>(key)));

  nsString str;

  // normal chars
  uint32_t unshiftedChar = TranslateToChar(key, lockState, kbType);
  UInt32 shiftLockMod = shiftKey | lockState;
  uint32_t shiftedChar = TranslateToChar(key, shiftLockMod, kbType);

  // characters generated with Cmd key
  // XXX we should remove CapsLock state, which changes characters from
  //     Latin to Cyrillic with Russian layout on 10.4 only when Cmd key
  //     is pressed.
  UInt32 numState = (lockState & ~alphaLock);  // only num lock state
  uint32_t uncmdedChar = TranslateToChar(key, numState, kbType);
  UInt32 shiftNumMod = numState | shiftKey;
  uint32_t uncmdedShiftChar = TranslateToChar(key, shiftNumMod, kbType);
  uint32_t uncmdedUSChar = USLayout.TranslateToChar(key, numState, kbType);
  UInt32 cmdNumMod = cmdKey | numState;
  uint32_t cmdedChar = TranslateToChar(key, cmdNumMod, kbType);
  UInt32 cmdShiftNumMod = shiftKey | cmdNumMod;
  uint32_t cmdedShiftChar = TranslateToChar(key, cmdShiftNumMod, kbType);

  // Is the keyboard layout changed by Cmd key?
  // E.g., Arabic, Russian, Hebrew, Greek and Dvorak-QWERTY.
  bool isCmdSwitchLayout = uncmdedChar != cmdedChar;
  // Is the keyboard layout for Latin, but Cmd key switches the layout?
  // I.e., Dvorak-QWERTY
  bool isDvorakQWERTY = isCmdSwitchLayout && isRomanKeyboardLayout;

  // If the current keyboard is not Dvorak-QWERTY or Cmd is not pressed,
  // we should append unshiftedChar and shiftedChar for handling the
  // normal characters.  These are the characters that the user is most
  // likely to associate with this key.
  if ((unshiftedChar || shiftedChar) &&
      (!aKeyEvent.IsMeta() || !isDvorakQWERTY)) {
    AlternativeCharCode altCharCodes(unshiftedChar, shiftedChar);
    aKeyEvent.mAlternativeCharCodes.AppendElement(altCharCodes);
  }
  MOZ_LOG(gKeyLog, LogLevel::Info,
          ("%p   TISInputSourceWrapper::WillDispatchKeyboardEvent, "
           "aKeyEvent.isMeta=%s, isDvorakQWERTY=%s, "
           "unshiftedChar=U+%X, shiftedChar=U+%X",
           this, OnOrOff(aKeyEvent.IsMeta()), TrueOrFalse(isDvorakQWERTY),
           unshiftedChar, shiftedChar));

  // Most keyboard layouts provide the same characters in the NSEvents
  // with Command+Shift as with Command.  However, with Command+Shift we
  // want the character on the second level.  e.g. With a US QWERTY
  // layout, we want "?" when the "/","?" key is pressed with
  // Command+Shift.

  // On a German layout, the OS gives us '/' with Cmd+Shift+SS(eszett)
  // even though Cmd+SS is 'SS' and Shift+'SS' is '?'.  This '/' seems
  // like a hack to make the Cmd+"?" event look the same as the Cmd+"?"
  // event on a US keyboard.  The user thinks they are typing Cmd+"?", so
  // we'll prefer the "?" character, replacing mCharCode with shiftedChar
  // when Shift is pressed.  However, in case there is a layout where the
  // character unique to Cmd+Shift is the character that the user expects,
  // we'll send it as an alternative char.
  bool hasCmdShiftOnlyChar =
      cmdedChar != cmdedShiftChar && uncmdedShiftChar != cmdedShiftChar;
  uint32_t originalCmdedShiftChar = cmdedShiftChar;

  // If we can make a good guess at the characters that the user would
  // expect this key combination to produce (with and without Shift) then
  // use those characters.  This also corrects for CapsLock, which was
  // ignored above.
  if (!isCmdSwitchLayout) {
    // The characters produced with Command seem similar to those without
    // Command.
    if (unshiftedChar) {
      cmdedChar = unshiftedChar;
    }
    if (shiftedChar) {
      cmdedShiftChar = shiftedChar;
    }
  } else if (uncmdedUSChar == cmdedChar) {
    // It looks like characters from a US layout are provided when Command
    // is down.
    uint32_t ch = USLayout.TranslateToChar(key, lockState, kbType);
    if (ch) {
      cmdedChar = ch;
    }
    ch = USLayout.TranslateToChar(key, shiftLockMod, kbType);
    if (ch) {
      cmdedShiftChar = ch;
    }
  }

  // If the current keyboard layout is switched by the Cmd key,
  // we should append cmdedChar and shiftedCmdChar that are
  // Latin char for the key.
  // If the keyboard layout is Dvorak-QWERTY, we should append them only when
  // command key is pressed because when command key isn't pressed, uncmded
  // chars have been appended already.
  if ((cmdedChar || cmdedShiftChar) && isCmdSwitchLayout &&
      (aKeyEvent.IsMeta() || !isDvorakQWERTY)) {
    AlternativeCharCode altCharCodes(cmdedChar, cmdedShiftChar);
    aKeyEvent.mAlternativeCharCodes.AppendElement(altCharCodes);
  }
  MOZ_LOG(
      gKeyLog, LogLevel::Info,
      ("%p   TISInputSourceWrapper::WillDispatchKeyboardEvent, "
       "hasCmdShiftOnlyChar=%s, isCmdSwitchLayout=%s, isDvorakQWERTY=%s, "
       "cmdedChar=U+%X, cmdedShiftChar=U+%X",
       this, TrueOrFalse(hasCmdShiftOnlyChar), TrueOrFalse(isCmdSwitchLayout),
       TrueOrFalse(isDvorakQWERTY), cmdedChar, cmdedShiftChar));
  // Special case for 'SS' key of German layout. See the comment of
  // hasCmdShiftOnlyChar definition for the detail.
  if (hasCmdShiftOnlyChar && originalCmdedShiftChar) {
    AlternativeCharCode altCharCodes(0, originalCmdedShiftChar);
    aKeyEvent.mAlternativeCharCodes.AppendElement(altCharCodes);
  }
  MOZ_LOG(gKeyLog, LogLevel::Info,
          ("%p   TISInputSourceWrapper::WillDispatchKeyboardEvent, "
           "hasCmdShiftOnlyChar=%s, originalCmdedShiftChar=U+%X",
           this, TrueOrFalse(hasCmdShiftOnlyChar), originalCmdedShiftChar));

  NS_OBJC_END_TRY_IGNORE_BLOCK
}

uint32_t TISInputSourceWrapper::ComputeGeckoKeyCode(UInt32 aNativeKeyCode,
                                                    UInt32 aKbType,
                                                    bool aCmdIsPressed) {
  MOZ_LOG(
      gKeyLog, LogLevel::Info,
      ("%p TISInputSourceWrapper::ComputeGeckoKeyCode, aNativeKeyCode=0x%X, "
       "aKbType=0x%X, aCmdIsPressed=%s, IsOpenedIMEMode()=%s, "
       "IsASCIICapable()=%s",
       this, static_cast<unsigned int>(aNativeKeyCode),
       static_cast<unsigned int>(aKbType), TrueOrFalse(aCmdIsPressed),
       TrueOrFalse(IsOpenedIMEMode()), TrueOrFalse(IsASCIICapable())));

  switch (aNativeKeyCode) {
    case kVK_Space:
      return NS_VK_SPACE;
    case kVK_Escape:
      return NS_VK_ESCAPE;

    // modifiers
    case kVK_RightCommand:
    case kVK_Command:
      return NS_VK_META;
    case kVK_RightShift:
    case kVK_Shift:
      return NS_VK_SHIFT;
    case kVK_CapsLock:
      return NS_VK_CAPS_LOCK;
    case kVK_RightControl:
    case kVK_Control:
      return NS_VK_CONTROL;
    case kVK_RightOption:
    case kVK_Option:
      return NS_VK_ALT;

    case kVK_ANSI_KeypadClear:
      return NS_VK_CLEAR;

    // function keys
    case kVK_F1:
      return NS_VK_F1;
    case kVK_F2:
      return NS_VK_F2;
    case kVK_F3:
      return NS_VK_F3;
    case kVK_F4:
      return NS_VK_F4;
    case kVK_F5:
      return NS_VK_F5;
    case kVK_F6:
      return NS_VK_F6;
    case kVK_F7:
      return NS_VK_F7;
    case kVK_F8:
      return NS_VK_F8;
    case kVK_F9:
      return NS_VK_F9;
    case kVK_F10:
      return NS_VK_F10;
    case kVK_F11:
      return NS_VK_F11;
    case kVK_F12:
      return NS_VK_F12;
    // case kVK_F13:               return NS_VK_F13;  // clash with the 3 below
    // case kVK_F14:               return NS_VK_F14;
    // case kVK_F15:               return NS_VK_F15;
    case kVK_F16:
      return NS_VK_F16;
    case kVK_F17:
      return NS_VK_F17;
    case kVK_F18:
      return NS_VK_F18;
    case kVK_F19:
      return NS_VK_F19;

    case kVK_PC_Pause:
      return NS_VK_PAUSE;
    case kVK_PC_ScrollLock:
      return NS_VK_SCROLL_LOCK;
    case kVK_PC_PrintScreen:
      return NS_VK_PRINTSCREEN;

    // keypad
    case kVK_ANSI_Keypad0:
      return NS_VK_NUMPAD0;
    case kVK_ANSI_Keypad1:
      return NS_VK_NUMPAD1;
    case kVK_ANSI_Keypad2:
      return NS_VK_NUMPAD2;
    case kVK_ANSI_Keypad3:
      return NS_VK_NUMPAD3;
    case kVK_ANSI_Keypad4:
      return NS_VK_NUMPAD4;
    case kVK_ANSI_Keypad5:
      return NS_VK_NUMPAD5;
    case kVK_ANSI_Keypad6:
      return NS_VK_NUMPAD6;
    case kVK_ANSI_Keypad7:
      return NS_VK_NUMPAD7;
    case kVK_ANSI_Keypad8:
      return NS_VK_NUMPAD8;
    case kVK_ANSI_Keypad9:
      return NS_VK_NUMPAD9;

    case kVK_ANSI_KeypadMultiply:
      return NS_VK_MULTIPLY;
    case kVK_ANSI_KeypadPlus:
      return NS_VK_ADD;
    case kVK_ANSI_KeypadMinus:
      return NS_VK_SUBTRACT;
    case kVK_ANSI_KeypadDecimal:
      return NS_VK_DECIMAL;
    case kVK_ANSI_KeypadDivide:
      return NS_VK_DIVIDE;

    case kVK_JIS_KeypadComma:
      return NS_VK_SEPARATOR;

    // IME keys
    case kVK_JIS_Eisu:
      return NS_VK_EISU;
    case kVK_JIS_Kana:
      return NS_VK_KANA;

    // these may clash with forward delete and help
    case kVK_PC_Insert:
      return NS_VK_INSERT;
    case kVK_PC_Delete:
      return NS_VK_DELETE;

    case kVK_PC_Backspace:
      return NS_VK_BACK;
    case kVK_Tab:
      return NS_VK_TAB;

    case kVK_Home:
      return NS_VK_HOME;
    case kVK_End:
      return NS_VK_END;

    case kVK_PageUp:
      return NS_VK_PAGE_UP;
    case kVK_PageDown:
      return NS_VK_PAGE_DOWN;

    case kVK_LeftArrow:
      return NS_VK_LEFT;
    case kVK_RightArrow:
      return NS_VK_RIGHT;
    case kVK_UpArrow:
      return NS_VK_UP;
    case kVK_DownArrow:
      return NS_VK_DOWN;

    case kVK_PC_ContextMenu:
      return NS_VK_CONTEXT_MENU;

    case kVK_ANSI_1:
      return NS_VK_1;
    case kVK_ANSI_2:
      return NS_VK_2;
    case kVK_ANSI_3:
      return NS_VK_3;
    case kVK_ANSI_4:
      return NS_VK_4;
    case kVK_ANSI_5:
      return NS_VK_5;
    case kVK_ANSI_6:
      return NS_VK_6;
    case kVK_ANSI_7:
      return NS_VK_7;
    case kVK_ANSI_8:
      return NS_VK_8;
    case kVK_ANSI_9:
      return NS_VK_9;
    case kVK_ANSI_0:
      return NS_VK_0;

    case kVK_ANSI_KeypadEnter:
    case kVK_Return:
    case kVK_Powerbook_KeypadEnter:
      return NS_VK_RETURN;
  }

  // If Cmd key is pressed, that causes switching keyboard layout temporarily.
  // E.g., Dvorak-QWERTY.  Therefore, if Cmd key is pressed, we should honor it.
  UInt32 modifiers = aCmdIsPressed ? cmdKey : 0;

  uint32_t charCode = TranslateToChar(aNativeKeyCode, modifiers, aKbType);

  // Special case for Mac.  Mac inputs Yen sign (U+00A5) directly instead of
  // Back slash (U+005C).  We should return NS_VK_BACK_SLASH for compatibility
  // with other platforms.
  // XXX How about Won sign (U+20A9) which has same problem as Yen sign?
  if (charCode == 0x00A5) {
    return NS_VK_BACK_SLASH;
  }

  uint32_t keyCode = WidgetUtils::ComputeKeyCodeFromChar(charCode);
  if (keyCode) {
    return keyCode;
  }

  // If the unshifed char isn't an ASCII character, use shifted char.
  charCode = TranslateToChar(aNativeKeyCode, modifiers | shiftKey, aKbType);
  keyCode = WidgetUtils::ComputeKeyCodeFromChar(charCode);
  if (keyCode) {
    return keyCode;
  }

  if (!IsASCIICapable()) {
    // Retry with ASCII capable keyboard layout.
    TISInputSourceWrapper currentKeyboardLayout;
    currentKeyboardLayout.InitByCurrentASCIICapableKeyboardLayout();
    NS_ENSURE_TRUE(mInputSource != currentKeyboardLayout.mInputSource, 0);
    keyCode = currentKeyboardLayout.ComputeGeckoKeyCode(aNativeKeyCode, aKbType,
                                                        aCmdIsPressed);
    // We've returned 0 for long time if keyCode isn't for an alphabet keys or
    // a numeric key even in alternative ASCII capable keyboard layout because
    // we decided that we should avoid setting same keyCode value to 2 or
    // more keys since active keyboard layout may have a key to input the
    // punctuation with different key.  However, setting keyCode to 0 makes
    // some web applications which are aware of neither KeyboardEvent.key nor
    // KeyboardEvent.code not work with Firefox when user selects non-ASCII
    // capable keyboard layout such as Russian and Thai.  So, if alternative
    // ASCII capable keyboard layout has keyCode value for the key, we should
    // use it.  In other words, this behavior does that non-ASCII capable
    // keyboard layout overrides some keys' keyCode value only if the key
    // produces ASCII character by itself or with Shift key.
    if (keyCode) {
      return keyCode;
    }
  }

  // Otherwise, let's decide keyCode value from the native virtual keycode
  // value on major keyboard layout.
  CodeNameIndex code = ComputeGeckoCodeNameIndex(aNativeKeyCode, aKbType);
  return WidgetKeyboardEvent::GetFallbackKeyCodeOfPunctuationKey(code);
}

// static
KeyNameIndex TISInputSourceWrapper::ComputeGeckoKeyNameIndex(
    UInt32 aNativeKeyCode) {
  // NOTE:
  //   When unsupported keys like Convert, Nonconvert of Japanese keyboard is
  //   pressed:
  //     on 10.6.x, 'A' key event is fired (and also actually 'a' is inserted).
  //     on 10.7.x, Nothing happens.
  //     on 10.8.x, Nothing happens.
  //     on 10.9.x, FlagsChanged event is fired with keyCode 0xFF.
  switch (aNativeKeyCode) {
#define NS_NATIVE_KEY_TO_DOM_KEY_NAME_INDEX(aNativeKey, aKeyNameIndex) \
  case aNativeKey:                                                     \
    return aKeyNameIndex;

#include "NativeKeyToDOMKeyName.inc"

#undef NS_NATIVE_KEY_TO_DOM_KEY_NAME_INDEX

    default:
      return KEY_NAME_INDEX_Unidentified;
  }
}

// static
CodeNameIndex TISInputSourceWrapper::ComputeGeckoCodeNameIndex(
    UInt32 aNativeKeyCode, UInt32 aKbType) {
  // macOS swaps native key code between Backquote key and IntlBackslash key
  // only when the keyboard type is ISO.  Let's treat the key code after
  // swapping them here because Chromium does so only when computing .code
  // value.
  if (::KBGetLayoutType(aKbType) == kKeyboardISO) {
    if (aNativeKeyCode == kVK_ISO_Section) {
      aNativeKeyCode = kVK_ANSI_Grave;
    } else if (aNativeKeyCode == kVK_ANSI_Grave) {
      aNativeKeyCode = kVK_ISO_Section;
    }
  }

  switch (aNativeKeyCode) {
#define NS_NATIVE_KEY_TO_DOM_CODE_NAME_INDEX(aNativeKey, aCodeNameIndex) \
  case aNativeKey:                                                       \
    return aCodeNameIndex;

#include "NativeKeyToDOMCodeName.inc"

#undef NS_NATIVE_KEY_TO_DOM_CODE_NAME_INDEX

    default:
      return CODE_NAME_INDEX_UNKNOWN;
  }
}

#pragma mark -

/******************************************************************************
 *
 *  TextInputHandler implementation (static methods)
 *
 ******************************************************************************/

NSUInteger TextInputHandler::sLastModifierState = 0;

// static
CFArrayRef TextInputHandler::CreateAllKeyboardLayoutList() {
  const void* keys[] = {kTISPropertyInputSourceType};
  const void* values[] = {kTISTypeKeyboardLayout};
  CFDictionaryRef filter = ::CFDictionaryCreate(kCFAllocatorDefault, keys,
                                                values, 1, nullptr, nullptr);
  NS_ASSERTION(filter, "failed to create the filter");
  CFArrayRef list = ::TISCreateInputSourceList(filter, true);
  ::CFRelease(filter);
  return list;
}

// static
void TextInputHandler::DebugPrintAllKeyboardLayouts() {
  if (MOZ_LOG_TEST(gKeyLog, LogLevel::Info)) {
    CFArrayRef list = CreateAllKeyboardLayoutList();
    MOZ_LOG(gKeyLog, LogLevel::Info, ("Keyboard layout configuration:"));
    CFIndex idx = ::CFArrayGetCount(list);
    TISInputSourceWrapper tis;
    for (CFIndex i = 0; i < idx; ++i) {
      TISInputSourceRef inputSource = static_cast<TISInputSourceRef>(
          const_cast<void*>(::CFArrayGetValueAtIndex(list, i)));
      tis.InitByTISInputSourceRef(inputSource);
      nsAutoString name, isid;
      tis.GetLocalizedName(name);
      tis.GetInputSourceID(isid);
      MOZ_LOG(gKeyLog, LogLevel::Info,
              ("  %s\t<%s>%s%s\n", NS_ConvertUTF16toUTF8(name).get(),
               NS_ConvertUTF16toUTF8(isid).get(),
               tis.IsASCIICapable() ? "" : "\t(Isn't ASCII capable)",
               tis.IsKeyboardLayout() && tis.GetUCKeyboardLayout()
                   ? ""
                   : "\t(uchr is NOT AVAILABLE)"));
    }
    ::CFRelease(list);
  }
}

#pragma mark -

/******************************************************************************
 *
 *  TextInputHandler implementation
 *
 ******************************************************************************/

TextInputHandler::TextInputHandler(nsCocoaWindow* aWidget,
                                   NSView<mozView>* aNativeView)
    : IMEInputHandler(aWidget, aNativeView) {
  EnsureToLogAllKeyboardLayoutsAndIMEs();
  [mView installTextInputHandler:this];
}

TextInputHandler::~TextInputHandler() { [mView uninstallTextInputHandler]; }

bool TextInputHandler::HandleKeyDownEvent(NSEvent* aNativeEvent,
                                          uint32_t aUniqueId) {
  NS_OBJC_BEGIN_TRY_BLOCK_RETURN;

  if (Destroyed()) {
    MOZ_LOG_KEY_OR_IME(LogLevel::Info,
                       ("%p TextInputHandler::HandleKeyDownEvent, "
                        "widget has been already destroyed",
                        this));
    return false;
  }

  // Insert empty line to the log for easier to read.
  MOZ_LOG_KEY_OR_IME(LogLevel::Info, (""));
  MOZ_LOG_KEY_OR_IME(
      LogLevel::Info,
      ("%p TextInputHandler::HandleKeyDownEvent, aNativeEvent=%p, "
       "type=%s, keyCode=%u (0x%X), modifierFlags=0x%lX, characters=\"%s\", "
       "charactersIgnoringModifiers=\"%s\"",
       this, aNativeEvent, GetNativeKeyEventType(aNativeEvent),
       [aNativeEvent keyCode], [aNativeEvent keyCode],
       static_cast<unsigned long>([aNativeEvent modifierFlags]),
       GetCharacters([aNativeEvent characters]).get(),
       GetCharacters([aNativeEvent charactersIgnoringModifiers]).get()));

  // We should hide the mouse cursor until the next mousemove, unless we aren't
  // dealing with editable content or the Command key was pressed. We hide the
  // mouse cursor even if the key event will be handled by IME (i.e., even
  // without dispatching eKeyPress events).
  if (IsEditableContent() &&
      !([aNativeEvent modifierFlags] & NSEventModifierFlagCommand)) {
    [NSCursor setHiddenUntilMouseMoves:YES];
  }

  RefPtr<nsCocoaWindow> widget(mWidget);

  KeyEventState* currentKeyEvent = PushKeyEvent(aNativeEvent, aUniqueId);
  AutoKeyEventStateCleaner remover(this);

  RefPtr<TextInputHandler> kungFuDeathGrip(this);

  // When we're already in a composition, we need always to mark the eKeyDown
  // event as "processed by IME".  So, let's dispatch eKeyDown event here in
  // such case.
  if (IsIMEComposing() && !MaybeDispatchCurrentKeydownEvent(true)) {
    MOZ_LOG_KEY_OR_IME(LogLevel::Info,
                       ("%p   TextInputHandler::HandleKeyDownEvent, eKeyDown "
                        "caused focus move or "
                        "something and canceling the composition",
                        this));
    return false;
  }

  // macOS supports shortcut keys using the `Fn` key. Check first if this key
  // event is such a shortcut key.
  if (nsCocoaUtils::ModifiersForEvent(aNativeEvent) & MODIFIER_FN) {
    if (mWidget->SendEventToNativeMenuSystem(aNativeEvent)) {
      return true;
    }
  }

  // Let Cocoa interpret the key events, caching IsIMEComposing first.
  bool wasComposing = IsIMEComposing();
  bool interpretKeyEventsCalled = false;
  // Don't call interpretKeyEvents when a plugin has focus.  If we call it,
  // for example, a character is inputted twice during a composition in e10s
  // mode.
  if (IsIMEEnabled() || IsASCIICapableOnly()) {
    MOZ_LOG_KEY_OR_IME(LogLevel::Info,
                       ("%p   TextInputHandler::HandleKeyDownEvent, calling "
                        "interpretKeyEvents",
                        this));
    [mView interpretKeyEvents:[NSArray arrayWithObject:aNativeEvent]];
    interpretKeyEventsCalled = true;
    MOZ_LOG_KEY_OR_IME(
        LogLevel::Info,
        ("%p   TextInputHandler::HandleKeyDownEvent, called interpretKeyEvents",
         this));
  }

  if (Destroyed()) {
    MOZ_LOG_KEY_OR_IME(
        LogLevel::Info,
        ("%p   TextInputHandler::HandleKeyDownEvent, widget was destroyed",
         this));
    return currentKeyEvent->IsDefaultPrevented();
  }

  MOZ_LOG_KEY_OR_IME(
      LogLevel::Info,
      ("%p   TextInputHandler::HandleKeyDownEvent, wasComposing=%s, "
       "IsIMEComposing()=%s",
       this, TrueOrFalse(wasComposing), TrueOrFalse(IsIMEComposing())));

  if (currentKeyEvent->CanDispatchKeyDownEvent()) {
    // Dispatch eKeyDown event if nobody has dispatched it yet.
    // NOTE: Although reaching here means that the native keydown event may
    //       not be handled by IME.  However, we cannot know if it is.
    //       For example, Japanese IME of Apple shows candidate window for
    //       typing window.  They, you can switch the sort order with Tab key.
    //       However, when you choose "Symbol" of the sort order, there may
    //       be no candidate words.  In this case, IME handles the Tab key
    //       actually, but we cannot know it because composition string is
    //       not updated.  So, let's mark eKeyDown event as "processed by IME"
    //       when there is composition string.  This is same as Chrome.
    MOZ_LOG_KEY_OR_IME(LogLevel::Info,
                       ("%p   TextInputHandler::HandleKeyDownEvent, trying to "
                        "dispatch eKeyDown "
                        "event since it's not yet dispatched",
                        this));
    if (!MaybeDispatchCurrentKeydownEvent(IsIMEComposing())) {
      return true;  // treat the eKeydDown event as consumed.
    }
    MOZ_LOG_KEY_OR_IME(
        LogLevel::Info,
        ("%p   TextInputHandler::HandleKeyDownEvent, eKeyDown event has been "
         "dispatched",
         this));
  }

  if (currentKeyEvent->CanDispatchKeyPressEvent() && !wasComposing &&
      !IsIMEComposing()) {
    nsresult rv = mDispatcher->BeginNativeInputTransaction();
    if (NS_WARN_IF(NS_FAILED(rv))) {
      MOZ_LOG_KEY_OR_IME(
          LogLevel::Error,
          ("%p   TextInputHandler::HandleKeyDownEvent, "
           "FAILED, due to BeginNativeInputTransaction() failure "
           "at dispatching keypress",
           this));
      return false;
    }

    WidgetKeyboardEvent keypressEvent(true, eKeyPress, widget);
    currentKeyEvent->InitKeyEvent(this, keypressEvent, false);

    // If we called interpretKeyEvents and this isn't normal character input
    // then IME probably ate the event for some reason. We do not want to
    // send a key press event in that case.
    // TODO:
    // There are some other cases which IME eats the current event.
    // 1. If key events were nested during calling interpretKeyEvents, it means
    //    that IME did something.  Then, we should do nothing.
    // 2. If one or more commands are called like "deleteBackward", we should
    //    dispatch keypress event at that time.  Note that the command may have
    //    been a converted or generated action by IME.  Then, we shouldn't do
    //    our default action for this key.
    if (!(interpretKeyEventsCalled &&
          IsNormalCharInputtingEvent(aNativeEvent))) {
      MOZ_LOG_KEY_OR_IME(
          LogLevel::Info,
          ("%p   TextInputHandler::HandleKeyDownEvent, trying to dispatch "
           "eKeyPress event since it's not yet dispatched",
           this));
      nsEventStatus status = nsEventStatus_eIgnore;
      currentKeyEvent->mKeyPressDispatched =
          mDispatcher->MaybeDispatchKeypressEvents(keypressEvent, status,
                                                   currentKeyEvent);
      currentKeyEvent->mKeyPressHandled =
          (status == nsEventStatus_eConsumeNoDefault);
      MOZ_LOG_KEY_OR_IME(
          LogLevel::Info,
          ("%p TextInputHandler::HandleKeyDownEvent, eKeyPress event has been "
           "dispatched",
           this));
    }
  }

  // Note: mWidget might have become null here. Don't count on it from here on.

  MOZ_LOG_KEY_OR_IME(
      LogLevel::Info,
      ("%p   TextInputHandler::HandleKeyDownEvent, "
       "keydown handled=%s, keypress handled=%s, causedOtherKeyEvents=%s, "
       "compositionDispatched=%s",
       this, TrueOrFalse(currentKeyEvent->mKeyDownHandled),
       TrueOrFalse(currentKeyEvent->mKeyPressHandled),
       TrueOrFalse(currentKeyEvent->mCausedOtherKeyEvents),
       TrueOrFalse(currentKeyEvent->mCompositionDispatched)));
  // Insert empty line to the log for easier to read.
  MOZ_LOG_KEY_OR_IME(LogLevel::Info, (""));
  return currentKeyEvent->IsDefaultPrevented();

  NS_OBJC_END_TRY_BLOCK_RETURN(false);
}

void TextInputHandler::HandleKeyUpEvent(NSEvent* aNativeEvent) {
  NS_OBJC_BEGIN_TRY_IGNORE_BLOCK;

  MOZ_LOG_KEY_OR_IME(
      LogLevel::Info,
      ("%p   TextInputHandler::HandleKeyUpEvent, aNativeEvent=%p, "
       "type=%s, keyCode=%u (0x%X), modifierFlags=0x%lX, characters=\"%s\", "
       "charactersIgnoringModifiers=\"%s\", "
       "IsIMEComposing()=%s",
       this, aNativeEvent, GetNativeKeyEventType(aNativeEvent),
       [aNativeEvent keyCode], [aNativeEvent keyCode],
       static_cast<unsigned long>([aNativeEvent modifierFlags]),
       GetCharacters([aNativeEvent characters]).get(),
       GetCharacters([aNativeEvent charactersIgnoringModifiers]).get(),
       TrueOrFalse(IsIMEComposing())));

  if (Destroyed()) {
    MOZ_LOG_KEY_OR_IME(LogLevel::Info,
                       ("%p TextInputHandler::HandleKeyUpEvent, "
                        "widget has been already destroyed",
                        this));
    return;
  }

  nsresult rv = mDispatcher->BeginNativeInputTransaction();
  if (NS_WARN_IF(NS_FAILED(rv))) {
    MOZ_LOG_KEY_OR_IME(LogLevel::Error,
                       ("%p   TextInputHandler::HandleKeyUpEvent, "
                        "FAILED, due to BeginNativeInputTransaction() failure",
                        this));
    return;
  }

  // Neither Chrome for macOS nor Safari marks "keyup" event as "processed by
  // IME" even during composition.  So, let's follow this behavior.
  WidgetKeyboardEvent keyupEvent(true, eKeyUp, mWidget);
  InitKeyEvent(aNativeEvent, keyupEvent, false);

  KeyEventState currentKeyEvent(aNativeEvent);
  nsEventStatus status = nsEventStatus_eIgnore;
  mDispatcher->DispatchKeyboardEvent(eKeyUp, keyupEvent, status,
                                     &currentKeyEvent);

  if (!mCurrentKeyEvents.Length()) {
    // No pending key. Then show text substitution panel if necessary.
    ShowTextSubstitutionPanel();
  }

  NS_OBJC_END_TRY_IGNORE_BLOCK;
}

void TextInputHandler::HandleFlagsChanged(NSEvent* aNativeEvent) {
  NS_OBJC_BEGIN_TRY_IGNORE_BLOCK;

  if (Destroyed()) {
    MOZ_LOG_KEY_OR_IME(LogLevel::Info,
                       ("%p TextInputHandler::HandleFlagsChanged, "
                        "widget has been already destroyed",
                        this));
    return;
  }

  RefPtr<nsCocoaWindow> kungFuDeathGrip(mWidget);
  (void)kungFuDeathGrip;  // Not referenced within this function

  MOZ_LOG_KEY_OR_IME(
      LogLevel::Info,
      ("%p TextInputHandler::HandleFlagsChanged, aNativeEvent=%p, "
       "type=%s, keyCode=%s (0x%X), modifierFlags=0x%08lX, "
       "sLastModifierState=0x%08lX, IsIMEComposing()=%s",
       this, aNativeEvent, GetNativeKeyEventType(aNativeEvent),
       GetKeyNameForNativeKeyCode([aNativeEvent keyCode]),
       [aNativeEvent keyCode],
       static_cast<unsigned long>([aNativeEvent modifierFlags]),
       static_cast<unsigned long>(sLastModifierState),
       TrueOrFalse(IsIMEComposing())));

  MOZ_ASSERT([aNativeEvent type] == NSEventTypeFlagsChanged);

  NSUInteger diff = [aNativeEvent modifierFlags] ^ sLastModifierState;
  // Device dependent flags for left-control key, both shift keys, both command
  // keys and both option keys have been defined in Next's SDK.  But we
  // shouldn't use it directly as far as possible since Cocoa SDK doesn't
  // define them.  Fortunately, we need them only when we dispatch keyup
  // events.  So, we can usually know the actual relation between keyCode and
  // device dependent flags.  However, we need to remove following flags first
  // since the differences don't indicate modifier key state.
  // NX_STYLUSPROXIMITYMASK: Probably used for pen like device.
  // kCGEventFlagMaskNonCoalesced (= NX_NONCOALSESCEDMASK): See the document for
  // Quartz Event Services.
  diff &= ~(NX_STYLUSPROXIMITYMASK | kCGEventFlagMaskNonCoalesced);

  switch ([aNativeEvent keyCode]) {
    // CapsLock state and other modifier states are different:
    // CapsLock state does not revert when the CapsLock key goes up, as the
    // modifier state does for other modifier keys on key up.
    case kVK_CapsLock: {
      // Fire key down event for caps lock.
      DispatchKeyEventForFlagsChanged(aNativeEvent, true);
      // XXX should we fire keyup event too? The keyup event for CapsLock key
      // is never dispatched on Gecko.
      // XXX WebKit dispatches keydown event when CapsLock is locked, otherwise,
      // keyup event.  If we do so, we cannot keep the consistency with other
      // platform's behavior...
      break;
    }

    // If the event is caused by pressing or releasing a modifier key, just
    // dispatch the key's event.
    case kVK_Shift:
    case kVK_RightShift:
    case kVK_Command:
    case kVK_RightCommand:
    case kVK_Control:
    case kVK_RightControl:
    case kVK_Option:
    case kVK_RightOption:
    case kVK_Help: {
      // We assume that at most one modifier is changed per event if the event
      // is caused by pressing or releasing a modifier key.
      bool isKeyDown = ([aNativeEvent modifierFlags] & diff) != 0;
      DispatchKeyEventForFlagsChanged(aNativeEvent, isKeyDown);
      // XXX Some applications might send the event with incorrect device-
      //     dependent flags.
      if (isKeyDown &&
          ((diff & ~NSEventModifierFlagDeviceIndependentFlagsMask) != 0)) {
        unsigned short keyCode = [aNativeEvent keyCode];
        const ModifierKey* modifierKey =
            GetModifierKeyForDeviceDependentFlags(diff);
        if (modifierKey && modifierKey->keyCode != keyCode) {
          // Although, we're not sure the actual cause of this case, the stored
          // modifier information and the latest key event information may be
          // mismatched. Then, let's reset the stored information.
          // NOTE: If this happens, it may fail to handle
          // NSEventTypeFlagsChanged event in the default case (below). However,
          // it's the rare case handler and this case occurs rarely. So, we can
          // ignore the edge case bug.
          NS_WARNING("Resetting stored modifier key information");
          mModifierKeys.Clear();
          modifierKey = nullptr;
        }
        if (!modifierKey) {
          mModifierKeys.AppendElement(ModifierKey(diff, keyCode));
        }
      }
      break;
    }

    // Currently we don't support Fn key since other browsers don't dispatch
    // events for it and we don't have keyCode for this key.
    // It should be supported when we implement .key and .char.
    case kVK_Function:
      break;

    // If the event is caused by something else than pressing or releasing a
    // single modifier key (for example by the app having been deactivated
    // using command-tab), use the modifiers themselves to determine which
    // key's event to dispatch, and whether it's a keyup or keydown event.
    // In all cases we assume one or more modifiers are being deactivated
    // (never activated) -- otherwise we'd have received one or more events
    // corresponding to a single modifier key being pressed.
    default: {
      NSUInteger modifiers = sLastModifierState;
      AutoTArray<unsigned short, 10> dispatchedKeyCodes;
      for (int32_t bit = 0; bit < 32; ++bit) {
        NSUInteger flag = 1 << bit;
        if (!(diff & flag)) {
          continue;
        }

        // Given correct information from the application, a flag change here
        // will normally be a deactivation (except for some lockable modifiers
        // such as CapsLock).  But some applications (like VNC) can send an
        // activating event with a zero keyCode.  So we need to check for that
        // here.
        bool dispatchKeyDown = ((flag & [aNativeEvent modifierFlags]) != 0);

        unsigned short keyCode = 0;
        if (flag & NSEventModifierFlagDeviceIndependentFlagsMask) {
          switch (flag) {
            case NSEventModifierFlagCapsLock:
              keyCode = kVK_CapsLock;
              dispatchKeyDown = true;
              break;

            case NSEventModifierFlagNumericPad:
              // NSEventModifierFlagNumericPad is fired by VNC a lot. But not
              // all of these events can really be Clear key events, so we just
              // ignore them.
              continue;

            case NSEventModifierFlagHelp:
              keyCode = kVK_Help;
              break;

            case NSEventModifierFlagFunction:
              // An NSEventModifierFlagFunction change here will normally be a
              // deactivation.  But sometimes it will be an activation send (by
              // VNC for example) with a zero keyCode.
              continue;

            // These cases (NSEventModifierFlagShift,
            // NSEventModifierFlagControl, NSEventModifierFlagOption and
            // NSEventModifierFlagCommand) should be handled by the other branch
            // of the if statement, below (which handles device dependent
            // flags). However, some applications (like VNC) can send key events
            // without any device dependent flags, so we handle them here
            // instead.
            case NSEventModifierFlagShift:
              keyCode = (modifiers & 0x0004) ? kVK_RightShift : kVK_Shift;
              break;
            case NSEventModifierFlagControl:
              keyCode = (modifiers & 0x2000) ? kVK_RightControl : kVK_Control;
              break;
            case NSEventModifierFlagOption:
              keyCode = (modifiers & 0x0040) ? kVK_RightOption : kVK_Option;
              break;
            case NSEventModifierFlagCommand:
              keyCode = (modifiers & 0x0010) ? kVK_RightCommand : kVK_Command;
              break;

            default:
              continue;
          }
        } else {
          const ModifierKey* modifierKey =
              GetModifierKeyForDeviceDependentFlags(flag);
          if (!modifierKey) {
            // See the note above (in the other branch of the if statement)
            // about the NSEventModifierFlagShift, NSEventModifierFlagControl,
            // NSEventModifierFlagOption and NSEventModifierFlagCommand cases.
            continue;
          }
          keyCode = modifierKey->keyCode;
        }

        // Remove flags
        modifiers &= ~flag;
        switch (keyCode) {
          case kVK_Shift: {
            const ModifierKey* modifierKey =
                GetModifierKeyForNativeKeyCode(kVK_RightShift);
            if (!modifierKey ||
                !(modifiers & modifierKey->GetDeviceDependentFlags())) {
              modifiers &= ~NSEventModifierFlagShift;
            }
            break;
          }
          case kVK_RightShift: {
            const ModifierKey* modifierKey =
                GetModifierKeyForNativeKeyCode(kVK_Shift);
            if (!modifierKey ||
                !(modifiers & modifierKey->GetDeviceDependentFlags())) {
              modifiers &= ~NSEventModifierFlagShift;
            }
            break;
          }
          case kVK_Command: {
            const ModifierKey* modifierKey =
                GetModifierKeyForNativeKeyCode(kVK_RightCommand);
            if (!modifierKey ||
                !(modifiers & modifierKey->GetDeviceDependentFlags())) {
              modifiers &= ~NSEventModifierFlagCommand;
            }
            break;
          }
          case kVK_RightCommand: {
            const ModifierKey* modifierKey =
                GetModifierKeyForNativeKeyCode(kVK_Command);
            if (!modifierKey ||
                !(modifiers & modifierKey->GetDeviceDependentFlags())) {
              modifiers &= ~NSEventModifierFlagCommand;
            }
            break;
          }
          case kVK_Control: {
            const ModifierKey* modifierKey =
                GetModifierKeyForNativeKeyCode(kVK_RightControl);
            if (!modifierKey ||
                !(modifiers & modifierKey->GetDeviceDependentFlags())) {
              modifiers &= ~NSEventModifierFlagControl;
            }
            break;
          }
          case kVK_RightControl: {
            const ModifierKey* modifierKey =
                GetModifierKeyForNativeKeyCode(kVK_Control);
            if (!modifierKey ||
                !(modifiers & modifierKey->GetDeviceDependentFlags())) {
              modifiers &= ~NSEventModifierFlagControl;
            }
            break;
          }
          case kVK_Option: {
            const ModifierKey* modifierKey =
                GetModifierKeyForNativeKeyCode(kVK_RightOption);
            if (!modifierKey ||
                !(modifiers & modifierKey->GetDeviceDependentFlags())) {
              modifiers &= ~NSEventModifierFlagOption;
            }
            break;
          }
          case kVK_RightOption: {
            const ModifierKey* modifierKey =
                GetModifierKeyForNativeKeyCode(kVK_Option);
            if (!modifierKey ||
                !(modifiers & modifierKey->GetDeviceDependentFlags())) {
              modifiers &= ~NSEventModifierFlagOption;
            }
            break;
          }
          case kVK_Help:
            modifiers &= ~NSEventModifierFlagHelp;
            break;
          default:
            break;
        }

        // Avoid dispatching same keydown/keyup events twice or more.
        // We must be able to assume that there is no case to dispatch
        // both keydown and keyup events with same key code value here.
        if (dispatchedKeyCodes.Contains(keyCode)) {
          continue;
        }
        dispatchedKeyCodes.AppendElement(keyCode);

        NSEvent* event =
            [NSEvent keyEventWithType:NSEventTypeFlagsChanged
                                   location:[aNativeEvent locationInWindow]
                              modifierFlags:modifiers
                                  timestamp:[aNativeEvent timestamp]
                               windowNumber:[aNativeEvent windowNumber]
                                    context:nil
                                 characters:@""
                charactersIgnoringModifiers:@""
                                  isARepeat:NO
                                    keyCode:keyCode];
        DispatchKeyEventForFlagsChanged(event, dispatchKeyDown);
        if (Destroyed()) {
          break;
        }

        // Stop if focus has changed.
        // Check to see if mView is still the first responder.
        if (![mView isFirstResponder]) {
          break;
        }
      }
      break;
    }
  }

  // Be aware, the widget may have been destroyed.
  sLastModifierState = [aNativeEvent modifierFlags];

  NS_OBJC_END_TRY_IGNORE_BLOCK;
}

const TextInputHandler::ModifierKey*
TextInputHandler::GetModifierKeyForNativeKeyCode(
    unsigned short aKeyCode) const {
  for (ModifierKeyArray::index_type i = 0; i < mModifierKeys.Length(); ++i) {
    if (mModifierKeys[i].keyCode == aKeyCode) {
      return &((ModifierKey&)mModifierKeys[i]);
    }
  }
  return nullptr;
}

const TextInputHandler::ModifierKey*
TextInputHandler::GetModifierKeyForDeviceDependentFlags(
    NSUInteger aFlags) const {
  for (ModifierKeyArray::index_type i = 0; i < mModifierKeys.Length(); ++i) {
    if (mModifierKeys[i].GetDeviceDependentFlags() ==
        (aFlags & ~NSEventModifierFlagDeviceIndependentFlagsMask)) {
      return &((ModifierKey&)mModifierKeys[i]);
    }
  }
  return nullptr;
}

void TextInputHandler::DispatchKeyEventForFlagsChanged(NSEvent* aNativeEvent,
                                                       bool aDispatchKeyDown) {
  NS_OBJC_BEGIN_TRY_IGNORE_BLOCK;

  if (Destroyed()) {
    return;
  }

  MOZ_LOG_KEY_OR_IME(
      LogLevel::Info,
      ("%p TextInputHandler::DispatchKeyEventForFlagsChanged, aNativeEvent=%p, "
       "type=%s, keyCode=%s (0x%X), aDispatchKeyDown=%s, IsIMEComposing()=%s",
       this, aNativeEvent, GetNativeKeyEventType(aNativeEvent),
       GetKeyNameForNativeKeyCode([aNativeEvent keyCode]),
       [aNativeEvent keyCode], TrueOrFalse(aDispatchKeyDown),
       TrueOrFalse(IsIMEComposing())));

  if ([aNativeEvent type] != NSEventTypeFlagsChanged) {
    return;
  }

  nsresult rv = mDispatcher->BeginNativeInputTransaction();
  if (NS_WARN_IF(NS_FAILED(rv))) {
    MOZ_LOG_KEY_OR_IME(
        LogLevel::Error,
        ("%p   TextInputHandler::DispatchKeyEventForFlagsChanged, "
         "FAILED, due to BeginNativeInputTransaction() failure",
         this));
    return;
  }

  EventMessage message = aDispatchKeyDown ? eKeyDown : eKeyUp;

  // Fire a key event for the modifier key.  Note that even if modifier key
  // is pressed during composition, we shouldn't mark the keyboard event as
  // "processed by IME" since neither Chrome for macOS nor Safari does it.
  WidgetKeyboardEvent keyEvent(true, message, mWidget);
  InitKeyEvent(aNativeEvent, keyEvent, false);

  KeyEventState currentKeyEvent(aNativeEvent);
  nsEventStatus status = nsEventStatus_eIgnore;
  mDispatcher->DispatchKeyboardEvent(message, keyEvent, status,
                                     &currentKeyEvent);

  NS_OBJC_END_TRY_IGNORE_BLOCK;
}

void TextInputHandler::InsertText(NSString* aString,
                                  NSRange* aReplacementRange) {
  NS_OBJC_BEGIN_TRY_IGNORE_BLOCK;

  if (Destroyed()) {
    return;
  }

  KeyEventState* currentKeyEvent = GetCurrentKeyEvent();

  MOZ_LOG_KEY_OR_IME(
      LogLevel::Info,
      ("%p TextInputHandler::InsertText, aString=\"%s\", aReplacementRange=%s, "
       "IsIMEComposing()=%s, keyevent=%p, keydownDispatched=%s, "
       "keydownHandled=%s, keypressDispatched=%s, causedOtherKeyEvents=%s, "
       "compositionDispatched=%s",
       this, GetCharacters(aString).get(),
       aReplacementRange ? ToString(*aReplacementRange).c_str() : "nullptr",
       TrueOrFalse(IsIMEComposing()),
       currentKeyEvent ? currentKeyEvent->mKeyEvent : nullptr,
       currentKeyEvent ? TrueOrFalse(currentKeyEvent->mKeyDownDispatched)
                       : "N/A",
       currentKeyEvent ? TrueOrFalse(currentKeyEvent->mKeyDownHandled) : "N/A",
       currentKeyEvent ? TrueOrFalse(currentKeyEvent->mKeyPressDispatched)
                       : "N/A",
       currentKeyEvent ? TrueOrFalse(currentKeyEvent->mCausedOtherKeyEvents)
                       : "N/A",
       currentKeyEvent ? TrueOrFalse(currentKeyEvent->mCompositionDispatched)
                       : "N/A"));

  InputContext context = mWidget->GetInputContext();
  bool isEditable = (context.mIMEState.mEnabled == IMEEnabled::Enabled ||
                     context.mIMEState.mEnabled == IMEEnabled::Password);
  NSRange selectedRange = SelectedRange();

  nsAutoString str;
  CopyNSStringToXPCOMString(aString, str);

  AutoInsertStringClearer clearer(currentKeyEvent);
  if (currentKeyEvent) {
    currentKeyEvent->mInsertString = &str;
  }

  if (!IsIMEComposing() && str.IsEmpty()) {
    // nothing to do if there is no content which can be removed.
    if (!isEditable) {
      return;
    }
    // If replacement range is specified, we need to remove the range.
    // Otherwise, we need to remove the selected range if it's not collapsed.
    if (aReplacementRange && aReplacementRange->location != NSNotFound) {
      // nothing to do since the range is collapsed.
      if (aReplacementRange->length == 0) {
        return;
      }
      // If the replacement range is different from current selected range,
      // select the range.
      if (!NSEqualRanges(selectedRange, *aReplacementRange)) {
        NS_ENSURE_TRUE_VOID(SetSelection(*aReplacementRange));
      }
      selectedRange = SelectedRange();
    }
    NS_ENSURE_TRUE_VOID(selectedRange.location != NSNotFound);
    if (selectedRange.length == 0) {
      return;  // nothing to do
    }
    // If this is caused by a key input, the keypress event which will be
    // dispatched later should cause the delete.  Therefore, nothing to do here.
    // Although, we're not sure if such case is actually possible.
    if (!currentKeyEvent) {
      return;
    }

    // When current keydown event causes this empty text input, let's
    // dispatch eKeyDown event before any other events.  Note that if we're
    // in a composition, we've already dispatched eKeyDown event from
    // TextInputHandler::HandleKeyDownEvent().
    // XXX Should we mark this eKeyDown event as "processed by IME"?
    RefPtr<TextInputHandler> kungFuDeathGrip(this);
    if (!IsIMEComposing() && !MaybeDispatchCurrentKeydownEvent(false)) {
      MOZ_LOG_KEY_OR_IME(
          LogLevel::Info,
          ("%p   TextInputHandler::InsertText, eKeyDown caused focus move or "
           "something and canceling the composition",
           this));
      return;
    }

    // Delete the selected range.
    const RefPtr<TextEventDispatcher> dispatcher(mDispatcher);
    MOZ_ASSERT(dispatcher);
    const Result<bool, nsresult> deleteCommandResult =
        dispatcher->DispatchContentCommandEvent(eContentCommandDelete);
    NS_ENSURE_TRUE_VOID(deleteCommandResult.isOk());
    // Be aware! The widget might be destroyed here.
    return;
  }

  bool isReplacingSpecifiedRange =
      isEditable && aReplacementRange &&
      aReplacementRange->location != NSNotFound &&
      !NSEqualRanges(selectedRange, *aReplacementRange);

  // If this is not caused by pressing a key, there is a composition or
  // replacing a range which is different from current selection, let's
  // insert the text as committing a composition.
  // If InsertText() is called two or more times, we should insert all
  // text with composition events.
  // XXX When InsertText() is called multiple times, Chromium dispatches
  //     only one composition event.  So, we need to store InsertText()
  //     calls and flush later.
  if (!currentKeyEvent || currentKeyEvent->mCompositionDispatched ||
      IsIMEComposing() || isReplacingSpecifiedRange) {
    InsertTextAsCommittingComposition(aString, aReplacementRange);
    if (currentKeyEvent) {
      currentKeyEvent->mCompositionDispatched = true;
    }
    return;
  }

  // Don't let the same event be fired twice when hitting
  // enter/return for Bug 420502.  However, Korean IME (or some other
  // simple IME) may work without marked text.  For example, composing
  // character may be inserted as committed text and it's modified with
  // aReplacementRange.  When a keydown starts new composition with
  // committing previous character, InsertText() may be called twice,
  // one is for committing previous character and then, inserting new
  // composing character as committed character.  In the latter case,
  // |CanDispatchKeyPressEvent()| returns true but we need to dispatch
  // keypress event for the new character.  So, when IME tries to insert
  // printable characters, we should ignore current key event state even
  // after the keydown has already caused dispatching composition event.
  // XXX Anyway, we should sort out around this at fixing bug 1338460.
  if (currentKeyEvent && !currentKeyEvent->CanDispatchKeyPressEvent() &&
      (str.IsEmpty() || (str.Length() == 1 && !IsPrintableChar(str[0])))) {
    return;
  }

  // This is the normal path to input a character when you press a key.
  // Let's dispatch eKeyDown event now.
  RefPtr<TextInputHandler> kungFuDeathGrip(this);
  if (!MaybeDispatchCurrentKeydownEvent(false)) {
    MOZ_LOG_KEY_OR_IME(
        LogLevel::Info,
        ("%p   TextInputHandler::InsertText, eKeyDown caused focus move or "
         "something and canceling the composition",
         this));
    return;
  }

  // XXX Shouldn't we hold mDispatcher instead of mWidget?
  RefPtr<nsCocoaWindow> widget(mWidget);
  nsresult rv = mDispatcher->BeginNativeInputTransaction();
  if (NS_WARN_IF(NS_FAILED(rv))) {
    MOZ_LOG_KEY_OR_IME(LogLevel::Error,
                       ("%p   TextInputHandler::InsertText, "
                        "FAILED, due to BeginNativeInputTransaction() failure",
                        this));
    return;
  }

  MOZ_LOG_KEY_OR_IME(LogLevel::Info, ("%p   TextInputHandler::InsertText, "
                                      "maybe dispatches eKeyPress event "
                                      "without control, alt and meta modifiers",
                                      this));

  // Dispatch keypress event with char instead of compositionchange event
  WidgetKeyboardEvent keypressEvent(true, eKeyPress, widget);
  // XXX Why do we need to dispatch keypress event for not inputting any
  //     string?  If it wants to delete the specified range, should we
  //     dispatch an eContentCommandDelete event instead?  Because this
  //     must not be caused by a key operation, a part of IME's processing.

  // Don't set other modifiers from the current event, because here in
  // -insertText: they've already been taken into account in creating
  // the input string.

  if (currentKeyEvent) {
    currentKeyEvent->InitKeyEvent(this, keypressEvent, false);
  } else {
    nsCocoaUtils::InitInputEvent(keypressEvent, static_cast<NSEvent*>(nullptr));
    keypressEvent.mKeyNameIndex = KEY_NAME_INDEX_USE_STRING;
    keypressEvent.mKeyValue = str;
    // FYI: TextEventDispatcher will set mKeyCode to 0 for printable key's
    //      keypress events even if they don't cause inputting non-empty string.
  }

  // TODO:
  // If mCurrentKeyEvent.mKeyEvent is null, the text should be inputted as
  // composition events.
  nsEventStatus status = nsEventStatus_eIgnore;
  bool keyPressDispatched = [&]() {
    // If text content is chrome process, OnTextChange etc will be dispatched
    // synchronously. We don't want to dismiss text substitution panel at this
    // point.
    mPendingDismissTextSubstitution = false;
    AutoRestore<bool> block(mBlockDismissTextSubstitutionPanel);
    mBlockDismissTextSubstitutionPanel = true;

    return mDispatcher->MaybeDispatchKeypressEvents(keypressEvent, status,
                                                    currentKeyEvent);
  }();
  bool keyPressHandled = (status == nsEventStatus_eConsumeNoDefault);

  if (keyPressDispatched) {
    // WebKit and text editor dismisses autocorrect panel by space, then process
    // autocorrect.
    const bool isSpaceKeyPress = (keypressEvent.mKeyCode == NS_VK_SPACE);
    // If not space key, we don't process text substitution.
    mProcessTextSubstitution = isSpaceKeyPress;

    if (isSpaceKeyPress || mPendingDismissTextSubstitution) {
      DismissTextSubstitutionPanel();
    }
  }

  // Note: mWidget might have become null here. Don't count on it from here on.

  if (currentKeyEvent) {
    currentKeyEvent->mKeyPressHandled = keyPressHandled;
    currentKeyEvent->mKeyPressDispatched = keyPressDispatched;
  }

  NS_OBJC_END_TRY_IGNORE_BLOCK;
}

bool TextInputHandler::HandleCommand(Command aCommand) {
  NS_OBJC_BEGIN_TRY_BLOCK_RETURN;

  if (Destroyed()) {
    return false;
  }

  KeyEventState* currentKeyEvent = GetCurrentKeyEvent();

  MOZ_LOG_KEY_OR_IME(
      LogLevel::Info,
      ("%p TextInputHandler::HandleCommand, "
       "aCommand=%s, IsIMEComposing()=%s, "
       "keyevent=%p, keydownHandled=%s, keypressDispatched=%s, "
       "causedOtherKeyEvents=%s, compositionDispatched=%s",
       this, ToChar(aCommand), TrueOrFalse(IsIMEComposing()),
       currentKeyEvent ? currentKeyEvent->mKeyEvent : nullptr,
       currentKeyEvent ? TrueOrFalse(currentKeyEvent->mKeyDownHandled) : "N/A",
       currentKeyEvent ? TrueOrFalse(currentKeyEvent->mKeyPressDispatched)
                       : "N/A",
       currentKeyEvent ? TrueOrFalse(currentKeyEvent->mCausedOtherKeyEvents)
                       : "N/A",
       currentKeyEvent ? TrueOrFalse(currentKeyEvent->mCompositionDispatched)
                       : "N/A"));

  // The command shouldn't be handled, let's ignore it.
  if (currentKeyEvent && !currentKeyEvent->CanHandleCommand()) {
    return false;
  }

  // When current keydown event causes this command, let's dispatch
  // eKeyDown event before any other events.  Note that if we're in a
  // composition, we've already dispatched eKeyDown event from
  // TextInputHandler::HandleKeyDownEvent().
  RefPtr<TextInputHandler> kungFuDeathGrip(this);
  if (!IsIMEComposing() && !MaybeDispatchCurrentKeydownEvent(false)) {
    MOZ_LOG_KEY_OR_IME(
        LogLevel::Info,
        ("%p   TextInputHandler::SetMarkedText, eKeyDown caused focus move or "
         "something and canceling the composition",
         this));
    return false;
  }

  // If it's in composition, we cannot dispatch keypress event.
  // Therefore, we should use different approach or give up to handle
  // the command.
  if (IsIMEComposing()) {
    switch (aCommand) {
      case Command::InsertLineBreak:
      case Command::InsertParagraph: {
        // Insert '\n' as committing composition.
        // Otherwise, we need to dispatch keypress event because HTMLEditor
        // doesn't treat "\n" in composition string as a line break unless
        // the whitespace is treated as pre (see bug 1350541).  In strictly
        // speaking, we should dispatch keypress event as-is if it's handling
        // NSEventTypeKeyDown event or should insert it with committing
        // composition.
        InsertTextAsCommittingComposition(@"\n", nullptr);
        if (currentKeyEvent) {
          currentKeyEvent->mCompositionDispatched = true;
        }
        return true;
      }
      case Command::DeleteCharBackward:
      case Command::DeleteCharForward:
      case Command::DeleteToBeginningOfLine:
      case Command::DeleteWordBackward:
      case Command::DeleteWordForward:
        // Don't remove any contents during composition.
        return false;
      case Command::InsertTab:
      case Command::InsertBacktab:
        // Don't move focus during composition.
        return false;
      case Command::CharNext:
      case Command::SelectCharNext:
      case Command::WordNext:
      case Command::SelectWordNext:
      case Command::EndLine:
      case Command::SelectEndLine:
      case Command::MoveRight3:
      case Command::SelectRight3:
      case Command::CharPrevious:
      case Command::SelectCharPrevious:
      case Command::WordPrevious:
      case Command::SelectWordPrevious:
      case Command::BeginLine:
      case Command::SelectBeginLine:
      case Command::MoveLeft3:
      case Command::SelectLeft3:
      case Command::LinePrevious:
      case Command::SelectLinePrevious:
      case Command::MoveTop:
      case Command::LineNext:
      case Command::SelectLineNext:
      case Command::MoveBottom:
      case Command::SelectBottom:
      case Command::SelectPageUp:
      case Command::SelectPageDown:
      case Command::ScrollBottom:
      case Command::ScrollTop:
        // Don't move selection during composition.
        return false;
      case Command::CancelOperation:
      case Command::Complete:
        // Don't handle Escape key by ourselves during composition.
        return false;
      case Command::ScrollPageUp:
      case Command::ScrollPageDown:
        // Allow to scroll.
        break;
      default:
        break;
    }
  }

  RefPtr<nsCocoaWindow> widget(mWidget);
  nsresult rv = mDispatcher->BeginNativeInputTransaction();
  if (NS_WARN_IF(NS_FAILED(rv))) {
    MOZ_LOG_KEY_OR_IME(LogLevel::Error,
                       ("%p,   TextInputHandler::HandleCommand, "
                        "FAILED, due to BeginNativeInputTransaction() failure",
                        this));
    return false;
  }

  // TODO: If it's not appropriate keypress but user customized the OS
  //       settings to do the command with other key, we should just set
  //       command to the keypress event and it should be handled as
  //       the key press in editor.

  // If it's handling actual key event and hasn't caused any composition
  // events nor other key events, we should expose actual modifier state.
  // Otherwise, we should adjust Control, Option and Command state since
  // editor may behave differently if some of them are active.
  bool dispatchFakeKeyPress =
      !(currentKeyEvent && currentKeyEvent->IsProperKeyEvent(aCommand));

  WidgetKeyboardEvent keydownEvent(true, eKeyDown, widget);
  WidgetKeyboardEvent keypressEvent(true, eKeyPress, widget);
  if (!dispatchFakeKeyPress) {
    // If we're actually handling a key press, we should dispatch
    // the keypress event as-is.
    currentKeyEvent->InitKeyEvent(this, keydownEvent, false);
    currentKeyEvent->InitKeyEvent(this, keypressEvent, false);
  } else {
    // Otherwise, we should dispatch "fake" keypress event.
    // However, for making it possible to compute edit commands, we need to
    // set current native key event to the fake keyboard event even if it's
    // not same as what we expect since the native keyboard event caused
    // this command.
    NSEvent* keyEvent = currentKeyEvent ? currentKeyEvent->mKeyEvent : nullptr;
    keydownEvent.mNativeKeyEvent = keypressEvent.mNativeKeyEvent = keyEvent;
    NS_WARNING_ASSERTION(
        keypressEvent.mNativeKeyEvent,
        "Without native key event, NativeKeyBindings cannot compute aCommand");
    switch (aCommand) {
      case Command::InsertLineBreak:
      case Command::InsertParagraph: {
        // Although, Shift+Enter and Enter are work differently in HTML
        // editor, we should expose actual Shift state if it's caused by
        // Enter key for compatibility with Chromium.  Chromium breaks
        // line in HTML editor with default pargraph separator when Enter
        // is pressed, with <br> element when Shift+Enter.  Safari breaks
        // line in HTML editor with default paragraph separator when
        // Enter, Shift+Enter or Option+Enter.  So, we should not change
        // Shift+Enter meaning when there was composition string or not.
        nsCocoaUtils::InitInputEvent(keypressEvent, keyEvent);
        keypressEvent.mKeyCode = NS_VK_RETURN;
        keypressEvent.mKeyNameIndex = KEY_NAME_INDEX_Enter;
        keypressEvent.mModifiers &=
            ~(MODIFIER_CONTROL | MODIFIER_ALT | MODIFIER_META);
        if (aCommand == Command::InsertLineBreak) {
          // In default settings, Ctrl + Enter causes insertLineBreak command.
          // So, let's make Ctrl state active of the keypress event.
          keypressEvent.mModifiers |= MODIFIER_CONTROL;
        }
        break;
      }
      case Command::InsertTab:
      case Command::InsertBacktab:
        nsCocoaUtils::InitInputEvent(keypressEvent, keyEvent);
        keypressEvent.mKeyCode = NS_VK_TAB;
        keypressEvent.mKeyNameIndex = KEY_NAME_INDEX_Tab;
        keypressEvent.mModifiers &=
            ~(MODIFIER_CONTROL | MODIFIER_ALT | MODIFIER_META);
        if (aCommand == Command::InsertBacktab) {
          keypressEvent.mModifiers |= MODIFIER_SHIFT;
        }
        break;
      case Command::DeleteCharBackward:
      case Command::DeleteToBeginningOfLine:
      case Command::DeleteWordBackward: {
        nsCocoaUtils::InitInputEvent(keypressEvent, keyEvent);
        keypressEvent.mKeyCode = NS_VK_BACK;
        keypressEvent.mKeyNameIndex = KEY_NAME_INDEX_Backspace;
        keypressEvent.mModifiers &=
            ~(MODIFIER_CONTROL | MODIFIER_ALT | MODIFIER_META);
        if (aCommand == Command::DeleteToBeginningOfLine) {
          keypressEvent.mModifiers |= MODIFIER_META;
        } else if (aCommand == Command::DeleteWordBackward) {
          keypressEvent.mModifiers |= MODIFIER_ALT;
        }
        break;
      }
      case Command::DeleteCharForward:
      case Command::DeleteWordForward: {
        nsCocoaUtils::InitInputEvent(keypressEvent, keyEvent);
        keypressEvent.mKeyCode = NS_VK_DELETE;
        keypressEvent.mKeyNameIndex = KEY_NAME_INDEX_Delete;
        keypressEvent.mModifiers &=
            ~(MODIFIER_CONTROL | MODIFIER_ALT | MODIFIER_META);
        if (aCommand == Command::DeleteWordForward) {
          keypressEvent.mModifiers |= MODIFIER_ALT;
        }
        break;
      }
      case Command::CharNext:
      case Command::SelectCharNext:
      case Command::WordNext:
      case Command::SelectWordNext:
      case Command::EndLine:
      case Command::SelectEndLine:
      case Command::MoveRight3:
      case Command::SelectRight3: {
        nsCocoaUtils::InitInputEvent(keypressEvent, keyEvent);
        keypressEvent.mKeyCode = NS_VK_RIGHT;
        keypressEvent.mKeyNameIndex = KEY_NAME_INDEX_ArrowRight;
        keypressEvent.mModifiers &=
            ~(MODIFIER_CONTROL | MODIFIER_ALT | MODIFIER_META);
        if (aCommand == Command::SelectCharNext ||
            aCommand == Command::SelectWordNext ||
            aCommand == Command::SelectEndLine ||
            aCommand == Command::SelectRight3) {
          keypressEvent.mModifiers |= MODIFIER_SHIFT;
        }
        if (aCommand == Command::WordNext ||
            aCommand == Command::SelectWordNext) {
          keypressEvent.mModifiers |= MODIFIER_ALT;
        }
        if (aCommand == Command::EndLine ||
            aCommand == Command::SelectEndLine ||
            aCommand == Command::MoveRight3 ||
            aCommand == Command::SelectRight3) {
          keypressEvent.mModifiers |= MODIFIER_META;
        }
        break;
      }
      case Command::CharPrevious:
      case Command::SelectCharPrevious:
      case Command::WordPrevious:
      case Command::SelectWordPrevious:
      case Command::BeginLine:
      case Command::SelectBeginLine:
      case Command::MoveLeft3:
      case Command::SelectLeft3: {
        nsCocoaUtils::InitInputEvent(keypressEvent, keyEvent);
        keypressEvent.mKeyCode = NS_VK_LEFT;
        keypressEvent.mKeyNameIndex = KEY_NAME_INDEX_ArrowLeft;
        keypressEvent.mModifiers &=
            ~(MODIFIER_CONTROL | MODIFIER_ALT | MODIFIER_META);
        if (aCommand == Command::SelectCharPrevious ||
            aCommand == Command::SelectWordPrevious ||
            aCommand == Command::SelectBeginLine ||
            aCommand == Command::SelectLeft3) {
          keypressEvent.mModifiers |= MODIFIER_SHIFT;
        }
        if (aCommand == Command::WordPrevious ||
            aCommand == Command::SelectWordPrevious) {
          keypressEvent.mModifiers |= MODIFIER_ALT;
        }
        if (aCommand == Command::BeginLine ||
            aCommand == Command::SelectBeginLine ||
            aCommand == Command::MoveLeft3 ||
            aCommand == Command::SelectLeft3) {
          keypressEvent.mModifiers |= MODIFIER_META;
        }
        break;
      }
      case Command::LinePrevious:
      case Command::SelectLinePrevious:
      case Command::MoveTop:
      case Command::SelectTop: {
        nsCocoaUtils::InitInputEvent(keypressEvent, keyEvent);
        keypressEvent.mKeyCode = NS_VK_UP;
        keypressEvent.mKeyNameIndex = KEY_NAME_INDEX_ArrowUp;
        keypressEvent.mModifiers &=
            ~(MODIFIER_CONTROL | MODIFIER_ALT | MODIFIER_META);
        if (aCommand == Command::SelectLinePrevious ||
            aCommand == Command::SelectTop) {
          keypressEvent.mModifiers |= MODIFIER_SHIFT;
        }
        if (aCommand == Command::MoveTop || aCommand == Command::SelectTop) {
          keypressEvent.mModifiers |= MODIFIER_META;
        }
        break;
      }
      case Command::LineNext:
      case Command::SelectLineNext:
      case Command::MoveBottom:
      case Command::SelectBottom: {
        nsCocoaUtils::InitInputEvent(keypressEvent, keyEvent);
        keypressEvent.mKeyCode = NS_VK_DOWN;
        keypressEvent.mKeyNameIndex = KEY_NAME_INDEX_ArrowDown;
        keypressEvent.mModifiers &=
            ~(MODIFIER_CONTROL | MODIFIER_ALT | MODIFIER_META);
        if (aCommand == Command::SelectLineNext ||
            aCommand == Command::SelectBottom) {
          keypressEvent.mModifiers |= MODIFIER_SHIFT;
        }
        if (aCommand == Command::MoveBottom ||
            aCommand == Command::SelectBottom) {
          keypressEvent.mModifiers |= MODIFIER_META;
        }
        break;
      }
      case Command::ScrollPageUp:
      case Command::SelectPageUp: {
        nsCocoaUtils::InitInputEvent(keypressEvent, keyEvent);
        keypressEvent.mKeyCode = NS_VK_PAGE_UP;
        keypressEvent.mKeyNameIndex = KEY_NAME_INDEX_PageUp;
        keypressEvent.mModifiers &=
            ~(MODIFIER_CONTROL | MODIFIER_ALT | MODIFIER_META);
        if (aCommand == Command::SelectPageUp) {
          keypressEvent.mModifiers |= MODIFIER_SHIFT;
        }
        break;
      }
      case Command::ScrollPageDown:
      case Command::SelectPageDown: {
        nsCocoaUtils::InitInputEvent(keypressEvent, keyEvent);
        keypressEvent.mKeyCode = NS_VK_PAGE_DOWN;
        keypressEvent.mKeyNameIndex = KEY_NAME_INDEX_PageDown;
        keypressEvent.mModifiers &=
            ~(MODIFIER_CONTROL | MODIFIER_ALT | MODIFIER_META);
        if (aCommand == Command::SelectPageDown) {
          keypressEvent.mModifiers |= MODIFIER_SHIFT;
        }
        break;
      }
      case Command::ScrollBottom:
      case Command::ScrollTop: {
        nsCocoaUtils::InitInputEvent(keypressEvent, keyEvent);
        if (aCommand == Command::ScrollBottom) {
          keypressEvent.mKeyCode = NS_VK_END;
          keypressEvent.mKeyNameIndex = KEY_NAME_INDEX_End;
        } else {
          keypressEvent.mKeyCode = NS_VK_HOME;
          keypressEvent.mKeyNameIndex = KEY_NAME_INDEX_Home;
        }
        keypressEvent.mModifiers &=
            ~(MODIFIER_CONTROL | MODIFIER_ALT | MODIFIER_META);
        break;
      }
      case Command::CancelOperation:
      case Command::Complete: {
        nsCocoaUtils::InitInputEvent(keypressEvent, keyEvent);
        keypressEvent.mKeyCode = NS_VK_ESCAPE;
        keypressEvent.mKeyNameIndex = KEY_NAME_INDEX_Escape;
        keypressEvent.mModifiers &=
            ~(MODIFIER_CONTROL | MODIFIER_ALT | MODIFIER_META);
        if (aCommand == Command::Complete) {
          keypressEvent.mModifiers |= MODIFIER_ALT;
        }
        break;
      }
      default:
        return false;
    }

    nsCocoaUtils::InitInputEvent(keydownEvent, keyEvent);
    keydownEvent.mKeyCode = keypressEvent.mKeyCode;
    keydownEvent.mKeyNameIndex = keypressEvent.mKeyNameIndex;
    keydownEvent.mModifiers = keypressEvent.mModifiers;
  }

  // We've stopped dispatching "keypress" events of non-printable keys on
  // the web.  Therefore, we need to dispatch eKeyDown event here for web
  // apps.  This is non-standard behavior if we've already dispatched a
  // "keydown" event.  However, Chrome also dispatches such fake "keydown"
  // (and "keypress") event for making same behavior as Safari.
  nsEventStatus status = nsEventStatus_eIgnore;
  if (mDispatcher->DispatchKeyboardEvent(eKeyDown, keydownEvent, status,
                                         nullptr)) {
    bool keydownHandled = status == nsEventStatus_eConsumeNoDefault;
    if (currentKeyEvent) {
      currentKeyEvent->mKeyDownDispatched = true;
      currentKeyEvent->mKeyDownHandled |= keydownHandled;
    }
    if (keydownHandled) {
      // Don't dispatch eKeyPress event if preceding eKeyDown event is
      // consumed for conforming to UI Events.
      // XXX Perhaps, we should ignore previous eKeyDown event result
      //     even if we've already dispatched because it may notify web apps
      //     of different key information, e.g., it's handled by IME, but
      //     web apps want to handle only this key.
      return true;
    }
  }

  bool keyPressDispatched = mDispatcher->MaybeDispatchKeypressEvents(
      keypressEvent, status, currentKeyEvent);
  bool keyPressHandled = (status == nsEventStatus_eConsumeNoDefault);

  // NOTE: mWidget might have become null here.

  if (keyPressDispatched) {
    // Record the keypress event state only when it dispatched actual Enter
    // keypress event because in other cases, the keypress event just a
    // messenger.  E.g., if it's caused by different key, keypress event for
    // the actual key should be dispatched.
    if (!dispatchFakeKeyPress && currentKeyEvent) {
      currentKeyEvent->mKeyPressHandled = keyPressHandled;
      currentKeyEvent->mKeyPressDispatched = keyPressDispatched;
    }
    return true;
  }

  // If keypress event isn't dispatched as expected, we should fallback to
  // using composition events.
  if (aCommand == Command::InsertLineBreak ||
      aCommand == Command::InsertParagraph) {
    InsertTextAsCommittingComposition(@"\n", nullptr);
    if (currentKeyEvent) {
      currentKeyEvent->mCompositionDispatched = true;
    }
    return true;
  }

  return false;

  NS_OBJC_END_TRY_BLOCK_RETURN(false);
}

bool TextInputHandler::DoCommandBySelector(const char* aSelector) {
  RefPtr<nsCocoaWindow> widget(mWidget);

  KeyEventState* currentKeyEvent = GetCurrentKeyEvent();

  MOZ_LOG_KEY_OR_IME(
      LogLevel::Info,
      ("%p TextInputHandler::DoCommandBySelector, aSelector=\"%s\", "
       "Destroyed()=%s, keydownDispatched=%s, keydownHandled=%s, "
       "keypressDispatched=%s, keypressHandled=%s, causedOtherKeyEvents=%s",
       this, aSelector ? aSelector : "", TrueOrFalse(Destroyed()),
       currentKeyEvent ? TrueOrFalse(currentKeyEvent->mKeyDownDispatched)
                       : "N/A",
       currentKeyEvent ? TrueOrFalse(currentKeyEvent->mKeyDownHandled) : "N/A",
       currentKeyEvent ? TrueOrFalse(currentKeyEvent->mKeyPressDispatched)
                       : "N/A",
       currentKeyEvent ? TrueOrFalse(currentKeyEvent->mKeyPressHandled) : "N/A",
       currentKeyEvent ? TrueOrFalse(currentKeyEvent->mCausedOtherKeyEvents)
                       : "N/A"));

  // If the command isn't caused by key operation, the command should
  // be handled in the super class of the caller.
  if (!currentKeyEvent) {
    return Destroyed();
  }

  // When current keydown event causes this command, let's dispatch
  // eKeyDown event before any other events.  Note that if we're in a
  // composition, we've already dispatched eKeyDown event from
  // TextInputHandler::HandleKeyDownEvent().
  RefPtr<TextInputHandler> kungFuDeathGrip(this);
  if (!IsIMEComposing() && !MaybeDispatchCurrentKeydownEvent(false)) {
    MOZ_LOG_KEY_OR_IME(
        LogLevel::Info,
        ("%p   TextInputHandler::SetMarkedText, eKeyDown caused focus move or "
         "something and canceling the composition",
         this));
    return true;
  }

  // If the key operation causes this command, should dispatch a keypress
  // event.
  // XXX This must be worng.  Even if this command is caused by the key
  //     operation, its our default action can be different from the
  //     command.  So, in this case, we should dispatch a keypress event
  //     which have the command and editor should handle it.
  if (currentKeyEvent->CanDispatchKeyPressEvent()) {
    nsresult rv = mDispatcher->BeginNativeInputTransaction();
    if (NS_WARN_IF(NS_FAILED(rv))) {
      MOZ_LOG_KEY_OR_IME(
          LogLevel::Error,
          ("%p   TextInputHandler::DoCommandBySelector, "
           "FAILED, due to BeginNativeInputTransaction() failure "
           "at dispatching keypress",
           this));
      return Destroyed();
    }

    WidgetKeyboardEvent keypressEvent(true, eKeyPress, widget);
    currentKeyEvent->InitKeyEvent(this, keypressEvent, false);

    nsEventStatus status = nsEventStatus_eIgnore;
    // If text content is chrome process, OnTextChange etc will be dispatched
    // synchronously. We don't want to dismiss text substitution panel at this
    // point.
    {
      mPendingDismissTextSubstitution = false;
      AutoRestore<bool> block(mBlockDismissTextSubstitutionPanel);
      mBlockDismissTextSubstitutionPanel = true;

      currentKeyEvent->mKeyPressDispatched =
          mDispatcher->MaybeDispatchKeypressEvents(keypressEvent, status,
                                                   currentKeyEvent);
    }
    currentKeyEvent->mKeyPressHandled =
        (status == nsEventStatus_eConsumeNoDefault);
    MOZ_LOG_KEY_OR_IME(
        LogLevel::Info,
        ("%p   TextInputHandler::DoCommandBySelector, keypress event "
         "dispatched, Destroyed()=%s, keypressHandled=%s",
         this, TrueOrFalse(Destroyed()),
         TrueOrFalse(currentKeyEvent->mKeyPressHandled)));

    if (currentKeyEvent->mKeyPressDispatched) {
      // WebKit and text editor dismisses autocorrect panel by enter, then
      // process autocorrect.
      const bool isEnterKeyPress = (keypressEvent.mKeyCode == NS_VK_RETURN);
      // If not enterkey key, we don't process text substitution.
      mProcessTextSubstitution = isEnterKeyPress;

      if (isEnterKeyPress || mPendingDismissTextSubstitution) {
        DismissTextSubstitutionPanel();
      }
    }

    // This command is now dispatched with keypress event.
    // So, this shouldn't be handled by nobody anymore.
    return true;
  }

  // If the key operation didn't cause keypress event or caused keypress event
  // but not prevented its default, we need to honor the command.  For example,
  // Korean IME sends "insertNewline:" when committing existing composition
  // with Enter key press.  In such case, the key operation has been consumed
  // by the committing composition but we still need to handle the command.
  if (Destroyed() || !currentKeyEvent->CanHandleCommand()) {
    return true;
  }

  // cancelOperation: command is fired after Escape or Command + Period.
  // However, if ChildView implements cancelOperation:, calling
  // [[ChildView super] doCommandBySelector:aSelector] when Command + Period
  // causes only a call of [ChildView cancelOperation:sender].  I.e.,
  // [ChildView keyDown:theEvent] becomes to be never called.  For avoiding
  // this odd behavior, we need to handle the command before super class of
  // ChildView only when current key event is proper event to fire Escape
  // keypress event.
  if (!strcmp(aSelector, "cancelOperation:") && currentKeyEvent &&
      currentKeyEvent->IsProperKeyEvent(Command::CancelOperation)) {
    return HandleCommand(Command::CancelOperation);
  }

  // Otherwise, we've not handled the command yet.  Propagate the command
  // to the super class of ChildView.
  return false;
}

#pragma mark -

/******************************************************************************
 *
 *  IMEInputHandler implementation (static methods)
 *
 ******************************************************************************/

bool IMEInputHandler::sStaticMembersInitialized = false;
bool IMEInputHandler::sCachedIsForRTLLanguage = false;
CFStringRef IMEInputHandler::sLatestIMEOpenedModeInputSourceID = nullptr;
IMEInputHandler* IMEInputHandler::sFocusedIMEHandler = nullptr;

// static
void IMEInputHandler::InitStaticMembers() {
  if (sStaticMembersInitialized) return;
  sStaticMembersInitialized = true;
  // We need to check the keyboard layout changes on all applications.
  CFNotificationCenterRef center = ::CFNotificationCenterGetDistributedCenter();
  // XXX Don't we need to remove the observer at shut down?
  // Mac Dev Center's document doesn't say how to remove the observer if
  // the second parameter is NULL.
  ::CFNotificationCenterAddObserver(
      center, nullptr, OnCurrentTextInputSourceChange,
      kTISNotifySelectedKeyboardInputSourceChanged, nullptr,
      CFNotificationSuspensionBehaviorDeliverImmediately);
  // Initiailize with the current keyboard layout
  OnCurrentTextInputSourceChange(nullptr, nullptr,
                                 kTISNotifySelectedKeyboardInputSourceChanged,
                                 nullptr, nullptr);
}

// static
void IMEInputHandler::OnCurrentTextInputSourceChange(
    CFNotificationCenterRef aCenter, void* aObserver, CFStringRef aName,
    const void* aObject, CFDictionaryRef aUserInfo) {
  // Cache the latest IME opened mode to sLatestIMEOpenedModeInputSourceID.
  TISInputSourceWrapper tis;
  tis.InitByCurrentInputSource();
  if (tis.IsOpenedIMEMode()) {
    tis.GetInputSourceID(sLatestIMEOpenedModeInputSourceID);
    // Collect Input Source ID which includes input mode in most cases.
    // However, if it's Japanese IME, collecting input mode (e.g.,
    // "HiraganaKotei") does not make sense because in most languages,
    // input mode changes "how to input", but Japanese IME changes
    // "which type of characters to input".  I.e., only Japanese IME
    // users may use multiple input modes.  If we'd collect each type of
    // input mode of Japanese IMEs, it'd be difficult to count actual
    // users of each IME from the result.  So, only when active IME is
    // a Japanese IME, we should use Bundle ID which does not contain
    // input mode instead.
    nsAutoString key;
    if (tis.IsForJapaneseLanguage()) {
      tis.GetBundleID(key);
    } else {
      tis.GetInputSourceID(key);
    }
    // 72 is kMaximumKeyStringLength in TelemetryScalar.cpp
    if (key.Length() > 72) {
      if (IsSurrogatePair(key[72 - 2], key[72 - 1])) {
        key.Truncate(72 - 2);
      } else {
        key.Truncate(72 - 1);
      }
      // U+2026 is "..."
      key.Append(char16_t(0x2026));
    }
    glean::widget::ime_name_on_mac.Get(NS_ConvertUTF16toUTF8(key)).Set(true);
  }

  if (MOZ_LOG_TEST(gIMELog, LogLevel::Info)) {
    static CFStringRef sLastTIS = nullptr;
    CFStringRef newTIS;
    tis.GetInputSourceID(newTIS);
    if (!sLastTIS ||
        ::CFStringCompare(sLastTIS, newTIS, 0) != kCFCompareEqualTo) {
      TISInputSourceWrapper tis1, tis2, tis3, tis4, tis5;
      tis1.InitByCurrentKeyboardLayout();
      tis2.InitByCurrentASCIICapableInputSource();
      tis3.InitByCurrentASCIICapableKeyboardLayout();
      tis4.InitByCurrentInputMethodKeyboardLayoutOverride();
      tis5.InitByTISInputSourceRef(tis.GetKeyboardLayoutInputSource());
      CFStringRef is0 = nullptr, is1 = nullptr, is2 = nullptr, is3 = nullptr,
                  is4 = nullptr, is5 = nullptr, type0 = nullptr,
                  lang0 = nullptr, bundleID0 = nullptr;
      tis.GetInputSourceID(is0);
      tis1.GetInputSourceID(is1);
      tis2.GetInputSourceID(is2);
      tis3.GetInputSourceID(is3);
      tis4.GetInputSourceID(is4);
      tis5.GetInputSourceID(is5);
      tis.GetInputSourceType(type0);
      tis.GetPrimaryLanguage(lang0);
      tis.GetBundleID(bundleID0);

      MOZ_LOG(gIMELog, LogLevel::Info,
              ("IMEInputHandler::OnCurrentTextInputSourceChange,\n"
               "  Current Input Source is changed to:\n"
               "    currentInputContext=%p\n"
               "    %s\n"
               "      type=%s %s\n"
               "      overridden keyboard layout=%s\n"
               "      used keyboard layout for translation=%s\n"
               "    primary language=%s\n"
               "    bundle ID=%s\n"
               "    current ASCII capable Input Source=%s\n"
               "    current Keyboard Layout=%s\n"
               "    current ASCII capable Keyboard Layout=%s",
               [NSTextInputContext currentInputContext],
               GetCharacters(is0).get(), GetCharacters(type0).get(),
               tis.IsASCIICapable() ? "- ASCII capable " : "",
               GetCharacters(is4).get(), GetCharacters(is5).get(),
               GetCharacters(lang0).get(), GetCharacters(bundleID0).get(),
               GetCharacters(is2).get(), GetCharacters(is1).get(),
               GetCharacters(is3).get()));
    }
    sLastTIS = newTIS;
  }

  /**
   * When the direction is changed, all the children are notified.
   * No need to treat the initial case separately because it is covered
   * by the general case (sCachedIsForRTLLanguage is initially false)
   */
  if (sCachedIsForRTLLanguage != tis.IsForRTLLanguage()) {
    WidgetUtils::SendBidiKeyboardInfoToContent();
    sCachedIsForRTLLanguage = tis.IsForRTLLanguage();
  }
}

// static
void IMEInputHandler::FlushPendingMethods(nsITimer* aTimer, void* aClosure) {
  NS_ASSERTION(aClosure, "aClosure is null");
  static_cast<IMEInputHandler*>(aClosure)->ExecutePendingMethods();
}

// static
CFArrayRef IMEInputHandler::CreateAllIMEModeList() {
  const void* keys[] = {kTISPropertyInputSourceType};
  const void* values[] = {kTISTypeKeyboardInputMode};
  CFDictionaryRef filter = ::CFDictionaryCreate(kCFAllocatorDefault, keys,
                                                values, 1, nullptr, nullptr);
  NS_ASSERTION(filter, "failed to create the filter");
  CFArrayRef list = ::TISCreateInputSourceList(filter, true);
  ::CFRelease(filter);
  return list;
}

// static
void IMEInputHandler::DebugPrintAllIMEModes() {
  if (MOZ_LOG_TEST(gIMELog, LogLevel::Info)) {
    CFArrayRef list = CreateAllIMEModeList();
    MOZ_LOG(gIMELog, LogLevel::Info, ("IME mode configuration:"));
    CFIndex idx = ::CFArrayGetCount(list);
    TISInputSourceWrapper tis;
    for (CFIndex i = 0; i < idx; ++i) {
      TISInputSourceRef inputSource = static_cast<TISInputSourceRef>(
          const_cast<void*>(::CFArrayGetValueAtIndex(list, i)));
      tis.InitByTISInputSourceRef(inputSource);
      nsAutoString name, isid, bundleID;
      tis.GetLocalizedName(name);
      tis.GetInputSourceID(isid);
      tis.GetBundleID(bundleID);
      MOZ_LOG(
          gIMELog, LogLevel::Info,
          ("  %s\t<%s>%s%s\n"
           "    bundled in <%s>\n",
           NS_ConvertUTF16toUTF8(name).get(), NS_ConvertUTF16toUTF8(isid).get(),
           tis.IsASCIICapable() ? "" : "\t(Isn't ASCII capable)",
           tis.IsEnabled() ? "" : "\t(Isn't Enabled)",
           NS_ConvertUTF16toUTF8(bundleID).get()));
    }
    ::CFRelease(list);
  }
}

// static
TSMDocumentID IMEInputHandler::GetCurrentTSMDocumentID() {
  // At least on macOS 10.6.x and 10.7.x, ::TSMGetActiveDocument() has a bug.
  // The result of ::TSMGetActiveDocument() isn't modified for new active text
  // input context until [NSTextInputContext currentInputContext] is called.
  // Therefore, we need to call it here.
  [NSTextInputContext currentInputContext];
  return ::TSMGetActiveDocument();
}

#pragma mark -

/******************************************************************************
 *
 *  IMEInputHandler implementation #1
 *    The methods are related to the pending methods.  Some jobs should be
 *    run after the stack is finished, e.g, some methods cannot run the jobs
 *    during processing the focus event.  And also some other jobs should be
 *    run at the next focus event is processed.
 *    The pending methods are recorded in mPendingMethods.  They are executed
 *    by ExecutePendingMethods via FlushPendingMethods.
 *
 ******************************************************************************/

nsresult IMEInputHandler::NotifyIME(TextEventDispatcher* aTextEventDispatcher,
                                    const IMENotification& aNotification) {
  switch (aNotification.mMessage) {
    case REQUEST_TO_COMMIT_COMPOSITION:
      CommitIMEComposition();
      return NS_OK;
    case REQUEST_TO_CANCEL_COMPOSITION:
      CancelIMEComposition();
      return NS_OK;
    case NOTIFY_IME_OF_FOCUS:
      if (IsFocused()) {
        nsIWidget* widget = aTextEventDispatcher->GetWidget();
        MOZ_LOG(gIMELog, LogLevel::Debug,
                ("%p IMEInputHandler::NotifyIME(), IsFocused()=true, "
                 "widget=%p, IsPasswordEditor()=%s",
                 this, widget,
                 TrueOrFalse(widget &&
                             widget->GetInputContext().IsPasswordEditor())));
        if (widget && widget->GetInputContext().IsPasswordEditor()) {
          EnableSecureEventInput();
        } else {
          EnsureSecureEventInputDisabled();
        }
      } else if (MOZ_LOG_TEST(gIMELog, LogLevel::Debug)) {
        NS_OBJC_BEGIN_TRY_BLOCK_RETURN
        NSWindow* window = mView ? [mView window] : nil;
        MOZ_LOG(gIMELog, LogLevel::Debug,
                ("%p IMEInputHandler::NotifyIME(), IsFocused()=false, "
                 "Destroyed()=%s, mView=%p, "
                 "[mView window]=%p, firstResponder=%p, isKeyWindow=%s, "
                 "isActive=%s",
                 this, TrueOrFalse(Destroyed()), mView, window,
                 window ? [window firstResponder] : nil,
                 TrueOrFalse(window && [window isKeyWindow]),
                 TrueOrFalse([[NSApplication sharedApplication] isActive])));
        NS_OBJC_END_TRY_BLOCK_RETURN(NS_ERROR_FAILURE)
      }
      OnFocusChangeInGecko(true);
      return NS_OK;
    case NOTIFY_IME_OF_BLUR:
      OnFocusChangeInGecko(false);
      return NS_OK;
    case NOTIFY_IME_OF_SELECTION_CHANGE:
      OnSelectionChange(aNotification);
      return NS_OK;
    case NOTIFY_IME_OF_POSITION_CHANGE:
      OnLayoutChange();
      return NS_OK;
    case NOTIFY_IME_OF_TEXT_CHANGE:
      OnTextChange(aNotification);
      return NS_OK;
    default:
      return NS_ERROR_NOT_IMPLEMENTED;
  }
}

NS_IMETHODIMP_(IMENotificationRequests)
IMEInputHandler::GetIMENotificationRequests() {
  // XXX Shouldn't we move floating window which shows composition string
  //     when plugin has focus and its parent is scrolled or the window is
  //     moved?
  return {IMENotificationRequest::TextChange};
}

NS_IMETHODIMP_(void)
IMEInputHandler::OnRemovedFrom(TextEventDispatcher* aTextEventDispatcher) {
  // XXX When input transaction is being stolen by add-on, what should we do?
}

NS_IMETHODIMP_(void)
IMEInputHandler::WillDispatchKeyboardEvent(
    TextEventDispatcher* aTextEventDispatcher,
    WidgetKeyboardEvent& aKeyboardEvent, uint32_t aIndexOfKeypress,
    void* aData) {
  // If the keyboard event is not caused by a native key event, we can do
  // nothing here.
  if (!aData) {
    return;
  }

  KeyEventState* currentKeyEvent = static_cast<KeyEventState*>(aData);
  NSEvent* nativeEvent = currentKeyEvent->mKeyEvent;
  nsAString* insertString = currentKeyEvent->mInsertString;
  if (aKeyboardEvent.mMessage == eKeyPress && aIndexOfKeypress == 0 &&
      (!insertString || insertString->IsEmpty())) {
    // Inform the child process that this is an event that we want a reply
    // from.
    // XXX This should be called only when the target is a remote process.
    //     However, it's difficult to check it under widget/.
    //     So, let's do this here for now, then,
    //     EventStateManager::PreHandleEvent() will reset the flags if
    //     the event target isn't in remote process.
    aKeyboardEvent.MarkAsWaitingReplyFromRemoteProcess();
  }
  if (KeyboardLayoutOverrideRef().mOverrideEnabled) {
    TISInputSourceWrapper tis;
    tis.InitByLayoutID(KeyboardLayoutOverrideRef().mKeyboardLayout, true);
    tis.WillDispatchKeyboardEvent(nativeEvent, insertString, aIndexOfKeypress,
                                  aKeyboardEvent);
  } else {
    TISInputSourceWrapper::CurrentInputSource().WillDispatchKeyboardEvent(
        nativeEvent, insertString, aIndexOfKeypress, aKeyboardEvent);
  }

  // Remove basic modifiers from keypress event because if they are included
  // but this causes inputting text, since TextEditor won't handle eKeyPress
  // events whose ctrlKey, altKey or metaKey is true as text input.
  // Note that this hack should be used only when an editor has focus because
  // this is a hack for TextEditor and modifier key information may be
  // important for current web app.
  if (IsEditableContent() && insertString &&
      aKeyboardEvent.mMessage == eKeyPress && aKeyboardEvent.mCharCode) {
    aKeyboardEvent.mModifiers &=
        ~(MODIFIER_CONTROL | MODIFIER_ALT | MODIFIER_META);
  }
}

void IMEInputHandler::NotifyIMEOfFocusChangeInGecko() {
  NS_OBJC_BEGIN_TRY_IGNORE_BLOCK;

  MOZ_LOG(gIMELog, LogLevel::Info,
          ("%p IMEInputHandler::NotifyIMEOfFocusChangeInGecko, "
           "Destroyed()=%s, IsFocused()=%s, inputContext=%p",
           this, TrueOrFalse(Destroyed()), TrueOrFalse(IsFocused()),
           mView ? [mView inputContext] : nullptr));

  if (Destroyed()) {
    return;
  }

  if (!IsFocused()) {
    // retry at next focus event
    mPendingMethods |= kNotifyIMEOfFocusChangeInGecko;
    return;
  }

  MOZ_ASSERT(mView);
  NSTextInputContext* inputContext = [mView inputContext];
  NS_ENSURE_TRUE_VOID(inputContext);

  // When an <input> element on a XUL <panel> element gets focus from an <input>
  // element on the opener window of the <panel> element, the owner window
  // still has native focus.  Therefore, IMEs may store the opener window's
  // level at this time because they don't know the actual focus is moved to
  // different window.  If IMEs try to get the newest window level after the
  // focus change, we return the window level of the XUL <panel>'s widget.
  // Therefore, let's emulate the native focus change.  Then, IMEs can refresh
  // the stored window level.
  [inputContext deactivate];
  [inputContext activate];

  NS_OBJC_END_TRY_IGNORE_BLOCK;
}

void IMEInputHandler::SyncASCIICapableOnly() {
  NS_OBJC_BEGIN_TRY_IGNORE_BLOCK;

  MOZ_LOG(gIMELog, LogLevel::Info,
          ("%p IMEInputHandler::SyncASCIICapableOnly, "
           "Destroyed()=%s, IsFocused()=%s, mIsASCIICapableOnly=%s, "
           "GetCurrentTSMDocumentID()=%p",
           this, TrueOrFalse(Destroyed()), TrueOrFalse(IsFocused()),
           TrueOrFalse(mIsASCIICapableOnly), GetCurrentTSMDocumentID()));

  if (Destroyed()) {
    return;
  }

  if (!IsFocused()) {
    // retry at next focus event
    mPendingMethods |= kSyncASCIICapableOnly;
    return;
  }

  TSMDocumentID doc = GetCurrentTSMDocumentID();
  if (!doc) {
    // retry
    mPendingMethods |= kSyncASCIICapableOnly;
    NS_WARNING("Application is active but there is no active document");
    ResetTimer();
    return;
  }

  if (mIsASCIICapableOnly) {
    CFArrayRef ASCIICapableTISList = ::TISCreateASCIICapableInputSourceList();
    ::TSMSetDocumentProperty(doc, kTSMDocumentEnabledInputSourcesPropertyTag,
                             sizeof(CFArrayRef), &ASCIICapableTISList);
    ::CFRelease(ASCIICapableTISList);
  } else {
    ::TSMRemoveDocumentProperty(doc,
                                kTSMDocumentEnabledInputSourcesPropertyTag);
  }

  NS_OBJC_END_TRY_IGNORE_BLOCK;
}

void IMEInputHandler::ResetTimer() {
  NS_ASSERTION(mPendingMethods != 0,
               "There are not pending methods, why this is called?");
  if (mTimer) {
    mTimer->Cancel();
  } else {
    mTimer = NS_NewTimer();
    NS_ENSURE_TRUE(mTimer, );
  }
  mTimer->InitWithNamedFuncCallback(FlushPendingMethods, this, 0,
                                    nsITimer::TYPE_ONE_SHOT,
                                    "IMEInputHandler::FlushPendingMethods"_ns);
}

void IMEInputHandler::ExecutePendingMethods() {
  NS_OBJC_BEGIN_TRY_IGNORE_BLOCK;

  if (mTimer) {
    mTimer->Cancel();
    mTimer = nullptr;
  }

  if (![[NSApplication sharedApplication] isActive]) {
    // If we're not active, we should retry at focus event
    return;
  }

  uint32_t pendingMethods = mPendingMethods;
  // First, reset the pending method flags because if each methods cannot
  // run now, they can reentry to the pending flags by theirselves.
  mPendingMethods = 0;

  if (pendingMethods & kSyncASCIICapableOnly) SyncASCIICapableOnly();
  if (pendingMethods & kNotifyIMEOfFocusChangeInGecko) {
    NotifyIMEOfFocusChangeInGecko();
  }

  NS_OBJC_END_TRY_IGNORE_BLOCK;
}

#pragma mark -

/******************************************************************************
 *
 * IMEInputHandler implementation (native event handlers)
 *
 ******************************************************************************/

TextRangeType IMEInputHandler::ConvertToTextRangeType(uint32_t aUnderlineStyle,
                                                      NSRange& aSelectedRange) {
  MOZ_LOG(gIMELog, LogLevel::Info,
          ("%p IMEInputHandler::ConvertToTextRangeType, "
           "aUnderlineStyle=%u, aSelectedRange.length=%lu,",
           this, aUnderlineStyle,
           static_cast<unsigned long>(aSelectedRange.length)));

  // We assume that aUnderlineStyle is NSUnderlineStyleSingle or
  // NSUnderlineStyleThick.  NSUnderlineStyleThick should indicate a selected
  // clause.  Otherwise, should indicate non-selected clause.

  if (aSelectedRange.length == 0) {
    switch (aUnderlineStyle) {
      case NSUnderlineStyleSingle:
        return TextRangeType::eRawClause;
      case NSUnderlineStyleThick:
        return TextRangeType::eSelectedRawClause;
      default:
        NS_WARNING("Unexpected line style");
        return TextRangeType::eSelectedRawClause;
    }
  }

  switch (aUnderlineStyle) {
    case NSUnderlineStyleSingle:
      return TextRangeType::eConvertedClause;
    case NSUnderlineStyleThick:
      return TextRangeType::eSelectedClause;
    default:
      NS_WARNING("Unexpected line style");
      return TextRangeType::eSelectedClause;
  }
}

uint32_t IMEInputHandler::GetRangeCount(NSAttributedString* aAttrString) {
  NS_OBJC_BEGIN_TRY_BLOCK_RETURN;

  // Iterate through aAttrString for the NSUnderlineStyleAttributeName and
  // count the different segments adjusting limitRange as we go.
  uint32_t count = 0;
  NSRange effectiveRange;
  NSRange limitRange = NSMakeRange(0, [aAttrString length]);
  while (limitRange.length > 0) {
    [aAttrString attribute:NSUnderlineStyleAttributeName
                      atIndex:limitRange.location
        longestEffectiveRange:&effectiveRange
                      inRange:limitRange];
    limitRange =
        NSMakeRange(NSMaxRange(effectiveRange),
                    NSMaxRange(limitRange) - NSMaxRange(effectiveRange));
    count++;
  }

  MOZ_LOG(gIMELog, LogLevel::Info,
          ("%p IMEInputHandler::GetRangeCount, aAttrString=\"%s\", count=%u",
           this, GetCharacters([aAttrString string]).get(), count));

  return count;

  NS_OBJC_END_TRY_BLOCK_RETURN(0);
}

already_AddRefed<mozilla::TextRangeArray> IMEInputHandler::CreateTextRangeArray(
    NSAttributedString* aAttrString, NSRange& aSelectedRange) {
  NS_OBJC_BEGIN_TRY_BLOCK_RETURN;

  auto textRangeArray = MakeRefPtr<mozilla::TextRangeArray>();

  // Note that we shouldn't append ranges when composition string
  // is empty because it may cause TextComposition confused.
  if (![aAttrString length]) {
    return textRangeArray.forget();
  }

  // Convert the Cocoa range into the TextRange Array used in Gecko.
  // Iterate through the attributed string and map the underline attribute to
  // Gecko IME textrange attributes.  We may need to change the code here if
  // we change the implementation of validAttributesForMarkedText.
  NSRange limitRange = NSMakeRange(0, [aAttrString length]);
  uint32_t rangeCount = GetRangeCount(aAttrString);
  for (uint32_t i = 0; i < rangeCount && limitRange.length > 0; i++) {
    NSRange effectiveRange;
    id attributeValue = [aAttrString attribute:NSUnderlineStyleAttributeName
                                       atIndex:limitRange.location
                         longestEffectiveRange:&effectiveRange
                                       inRange:limitRange];

    TextRange range;
    range.mStartOffset = effectiveRange.location;
    range.mEndOffset = NSMaxRange(effectiveRange);
    range.mRangeType =
        ConvertToTextRangeType([attributeValue intValue], aSelectedRange);
    textRangeArray->AppendElement(range);

    MOZ_LOG(
        gIMELog, LogLevel::Info,
        ("%p   IMEInputHandler::CreateTextRangeArray, "
         "range={ mStartOffset=%u, mEndOffset=%u, mRangeType=%s }",
         this, range.mStartOffset, range.mEndOffset, ToChar(range.mRangeType)));

    limitRange =
        NSMakeRange(NSMaxRange(effectiveRange),
                    NSMaxRange(limitRange) - NSMaxRange(effectiveRange));
  }

  // Get current caret position.
  TextRange range;
  range.mStartOffset = aSelectedRange.location + aSelectedRange.length;
  range.mEndOffset = range.mStartOffset;
  range.mRangeType = TextRangeType::eCaret;
  textRangeArray->AppendElement(range);

  MOZ_LOG(
      gIMELog, LogLevel::Info,
      ("%p IMEInputHandler::CreateTextRangeArray, "
       "range={ mStartOffset=%u, mEndOffset=%u, mRangeType=%s }",
       this, range.mStartOffset, range.mEndOffset, ToChar(range.mRangeType)));

  return textRangeArray.forget();

  NS_OBJC_END_TRY_BLOCK_RETURN(nullptr);
}

bool IMEInputHandler::DispatchCompositionStartEvent() {
  MOZ_LOG(gIMELog, LogLevel::Info,
          ("%p IMEInputHandler::DispatchCompositionStartEvent, "
           "mSelectedRange=%s, Destroyed()=%s, mView=%p, mWidget=%p, "
           "inputContext=%p, mIsIMEComposing=%s",
           this, ToString(SelectedRange()).c_str(), TrueOrFalse(Destroyed()),
           mView, mWidget, mView ? [mView inputContext] : nullptr,
           TrueOrFalse(mIsIMEComposing)));

  RefPtr<IMEInputHandler> kungFuDeathGrip(this);

  nsresult rv = mDispatcher->BeginNativeInputTransaction();
  if (NS_WARN_IF(NS_FAILED(rv))) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("%p   IMEInputHandler::DispatchCompositionStartEvent, "
             "FAILED, due to BeginNativeInputTransaction() failure",
             this));
    return false;
  }

  NS_ASSERTION(!mIsIMEComposing, "There is a composition already");
  mIsIMEComposing = true;
  KeyEventState* currentKeyEvent = GetCurrentKeyEvent();
  mIsDeadKeyComposing = currentKeyEvent && currentKeyEvent->mKeyEvent &&
                        TISInputSourceWrapper::CurrentInputSource().IsDeadKey(
                            currentKeyEvent->mKeyEvent);

  nsEventStatus status;
  // IME may have already retrieved the selection and cached it.  Therefore, we
  // should retrieve selection range before dispatching eCompositionStart.
  mIMECompositionStartBeforeStart = mIMECompositionStartInContent =
      Some(SelectedRange().location);
  mSelectedRangeOverride = Some(SelectedRange());
  rv = mDispatcher->StartComposition(status);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("%p   IMEInputHandler::DispatchCompositionStartEvent, "
             "FAILED, due to StartComposition() failure",
             this));
    return false;
  }

  if (Destroyed()) {
    MOZ_LOG(gIMELog, LogLevel::Info,
            ("%p   IMEInputHandler::DispatchCompositionStartEvent, "
             "destroyed by compositionstart event",
             this));
    return false;
  }

  // FYI: Dispatching eCompositionStart may cause committing the composition if
  // the focused editor is in chrome UI.
  return mIsIMEComposing;
}

bool IMEInputHandler::DispatchCompositionChangeEvent(
    const nsString& aText, NSAttributedString* aAttrString,
    NSRange& aSelectedRange) {
  NS_OBJC_BEGIN_TRY_BLOCK_RETURN;

  MOZ_LOG(gIMELog, LogLevel::Info,
          ("%p IMEInputHandler::DispatchCompositionChangeEvent, aText=\"%s\", "
           "aAttrString=\"%s\", aSelectedRange=%s, Destroyed()=%s, mView=%p, "
           "mWidget=%p, inputContext=%p, mIsIMEComposing=%s",
           this, NS_ConvertUTF16toUTF8(aText).get(),
           GetCharacters([aAttrString string]).get(),
           ToString(aSelectedRange).c_str(), TrueOrFalse(Destroyed()), mView,
           mWidget, mView ? [mView inputContext] : nullptr,
           TrueOrFalse(mIsIMEComposing)));

  NS_ENSURE_TRUE(!Destroyed(), false);

  NS_ASSERTION(mIsIMEComposing, "We're not in composition");

  RefPtr<IMEInputHandler> kungFuDeathGrip(this);

  nsresult rv = mDispatcher->BeginNativeInputTransaction();
  if (NS_WARN_IF(NS_FAILED(rv))) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("%p   IMEInputHandler::DispatchCompositionChangeEvent, "
             "FAILED, due to BeginNativeInputTransaction() failure",
             this));
    return false;
  }

  RefPtr<TextRangeArray> rangeArray =
      CreateTextRangeArray(aAttrString, aSelectedRange);

  rv = mDispatcher->SetPendingComposition(aText, rangeArray);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("%p   IMEInputHandler::DispatchCompositionChangeEvent, "
             "FAILED, due to SetPendingComposition() failure",
             this));
    return false;
  }

  // For avoiding IME to be confused at the preceding text changes during
  // composition, we should not use the actual offset in content so that we
  // should use the location at composition start.
  mSelectedRangeOverride = Some(
      NSMakeRange(*mIMECompositionStartBeforeStart + aSelectedRange.location,
                  aSelectedRange.length));

  if (mIMECompositionString) {
    [mIMECompositionString release];
  }
  mIMECompositionString = [[aAttrString string] retain];

  nsEventStatus status;
  rv = mDispatcher->FlushPendingComposition(status);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("%p   IMEInputHandler::DispatchCompositionChangeEvent, "
             "FAILED, due to FlushPendingComposition() failure",
             this));
    return false;
  }

  if (Destroyed()) {
    MOZ_LOG(gIMELog, LogLevel::Info,
            ("%p   IMEInputHandler::DispatchCompositionChangeEvent, "
             "destroyed by compositionchange event",
             this));
    return false;
  }

  // FYI: compositionstart may cause committing composition by the webapp.
  return mIsIMEComposing;

  NS_OBJC_END_TRY_BLOCK_RETURN(false);
}

bool IMEInputHandler::DispatchCompositionCommitEvent(
    const nsAString* aCommitString) {
  NS_OBJC_BEGIN_TRY_BLOCK_RETURN;

  MOZ_LOG(
      gIMELog, LogLevel::Info,
      ("%p IMEInputHandler::DispatchCompositionCommitEvent, "
       "aCommitString=0x%p (\"%s\"), Destroyed()=%s, mView=%p, mWidget=%p, "
       "inputContext=%p, mIsIMEComposing=%s",
       this, aCommitString,
       aCommitString ? NS_ConvertUTF16toUTF8(*aCommitString).get() : "",
       TrueOrFalse(Destroyed()), mView, mWidget,
       mView ? [mView inputContext] : nullptr, TrueOrFalse(mIsIMEComposing)));

  NS_ASSERTION(mIsIMEComposing, "We're not in composition");

  RefPtr<IMEInputHandler> kungFuDeathGrip(this);

  // Finish overriding Selection and mSelectedRange will be updated before
  // dispatching the committing composition below and allow OnSelectionChange()
  // changes the result of SelectedRange().
  mSelectedRangeOverride.reset();

  if (!Destroyed()) {
    // mSelectedRange will be updated asynchronously if focused editor is in a
    // remote process.  However, IME may retrieve selected range immediately.
    // Therefore, we should emulate the selection after committing composition
    // right now.
    mSelectedRange.location = *mIMECompositionStartInContent;
    if (aCommitString) {
      mSelectedRange.location += aCommitString->Length();
    } else if (mIMECompositionString) {
      nsAutoString commitString;
      CopyNSStringToXPCOMString(mIMECompositionString, commitString);
      mSelectedRange.location += commitString.Length();
    }
    mSelectedRange.length = 0;

    nsresult rv = mDispatcher->BeginNativeInputTransaction();
    if (NS_WARN_IF(NS_FAILED(rv))) {
      MOZ_LOG(gIMELog, LogLevel::Error,
              ("%p   IMEInputHandler::DispatchCompositionCommitEvent, "
               "FAILED, due to BeginNativeInputTransaction() failure",
               this));
    } else {
      nsEventStatus status;
      rv = mDispatcher->CommitComposition(status, aCommitString);
      if (NS_WARN_IF(NS_FAILED(rv))) {
        MOZ_LOG(gIMELog, LogLevel::Error,
                ("%p   IMEInputHandler::DispatchCompositionCommitEvent, "
                 "FAILED, due to BeginNativeInputTransaction() failure",
                 this));
      }
    }
  }

  mIsIMEComposing = mIsDeadKeyComposing = false;
  mIMECompositionStartBeforeStart.reset();
  mIMECompositionStartInContent.reset();
  if (mIMECompositionString) {
    [mIMECompositionString release];
    mIMECompositionString = nullptr;
  }

  if (Destroyed()) {
    MOZ_LOG(gIMELog, LogLevel::Info,
            ("%p   IMEInputHandler::DispatchCompositionCommitEvent, "
             "destroyed by compositioncommit event",
             this));
    return false;
  }

  return true;

  NS_OBJC_END_TRY_BLOCK_RETURN(false);
}

bool IMEInputHandler::MaybeDispatchCurrentKeydownEvent(bool aIsProcessedByIME) {
  NS_OBJC_BEGIN_TRY_IGNORE_BLOCK;

  if (Destroyed()) {
    return false;
  }
  MOZ_ASSERT(mWidget);

  KeyEventState* currentKeyEvent = GetCurrentKeyEvent();
  if (!currentKeyEvent || !currentKeyEvent->CanDispatchKeyDownEvent()) {
    return true;
  }

  NSEvent* nativeEvent = currentKeyEvent->mKeyEvent;
  if (NS_WARN_IF(!nativeEvent) || [nativeEvent type] != NSEventTypeKeyDown) {
    return true;
  }

  MOZ_LOG(
      gIMELog, LogLevel::Info,
      ("%p IMEInputHandler::MaybeDispatchKeydownEvent, aIsProcessedByIME=%s "
       "currentKeyEvent={ mKeyEvent(%p)={ type=%s, keyCode=%s (0x%X) } }, "
       "aIsProcessedBy=%s, IsDeadKeyComposing()=%s",
       this, TrueOrFalse(aIsProcessedByIME), nativeEvent,
       GetNativeKeyEventType(nativeEvent),
       GetKeyNameForNativeKeyCode([nativeEvent keyCode]), [nativeEvent keyCode],
       TrueOrFalse(IsIMEComposing()), TrueOrFalse(IsDeadKeyComposing())));

  RefPtr<IMEInputHandler> kungFuDeathGrip(this);
  RefPtr<TextEventDispatcher> dispatcher(mDispatcher);
  nsresult rv = dispatcher->BeginNativeInputTransaction();
  if (NS_WARN_IF(NS_FAILED(rv))) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("%p   IMEInputHandler::DispatchKeyEventForFlagsChanged, "
             "FAILED, due to BeginNativeInputTransaction() failure",
             this));
    return false;
  }

  NSResponder* firstResponder = [[mView window] firstResponder];

  // Mark currentKeyEvent as "dispatched eKeyDown event" and actually do it.
  currentKeyEvent->mKeyDownDispatched = true;

  RefPtr<nsCocoaWindow> widget(mWidget);

  WidgetKeyboardEvent keydownEvent(true, eKeyDown, widget);
  // Don't mark the eKeyDown event as "processed by IME" if the composition
  // is started with dead key.
  currentKeyEvent->InitKeyEvent(this, keydownEvent,
                                aIsProcessedByIME && !IsDeadKeyComposing());

  nsEventStatus status = nsEventStatus_eIgnore;
  dispatcher->DispatchKeyboardEvent(eKeyDown, keydownEvent, status,
                                    currentKeyEvent);
  currentKeyEvent->mKeyDownHandled =
      (status == nsEventStatus_eConsumeNoDefault);

  if (Destroyed()) {
    MOZ_LOG(gIMELog, LogLevel::Info,
            ("%p   IMEInputHandler::MaybeDispatchKeydownEvent, "
             "widget was destroyed by keydown event",
             this));
    return false;
  }

  // The key down event may have shifted the focus, in which case, we should
  // not continue to handle current key sequence and let's commit current
  // composition.
  if (firstResponder != [[mView window] firstResponder]) {
    MOZ_LOG(gIMELog, LogLevel::Info,
            ("%p   IMEInputHandler::MaybeDispatchKeydownEvent, "
             "view lost focus by keydown event",
             this));
    CommitIMEComposition();
    return false;
  }

  return true;

  NS_OBJC_END_TRY_BLOCK_RETURN(false);
}

void IMEInputHandler::InsertTextAsCommittingComposition(
    NSString* aString, NSRange* aReplacementRange) {
  NS_OBJC_BEGIN_TRY_IGNORE_BLOCK;

  MOZ_LOG(gIMELog, LogLevel::Info,
          ("%p IMEInputHandler::InsertTextAsCommittingComposition, "
           "aAttrString=\"%s\", aReplacementRange=%s, Destroyed()=%s, "
           "IsIMEComposing()=%s, mMarkedRange=%s",
           this, GetCharacters(aString).get(),
           aReplacementRange ? ToString(*aReplacementRange).c_str() : "nullptr",
           TrueOrFalse(Destroyed()), TrueOrFalse(IsIMEComposing()),
           ToString(mMarkedRange).c_str()));

  if (IgnoreIMECommit()) {
    MOZ_CRASH("IMEInputHandler::InsertTextAsCommittingComposition() must not"
              "be called while canceling the composition");
  }

  if (Destroyed()) {
    return;
  }

  // When current keydown event causes this text input, let's dispatch
  // eKeyDown event before any other events.  Note that if we're in a
  // composition, we've already dispatched eKeyDown event from
  // TextInputHandler::HandleKeyDownEvent().
  // XXX Should we mark the eKeyDown event as "processed by IME"?
  //     However, if the key causes two or more Unicode characters as
  //     UTF-16 string, this is used.  So, perhaps, we need to improve
  //     HandleKeyDownEvent() before do that.
  RefPtr<IMEInputHandler> kungFuDeathGrip(this);
  if (!IsIMEComposing() && !MaybeDispatchCurrentKeydownEvent(false)) {
    MOZ_LOG(
        gIMELog, LogLevel::Info,
        ("%p   IMEInputHandler::InsertTextAsCommittingComposition, eKeyDown "
         "caused focus move or something and canceling the composition",
         this));
    return;
  }

  // First, commit current composition with the latest composition string if the
  // replacement range is different from marked range.
  if (IsIMEComposing() && aReplacementRange &&
      aReplacementRange->location != NSNotFound &&
      !NSEqualRanges(MarkedRange(), *aReplacementRange)) {
    if (!DispatchCompositionCommitEvent()) {
      MOZ_LOG(
          gIMELog, LogLevel::Info,
          ("%p   IMEInputHandler::InsertTextAsCommittingComposition, "
           "destroyed by committing composition for setting replacement range",
           this));
      return;
    }
  }

  nsString str;
  CopyNSStringToXPCOMString(aString, str);

  if (!IsIMEComposing()) {
    MOZ_DIAGNOSTIC_ASSERT(!str.IsEmpty());

    // If there is no selection and replacement range is specified, set the
    // range as selection.
    if (aReplacementRange && aReplacementRange->location != NSNotFound &&
        !NSEqualRanges(SelectedRange(), *aReplacementRange)) {
      NS_ENSURE_TRUE_VOID(SetSelection(*aReplacementRange));
    }

    MOZ_LOG(gIMELog, LogLevel::Info,
            ("%p   IMEInputHandler::InsertTextAsCommittingComposition, "
             "inserting the text with %s",
             this,
             StaticPrefs::intl_ime_use_composition_events_for_insert_text()
                 ? "composition events"
                 : "an eContentCommandInsertText event"));

    if (!StaticPrefs::intl_ime_use_composition_events_for_insert_text()) {
      // In the default settings, we should not use composition events for
      // inserting text without key press nor IME composition because the
      // other browsers do so.   This will cause only a cancelable `beforeinput`
      // event whose `inputType` is `insertText`.

      const RefPtr<TextEventDispatcher> dispatcher(mDispatcher);
      MOZ_ASSERT(dispatcher);
      (void)dispatcher->DispatchInsertTextCommandEvent(str);
      return;
    }

    // Otherwise, emulate an IME composition.  This is our traditional behavior,
    // but `beforeinput` events are not cancelable since they should be so for
    // native IME limitation.  So, this is now seriously incompatible with the
    // other browsers.
    if (!DispatchCompositionStartEvent()) {
      MOZ_LOG(gIMELog, LogLevel::Info,
              ("%p   IMEInputHandler::InsertTextAsCommittingComposition, "
               "cannot continue handling composition after compositionstart",
               this));
      return;
    }
  }

  if (!DispatchCompositionCommitEvent(&str)) {
    MOZ_LOG(gIMELog, LogLevel::Info,
            ("%p   IMEInputHandler::InsertTextAsCommittingComposition, "
             "destroyed by compositioncommit event",
             this));
    return;
  }

  mMarkedRange = NSMakeRange(NSNotFound, 0);

  NS_OBJC_END_TRY_IGNORE_BLOCK;
}

void IMEInputHandler::SetMarkedText(NSAttributedString* aAttrString,
                                    NSRange& aSelectedRange,
                                    NSRange* aReplacementRange) {
  NS_OBJC_BEGIN_TRY_IGNORE_BLOCK;

  KeyEventState* currentKeyEvent = GetCurrentKeyEvent();

  MOZ_LOG(
      gIMELog, LogLevel::Info,
      ("%p IMEInputHandler::SetMarkedText, aAttrString=\"%s\", "
       "aSelectedRange=%s, aReplacementRange=%s, Destroyed()=%s, "
       "IsIMEComposing()=%s, mMarkedRange=%s, keyevent=%p, "
       "keydownDispatched=%s, keydownHandled=%s, keypressDispatched=%s, "
       "causedOtherKeyEvents=%s, compositionDispatched=%s",
       this, GetCharacters([aAttrString string]).get(),
       ToString(aSelectedRange).c_str(),
       aReplacementRange ? ToString(*aReplacementRange).c_str() : "nullptr",
       TrueOrFalse(Destroyed()), TrueOrFalse(IsIMEComposing()),
       ToString(mMarkedRange).c_str(),
       currentKeyEvent ? currentKeyEvent->mKeyEvent : nullptr,
       currentKeyEvent ? TrueOrFalse(currentKeyEvent->mKeyDownDispatched)
                       : "N/A",
       currentKeyEvent ? TrueOrFalse(currentKeyEvent->mKeyDownHandled) : "N/A",
       currentKeyEvent ? TrueOrFalse(currentKeyEvent->mKeyPressDispatched)
                       : "N/A",
       currentKeyEvent ? TrueOrFalse(currentKeyEvent->mCausedOtherKeyEvents)
                       : "N/A",
       currentKeyEvent ? TrueOrFalse(currentKeyEvent->mCompositionDispatched)
                       : "N/A"));

  RefPtr<IMEInputHandler> kungFuDeathGrip(this);

  // If SetMarkedText() is called during handling a key press, that means that
  // the key event caused this composition.  So, keypress event shouldn't
  // be dispatched later, let's mark the key event causing composition event.
  if (currentKeyEvent) {
    currentKeyEvent->mCompositionDispatched = true;

    // When current keydown event causes this text input, let's dispatch
    // eKeyDown event before any other events.  Note that if we're in a
    // composition, we've already dispatched eKeyDown event from
    // TextInputHandler::HandleKeyDownEvent().  On the other hand, if we're
    // not in composition, the key event starts new composition.  So, we
    // need to mark the eKeyDown event as "processed by IME".
    if (!IsIMEComposing() && !MaybeDispatchCurrentKeydownEvent(true)) {
      MOZ_LOG(
          gIMELog, LogLevel::Info,
          ("%p   IMEInputHandler::SetMarkedText, eKeyDown caused focus move or "
           "something and canceling the composition",
           this));
      return;
    }
  }

  if (Destroyed()) {
    return;
  }

  // First, commit current composition with the latest composition string if the
  // replacement range is different from marked range.
  if (IsIMEComposing() && aReplacementRange &&
      aReplacementRange->location != NSNotFound &&
      !NSEqualRanges(MarkedRange(), *aReplacementRange)) {
    AutoRestore<bool> ignoreIMECommit(mIgnoreIMECommit);
    mIgnoreIMECommit = false;
    if (!DispatchCompositionCommitEvent()) {
      MOZ_LOG(
          gIMELog, LogLevel::Info,
          ("%p   IMEInputHandler::SetMarkedText, "
           "destroyed by committing composition for setting replacement range",
           this));
      return;
    }
  }

  nsString str;
  CopyNSStringToXPCOMString([aAttrString string], str);

  mMarkedRange.length = str.Length();

  if (!IsIMEComposing() && !str.IsEmpty()) {
    // If there is no selection and replacement range is specified, set the
    // range as selection.
    if (aReplacementRange && aReplacementRange->location != NSNotFound &&
        !NSEqualRanges(SelectedRange(), *aReplacementRange)) {
      // Set temporary selection range since OnSelectionChange is async.
      mSelectedRange = *aReplacementRange;
      if (NS_WARN_IF(!SetSelection(*aReplacementRange))) {
        mSelectedRange.location = NSNotFound;  // Marking dirty
        return;
      }
    }

    mMarkedRange.location = SelectedRange().location;

    if (!DispatchCompositionStartEvent()) {
      MOZ_LOG(gIMELog, LogLevel::Info,
              ("%p   IMEInputHandler::SetMarkedText, cannot continue handling "
               "composition after dispatching compositionstart",
               this));
      return;
    }
  }

  if (!str.IsEmpty()) {
    if (!DispatchCompositionChangeEvent(str, aAttrString, aSelectedRange)) {
      MOZ_LOG(gIMELog, LogLevel::Info,
              ("%p   IMEInputHandler::SetMarkedText, cannot continue handling "
               "composition after dispatching compositionchange",
               this));
    }
    return;
  }

  // If the composition string becomes empty string, we should commit
  // current composition.
  if (!DispatchCompositionCommitEvent(&EmptyString())) {
    MOZ_LOG(gIMELog, LogLevel::Info,
            ("%p   IMEInputHandler::SetMarkedText, "
             "destroyed by compositioncommit event",
             this));
  }

  NS_OBJC_END_TRY_IGNORE_BLOCK;
}

NSAttributedString* IMEInputHandler::GetAttributedSubstringFromRange(
    NSRange& aRange, NSRange* aActualRange) {
  NS_OBJC_BEGIN_TRY_BLOCK_RETURN;

  MOZ_LOG(
      gIMELog, LogLevel::Info,
      ("%p   IMEInputHandler::GetAttributedSubstringFromRange, aRange=%s, "
       "aActualRange=%p, Destroyed()=%s",
       this, ToString(aRange).c_str(), aActualRange, TrueOrFalse(Destroyed())));

  if (aActualRange) {
    *aActualRange = NSMakeRange(NSNotFound, 0);
  }

  if (Destroyed() || !mDispatcher || aRange.location == NSNotFound ||
      aRange.length == 0) [[unlikely]] {
    return nil;
  }

  RefPtr<IMEInputHandler> kungFuDeathGrip(this);

  // If we're in composing, the queried range may be in the composition string.
  // In such case, we should use mIMECompositionString since if the composition
  // string is handled by a remote process, the content cache may be out of
  // date.
  // XXX Should we set composition string attributes?  Although, Blink claims
  //     that some attributes of marked text are supported, but they return
  //     just marked string without any style.  So, let's keep current behavior
  //     at least for now.
  NSUInteger compositionLength =
      mIMECompositionString ? [mIMECompositionString length] : 0;
  if (mIMECompositionStartBeforeStart.isSome() &&
      aRange.location >= *mIMECompositionStartBeforeStart &&
      aRange.location + aRange.length <=
          *mIMECompositionStartBeforeStart + compositionLength) {
    NSRange range = NSMakeRange(
        aRange.location - *mIMECompositionStartBeforeStart, aRange.length);
    NSString* nsstr = [mIMECompositionString substringWithRange:range];
    NSMutableAttributedString* result =
        [[[NSMutableAttributedString alloc] initWithString:nsstr
                                                attributes:nil] autorelease];
    // XXX We cannot return font information in this case.  However, this
    //     case must occur only when IME tries to confirm if composing string
    //     is handled as expected.
    if (aActualRange) {
      *aActualRange = aRange;
    }

    if (MOZ_LOG_TEST(gIMELog, LogLevel::Info)) {
      nsAutoString str;
      CopyNSStringToXPCOMString(nsstr, str);
      MOZ_LOG(gIMELog, LogLevel::Info,
              ("%p   IMEInputHandler::GetAttributedSubstringFromRange, "
               "computed with mIMECompositionString (result string=\"%s\")",
               this, NS_ConvertUTF16toUTF8(str).get()));
    }
    return result;
  }

  const OwningNonNull<nsCocoaWindow> widget = *mWidget;

  QueryContentOptions options;
  int64_t startOffset = aRange.location;
  if (mIMECompositionStartBeforeStart.isSome()) {
    // The composition may be at different offset from the selection start
    // offset at dispatching compositionstart because start of composition
    // is fixed when composition string becomes non-empty in the editor.
    // Therefore, we need to use query event which is relative to insertion
    // point.
    options.mRelativeToInsertionPoint = true;
    startOffset -= *mIMECompositionStartBeforeStart;
  }
  options.mRequestFontRanges = true;
  const OwningNonNull<TextEventDispatcher> dispatcher = *mDispatcher;
  const Maybe<QueryContentResult> queryTextContentResult =
      dispatcher->QueryTextContent(startOffset, aRange.length, options);

  MOZ_LOG_FMT(gIMELog, LogLevel::Info,
              "{}   IMEInputHandler::GetAttributedSubstringFromRange, "
              "queryTextContentResult={}",
              static_cast<void*>(this), queryTextContentResult);

  if (queryTextContentResult.isNothing()) [[unlikely]] {
    return nil;
  }

  // We don't set vertical information at this point.  If required,
  // OS will calls drawsVerticallyForCharacterAtIndex.
  NSMutableAttributedString* result =
      nsCocoaUtils::GetNSMutableAttributedString(
          queryTextContentResult->DataRef(),
          queryTextContentResult->mFontRanges, false,
          widget->BackingScaleFactor());
  if (aActualRange) {
    *aActualRange = MakeNSRangeFrom(queryTextContentResult->mOffsetAndData);
  }
  return result;

  NS_OBJC_END_TRY_BLOCK_RETURN(nil);
}

bool IMEInputHandler::HasMarkedText() {
  MOZ_LOG(gIMELog, LogLevel::Info,
          ("%p   IMEInputHandler::HasMarkedText, mMarkedRange=%s", this,
           ToString(mMarkedRange).c_str()));

  return (mMarkedRange.location != NSNotFound) && (mMarkedRange.length != 0);
}

NSRange IMEInputHandler::MarkedRange() {
  MOZ_LOG(gIMELog, LogLevel::Info,
          ("%p   IMEInputHandler::MarkedRange, mMarkedRange=%s", this,
           ToString(mMarkedRange).c_str()));

  if (!HasMarkedText()) {
    return NSMakeRange(NSNotFound, 0);
  }

  // XXX If MarkedRange() is required by IME, we could return actual range in
  // content.  If we do that, IME can interact with the latest content
  // information.  E.g., actual surrounding text which may have already been
  // modified by the web apps during the composition.  On the other hand,
  // there is no way to notify IME of when it's updated.  Therefore, if
  // `SelectedRange()` is called before `MarkedRange()`, the ranges will
  // mismatch in IME.  Therefore, probably we should not do that.

  return mMarkedRange;
}

NSRange IMEInputHandler::SelectedRange() {
  NS_OBJC_BEGIN_TRY_BLOCK_RETURN;

  MOZ_LOG_FMT(gIMELog, LogLevel::Info,
              "{}   IMEInputHandler::SelectedRange, Destroyed()={}, "
              "mSelectedRange={}, mSelectedRangeOverride={}",
              static_cast<void*>(this), Destroyed(), mSelectedRange,
              mSelectedRangeOverride);

  // If selection range is overridden during a composition, we should return the
  // override instead of selected range in the content.  That makes IME work
  // without confusion even if IME caches the compositing range at starting
  // composition and the web app changes the preceding text of the composition.
  if (mSelectedRangeOverride.isSome()) {
    return *mSelectedRangeOverride;
  }

  if (Destroyed() || !mDispatcher) [[unlikely]] {
    return mSelectedRange;
  }

  if (mSelectedRange.location != NSNotFound) {
    MOZ_ASSERT(mIMEHasFocus);
    return mSelectedRange;
  }

  RefPtr<IMEInputHandler> kungFuDeathGrip(this);
  const OwningNonNull<TextEventDispatcher> dispatcher = *mDispatcher;
  const Maybe<QueryContentResult> querySelectionResult =
      dispatcher->QueryNormalSelection();
  MOZ_LOG_FMT(gIMELog, LogLevel::Info,
              "{}   IMEInputHandler::SelectedRange, querySelectionResult={}",
              static_cast<void*>(this), querySelectionResult);

  if (querySelectionResult.isNothing()) [[unlikely]] {
    return mSelectedRange;
  }

  mWritingMode = querySelectionResult->WritingModeRef();
  mRangeForWritingMode = MakeNSRangeFrom(querySelectionResult->mOffsetAndData);

  if (mIMEHasFocus) {
    mSelectedRange = mRangeForWritingMode;
  }

  return mRangeForWritingMode;

  NS_OBJC_END_TRY_BLOCK_RETURN(mSelectedRange);
}

bool IMEInputHandler::DrawsVerticallyForCharacterAtIndex(uint32_t aCharIndex) {
  NS_OBJC_BEGIN_TRY_BLOCK_RETURN;

  if (Destroyed()) {
    return false;
  }

  if (mRangeForWritingMode.location == NSNotFound) {
    // Update cached writing-mode value for the current selection.
    SelectedRange();
  }

  if (aCharIndex < mRangeForWritingMode.location ||
      aCharIndex >
          mRangeForWritingMode.location + mRangeForWritingMode.length) {
    // It's not clear to me whether this ever happens in practice, but if an
    // IME ever wants to query writing mode at an offset outside the current
    // selection, the writing-mode value may not be correct for the index.
    // In that case, use FirstRectForCharacterRange to get a fresh value.
    // This does more work than strictly necessary (we don't need the rect
    // here), but should be a rare case.
    NS_WARNING(
        "DrawsVerticallyForCharacterAtIndex not using cached writing mode");
    NSRange range = NSMakeRange(aCharIndex, 1);
    NSRange actualRange;
    FirstRectForCharacterRange(range, &actualRange);
  }

  return mWritingMode.IsVertical();

  NS_OBJC_END_TRY_BLOCK_RETURN(false);
}

NSRect IMEInputHandler::FirstRectForCharacterRange(NSRange& aRange,
                                                   NSRange* aActualRange) {
  NS_OBJC_BEGIN_TRY_BLOCK_RETURN;

  MOZ_LOG_FMT(gIMELog, LogLevel::Info,
              "{} IMEInputHandler::FirstRectForCharacterRange, Destroyed()={}, "
              "mDispatcher={}, aRange={}, aActualRange={}",
              static_cast<void*>(this), Destroyed(),
              static_cast<void*>(mDispatcher), aRange,
              static_cast<void*>(aActualRange));

  // XXX this returns first character rect or caret rect, it is limitation of
  // now. We need more work for returns first line rect. But current
  // implementation is enough for IMEs.

  NSRect rect = NSMakeRect(0.0, 0.0, 0.0, 0.0);
  NSRange actualRange = NSMakeRange(NSNotFound, 0);
  if (aActualRange) {
    *aActualRange = actualRange;
  }
  if (Destroyed() || !mDispatcher || aRange.location == NSNotFound)
      [[unlikely]] {
    return rect;
  }
  const OwningNonNull<TextEventDispatcher> dispatcher = *mDispatcher;

  RefPtr<IMEInputHandler> kungFuDeathGrip(this);

  LayoutDeviceIntRect r;
  bool useCaretRect = (aRange.length == 0);
  if (!useCaretRect) {
    QueryContentOptions options;
    int64_t startOffset = aRange.location;
    if (mIMECompositionStartBeforeStart.isSome()) {
      // The composition may be at different offset from the selection start
      // offset at dispatching compositionstart because start of composition
      // is fixed when composition string becomes non-empty in the editor.
      // Therefore, we need to use query event which is relative to insertion
      // point.
      options.mRelativeToInsertionPoint = true;
      startOffset -= *mIMECompositionStartBeforeStart;
    }
    const Maybe<QueryContentResult> queryTextRectResult =
        dispatcher->QueryTextRect(startOffset, 1, options);
    if (!mWidget) [[unlikely]] {
      return rect;
    }
    if (queryTextRectResult.isSome()) [[likely]] {
      r = queryTextRectResult->mRect;
      actualRange = MakeNSRangeFrom(queryTextRectResult->mOffsetAndData);
      mWritingMode = queryTextRectResult->WritingModeRef();
      mRangeForWritingMode = actualRange;
    } else {
      useCaretRect = true;
    }
  }

  if (useCaretRect) {
    QueryContentOptions options;
    int64_t startOffset = aRange.location;
    if (mIMECompositionStartBeforeStart.isSome()) {
      // The composition may be at different offset from the selection start
      // offset at dispatching compositionstart because start of composition
      // is fixed when composition string becomes non-empty in the editor.
      // Therefore, we need to use query event which is relative to insertion
      // point.
      options.mRelativeToInsertionPoint = true;
      startOffset -= *mIMECompositionStartBeforeStart;
    }
    const Maybe<QueryContentResult> queryCaretRectResult =
        dispatcher->QueryCaretRect(startOffset, options);
    if (queryCaretRectResult.isNothing() || !mWidget) [[unlikely]] {
      return rect;
    }
    r = queryCaretRectResult->mRect;
    r.width = 0;
    actualRange.location = queryCaretRectResult->StartOffset();
    actualRange.length = 0;
  }

  // Widget relative -> Screen relative.
  r += mWidget->WidgetToScreenOffset();
  rect = nsCocoaUtils::GeckoRectToCocoaRectDevPix(
      r, mWidget->BackingScaleFactor());

  if (aActualRange) {
    *aActualRange = actualRange;
  }

  MOZ_LOG_FMT(gIMELog, LogLevel::Info,
              "{}   IMEInputHandler::FirstRectForCharacterRange, "
              "useCaretRect={}, rect={}, actualRange={}",
              static_cast<void*>(this), useCaretRect, rect, actualRange);

  return rect;

  NS_OBJC_END_TRY_BLOCK_RETURN(NSMakeRect(0.0, 0.0, 0.0, 0.0));
}

NSUInteger IMEInputHandler::CharacterIndexForPoint(NSPoint& aPoint) {
  NS_OBJC_BEGIN_TRY_BLOCK_RETURN;

  MOZ_LOG(gIMELog, LogLevel::Info,
          ("%p IMEInputHandler::CharacterIndexForPoint, aPoint={ x=%f, y=%f }",
           this, aPoint.x, aPoint.y));

  NSWindow* mainWindow = [NSApp mainWindow];
  if (!mWidget || !mainWindow) {
    return NSNotFound;
  }

  WidgetQueryContentEvent queryCharAtPointEvent(true, eQueryCharacterAtPoint,
                                                mWidget);
  NSPoint ptInWindow = nsCocoaUtils::ConvertPointFromScreen(mainWindow, aPoint);
  NSPoint ptInView = [mView convertPoint:ptInWindow fromView:nil];
  queryCharAtPointEvent.mRefPoint.x =
      static_cast<int32_t>(ptInView.x * mWidget->BackingScaleFactor());
  queryCharAtPointEvent.mRefPoint.y =
      static_cast<int32_t>(ptInView.y * mWidget->BackingScaleFactor());
  mWidget->DispatchWindowEvent(queryCharAtPointEvent);
  if (queryCharAtPointEvent.Failed() ||
      queryCharAtPointEvent.DidNotFindChar() ||
      queryCharAtPointEvent.mReply->StartOffset() >=
          static_cast<uint32_t>(NSNotFound)) {
    return NSNotFound;
  }

  return queryCharAtPointEvent.mReply->StartOffset();

  NS_OBJC_END_TRY_BLOCK_RETURN(NSNotFound);
}

extern "C" {
extern NSString* NSTextInputReplacementRangeAttributeName;
}

NSArray* IMEInputHandler::GetValidAttributesForMarkedText() {
  NS_OBJC_BEGIN_TRY_BLOCK_RETURN;

  MOZ_LOG(gIMELog, LogLevel::Info,
          ("%p IMEInputHandler::GetValidAttributesForMarkedText", this));

  // Return same attributes as Chromium (see render_widget_host_view_mac.mm)
  // because most IMEs must be tested with Safari (OS default) and Chrome
  // (having most market share).  Therefore, we need to follow their behavior.
  // XXX It might be better to reuse an array instance for this result because
  //     this may be called a lot.  Note that Chromium does so.
  return
      [NSArray arrayWithObjects:NSUnderlineStyleAttributeName,
                                NSUnderlineColorAttributeName,
                                NSMarkedClauseSegmentAttributeName,
                                NSTextInputReplacementRangeAttributeName, nil];

  NS_OBJC_END_TRY_BLOCK_RETURN(nil);
}

#pragma mark -

/******************************************************************************
 *
 *  IMEInputHandler implementation #2
 *
 ******************************************************************************/

IMEInputHandler::IMEInputHandler(nsCocoaWindow* aWidget,
                                 NSView<mozView>* aNativeView)
    : TextInputHandlerBase(aWidget, aNativeView),
      mPendingMethods(0),
      mCandidatedTextSubstitutionResult(nullptr),
      mProcessTextSubstitution(false),
      mRangeForWritingMode(),
      mIsIMEComposing(false),
      mIsDeadKeyComposing(false),
      mIsIMEEnabled(true),
      mIsASCIICapableOnly(false),
      mIgnoreIMECommit(false),
      mIMEHasFocus(false),
      mEnableTextSubstitution(false) {
  InitStaticMembers();

  mMarkedRange.location = NSNotFound;
  mMarkedRange.length = 0;
  mSelectedRange.location = NSNotFound;
  mSelectedRange.length = 0;
}

IMEInputHandler::~IMEInputHandler() {
  if (mTimer) {
    mTimer->Cancel();
    mTimer = nullptr;
  }
  if (sFocusedIMEHandler == this) {
    sFocusedIMEHandler = nullptr;
  }
  if (mCandidatedTextSubstitutionResult) {
    [mCandidatedTextSubstitutionResult release];
    mCandidatedTextSubstitutionResult = nullptr;
  }
  if (mIMECompositionString) {
    [mIMECompositionString release];
    mIMECompositionString = nullptr;
  }
}

void IMEInputHandler::OnFocusChangeInGecko(bool aFocus) {
  MOZ_LOG(
      gIMELog, LogLevel::Info,
      ("%p IMEInputHandler::OnFocusChangeInGecko, aFocus=%s, Destroyed()=%s, "
       "sFocusedIMEHandler=%p",
       this, TrueOrFalse(aFocus), TrueOrFalse(Destroyed()),
       sFocusedIMEHandler));

  mSelectedRange.location = NSNotFound;  // Marking dirty
  mIMEHasFocus = aFocus;

  DismissTextSubstitutionPanel();

  // This is called when the native focus is changed and when the native focus
  // isn't changed but the focus is changed in Gecko.
  if (!aFocus) {
    if (sFocusedIMEHandler == this) sFocusedIMEHandler = nullptr;
    return;
  }

  sFocusedIMEHandler = this;

  // We need to notify IME of focus change in Gecko as native focus change
  // because the window level of the focused element in Gecko may be changed.
  mPendingMethods |= kNotifyIMEOfFocusChangeInGecko;
  ResetTimer();
}

bool IMEInputHandler::OnDestroyWidget(nsCocoaWindow* aDestroyingWidget) {
  MOZ_LOG(gIMELog, LogLevel::Info,
          ("%p IMEInputHandler::OnDestroyWidget, aDestroyingWidget=%p, "
           "sFocusedIMEHandler=%p, IsIMEComposing()=%s",
           this, aDestroyingWidget, sFocusedIMEHandler,
           TrueOrFalse(IsIMEComposing())));

  // If we're not focused, the focused IMEInputHandler may have been
  // created by another widget/nsCocoaWindow.
  if (sFocusedIMEHandler && sFocusedIMEHandler != this) {
    sFocusedIMEHandler->OnDestroyWidget(aDestroyingWidget);
  }

  if (!TextInputHandlerBase::OnDestroyWidget(aDestroyingWidget)) {
    return false;
  }

  // If the focused widget is being destroyed without a preceding resign-key or
  // blur (e.g. a popup or window closed while a password field is focused while
  // the app stays active), release Secure Event Input here.  Otherwise it can
  // stay enabled process-wide and lock other apps out of keyboard input (bug
  // 2050794, bug 556873).  Only the focused widget can own Secure Event Input,
  // so scope the drain to it to avoid disabling input that another window
  // holds.
  if (sFocusedIMEHandler == this) {
    EnsureSecureEventInputDisabled();
    // The normal blur/resign-key path that would clear sFocusedIMEHandler via
    // OnFocusChangeInGecko(false) was skipped, so clear it here too.  Otherwise
    // it keeps pointing at this handler after its widget is gone.
    sFocusedIMEHandler = nullptr;
  }

  if (IsIMEComposing()) {
    // If our view is in the composition, we should clean up it.
    CancelIMEComposition();
  }

  mSelectedRange.location = NSNotFound;  // Marking dirty
  mIMEHasFocus = false;

  return true;
}

void IMEInputHandler::SendCommittedText(NSString* aString) {
  NS_OBJC_BEGIN_TRY_IGNORE_BLOCK;

  MOZ_LOG(gIMELog, LogLevel::Info,
          ("%p IMEInputHandler::SendCommittedText, mView=%p, mWidget=%p, "
           "inputContext=%p, mIsIMEComposing=%s",
           this, mView, mWidget, mView ? [mView inputContext] : nullptr,
           TrueOrFalse(mIsIMEComposing)));

  NS_ENSURE_TRUE(mWidget, );
  // XXX We should send the string without mView.
  if (!mView) {
    return;
  }

  if ([mView conformsToProtocol:@protocol(NSTextInputClient)]) {
    NSObject<NSTextInputClient>* textInputClient =
        static_cast<NSObject<NSTextInputClient>*>(mView);
    [textInputClient insertText:aString
               replacementRange:NSMakeRange(NSNotFound, 0)];
  }

  // Last resort.  If we cannot retrieve NSTextInputProtocol from mView
  // or blocking to call our InsertText(), we should call InsertText()
  // directly to commit composition forcibly.
  if (mIsIMEComposing) {
    MOZ_LOG(gIMELog, LogLevel::Info,
            ("%p   IMEInputHandler::SendCommittedText, trying to insert text "
             "directly "
             "due to IME not calling our InsertText()",
             this));
    static_cast<TextInputHandler*>(this)->InsertText(aString);
    MOZ_ASSERT(!mIsIMEComposing);
  }

  NS_OBJC_END_TRY_IGNORE_BLOCK;
}

void IMEInputHandler::KillIMEComposition() {
  NS_OBJC_BEGIN_TRY_IGNORE_BLOCK;

  MOZ_LOG(gIMELog, LogLevel::Info,
          ("%p IMEInputHandler::KillIMEComposition, mView=%p, mWidget=%p, "
           "inputContext=%p, mIsIMEComposing=%s, "
           "Destroyed()=%s, IsFocused()=%s",
           this, mView, mWidget, mView ? [mView inputContext] : nullptr,
           TrueOrFalse(mIsIMEComposing), TrueOrFalse(Destroyed()),
           TrueOrFalse(IsFocused())));

  if (Destroyed() || NS_WARN_IF(!mView)) {
    return;
  }

  NSTextInputContext* inputContext = [mView inputContext];
  if (NS_WARN_IF(!inputContext)) {
    return;
  }
  [inputContext discardMarkedText];

  NS_OBJC_END_TRY_IGNORE_BLOCK;
}

void IMEInputHandler::CommitIMEComposition() {
  NS_OBJC_BEGIN_TRY_IGNORE_BLOCK;

  MOZ_LOG(gIMELog, LogLevel::Info,
          ("%p IMEInputHandler::CommitIMEComposition, mIMECompositionString=%s",
           this, GetCharacters(mIMECompositionString).get()));

  // If this is called before dispatching eCompositionStart, IsIMEComposing()
  // returns false.  Even in such case, we need to commit composition *in*
  // IME if this is called by preceding eKeyDown event of eCompositionStart.
  // So, we need to call KillIMEComposition() even when IsIMEComposing()
  // returns false.
  KillIMEComposition();

  if (!IsIMEComposing()) return;

  // If the composition is still there, KillIMEComposition only kills the
  // composition in TSM.  We also need to finish the our composition too.
  SendCommittedText(mIMECompositionString);

  NS_OBJC_END_TRY_IGNORE_BLOCK;
}

void IMEInputHandler::CancelIMEComposition() {
  NS_OBJC_BEGIN_TRY_IGNORE_BLOCK;

  if (!IsIMEComposing()) return;

  MOZ_LOG(gIMELog, LogLevel::Info,
          ("%p IMEInputHandler::CancelIMEComposition, mIMECompositionString=%s",
           this, GetCharacters(mIMECompositionString).get()));

  // For canceling the current composing, we need to ignore the param of
  // insertText.  But this code is ugly...
  mIgnoreIMECommit = true;
  KillIMEComposition();
  mIgnoreIMECommit = false;

  if (!IsIMEComposing()) return;

  // If the composition is still there, KillIMEComposition only kills the
  // composition in TSM.  We also need to kill the our composition too.
  SendCommittedText(@"");

  NS_OBJC_END_TRY_IGNORE_BLOCK;
}

bool IMEInputHandler::IsFocused() {
  NS_OBJC_BEGIN_TRY_IGNORE_BLOCK;

  NS_ENSURE_TRUE(!Destroyed(), false);
  NSWindow* window = [mView window];
  NS_ENSURE_TRUE(window, false);
  return [window firstResponder] == mView && [window isKeyWindow] &&
         [[NSApplication sharedApplication] isActive];

  NS_OBJC_END_TRY_BLOCK_RETURN(false);
}

bool IMEInputHandler::IsIMEOpened() {
  TISInputSourceWrapper tis;
  tis.InitByCurrentInputSource();
  return tis.IsOpenedIMEMode();
}

void IMEInputHandler::SetASCIICapableOnly(bool aASCIICapableOnly) {
  if (aASCIICapableOnly == mIsASCIICapableOnly) return;

  CommitIMEComposition();
  mIsASCIICapableOnly = aASCIICapableOnly;
  SyncASCIICapableOnly();
}

void IMEInputHandler::EnableIME(bool aEnableIME) {
  if (aEnableIME == mIsIMEEnabled) return;

  CommitIMEComposition();
  mIsIMEEnabled = aEnableIME;
}

void IMEInputHandler::SetIMEOpenState(bool aOpenIME) {
  if (!IsFocused() || IsIMEOpened() == aOpenIME) return;

  if (!aOpenIME) {
    TISInputSourceWrapper tis;
    tis.InitByCurrentASCIICapableInputSource();
    tis.Select();
    return;
  }

  // If we know the latest IME opened mode, we should select it.
  if (sLatestIMEOpenedModeInputSourceID) {
    TISInputSourceWrapper tis;
    tis.InitByInputSourceID(sLatestIMEOpenedModeInputSourceID);
    tis.Select();
    return;
  }

  // XXX If the current input source is a mode of IME, we should turn on it,
  // but we haven't found such way...

  // Finally, we should refer the system locale but this is a little expensive,
  // we shouldn't retry this (if it was succeeded, we already set
  // sLatestIMEOpenedModeInputSourceID at that time).
  static bool sIsPrefferredIMESearched = false;
  if (sIsPrefferredIMESearched) return;
  sIsPrefferredIMESearched = true;
  OpenSystemPreferredLanguageIME();
}

void IMEInputHandler::OpenSystemPreferredLanguageIME() {
  MOZ_LOG(gIMELog, LogLevel::Info,
          ("%p IMEInputHandler::OpenSystemPreferredLanguageIME", this));

  CFArrayRef langList = ::CFLocaleCopyPreferredLanguages();
  if (!langList) {
    MOZ_LOG(gIMELog, LogLevel::Info,
            ("%p   IMEInputHandler::OpenSystemPreferredLanguageIME, langList "
             "is NULL",
             this));
    return;
  }
  CFIndex count = ::CFArrayGetCount(langList);
  for (CFIndex i = 0; i < count; i++) {
    CFLocaleRef locale = ::CFLocaleCreate(
        kCFAllocatorDefault,
        static_cast<CFStringRef>(::CFArrayGetValueAtIndex(langList, i)));
    if (!locale) {
      continue;
    }

    bool changed = false;
    CFStringRef lang = static_cast<CFStringRef>(
        ::CFLocaleGetValue(locale, kCFLocaleLanguageCode));
    NS_ASSERTION(lang, "lang is null");
    if (lang) {
      TISInputSourceWrapper tis;
      tis.InitByLanguage(lang);
      if (tis.IsOpenedIMEMode()) {
        if (MOZ_LOG_TEST(gIMELog, LogLevel::Info)) {
          CFStringRef foundTIS;
          tis.GetInputSourceID(foundTIS);
          MOZ_LOG(
              gIMELog, LogLevel::Info,
              ("%p   IMEInputHandler::OpenSystemPreferredLanguageIME, "
               "foundTIS=%s, lang=%s",
               this, GetCharacters(foundTIS).get(), GetCharacters(lang).get()));
        }
        tis.Select();
        changed = true;
      }
    }
    ::CFRelease(locale);
    if (changed) {
      break;
    }
  }
  ::CFRelease(langList);
}

void IMEInputHandler::OnSelectionChange(
    const IMENotification& aIMENotification) {
  MOZ_ASSERT(aIMENotification.mSelectionChangeData.IsInitialized());

  const IMENotification::SelectionChangeDataBase& selectionChangeData =
      aIMENotification.mSelectionChangeData;
  MOZ_LOG(gIMELog, LogLevel::Info,
          ("%p "
           "IMEInputHandler::OnSelectionChange(aIMENotification."
           "mSelectionChangeData=%s)",
           this, ToString(selectionChangeData).c_str()));

  // mSelectedRange and mRangeForWritingMode should be the range in the actual
  // content.  Therefore, they should be maintained here.  On the other hand,
  // mSelectedRangeOverride needs to keep selection range which is probably
  // expected by IME.  Therefore, we shouldn't touch the override here.

  if (!selectionChangeData.HasRange()) {
    mSelectedRange.location = NSNotFound;
    mSelectedRange.length = 0;
    mRangeForWritingMode.location = NSNotFound;
    mRangeForWritingMode.length = 0;
    return;
  }

  mWritingMode = selectionChangeData.GetWritingMode();
  mRangeForWritingMode =
      NSMakeRange(selectionChangeData.mOffset, selectionChangeData.Length());
  if (mIMEHasFocus) {
    mSelectedRange = mRangeForWritingMode;
  }
}

void IMEInputHandler::OnLayoutChange() {
  NS_OBJC_BEGIN_TRY_IGNORE_BLOCK;

  if (!IsFocused()) {
    return;
  }
  NSTextInputContext* inputContext = [mView inputContext];
  [inputContext invalidateCharacterCoordinates];

  NS_OBJC_END_TRY_IGNORE_BLOCK;
}

static NSTextCheckingType GetTextCheckingTypes() {
  NS_OBJC_BEGIN_TRY_BLOCK_RETURN;

  NSTextCheckingType types = 0;

  if (StaticPrefs::widget_macos_automatic_text_replacement() &&
      [NSSpellChecker isAutomaticTextReplacementEnabled]) {
    types |= NSTextCheckingTypeReplacement;
  }

  if (StaticPrefs::widget_macos_automatic_quote_substitution() &&
      [NSSpellChecker isAutomaticQuoteSubstitutionEnabled]) {
    types |= NSTextCheckingTypeQuote;
  }

  if (StaticPrefs::widget_macos_automatic_dash_substitution() &&
      [NSSpellChecker isAutomaticDashSubstitutionEnabled]) {
    types |= NSTextCheckingTypeDash;
  }
  return types;

  NS_OBJC_END_TRY_BLOCK_RETURN(0);
}

void IMEInputHandler::OnTextChange(const IMENotification& aIMENotification) {
  if (mIMECompositionStartInContent.isSome()) {
    mIMECompositionStartInContent =
        Some(aIMENotification.mTextChangeData.ComputeNewOffset(
            *mIMECompositionStartInContent));
  }
  HandleTextSubstitution(aIMENotification);
}

void IMEInputHandler::HandleTextSubstitution(
    const IMENotification& aIMENotification) {
  if (!StaticPrefs::widget_macos_automatic_text_replacement() &&
      !StaticPrefs::widget_macos_automatic_quote_substitution() &&
      !StaticPrefs::widget_macos_automatic_dash_substitution()) {
    return;
  }

  // Dismiss text substitution panel since this text change might be script etc
  if (!mBlockDismissTextSubstitutionPanel) {
    mProcessTextSubstitution = false;
    DismissTextSubstitutionPanel();
  }

  // Set new text substitution data to show its panel by current changed text.
  //
  // We don't handle text substitution when the following.
  // - Current input type isn't allowed. Accepted by textarea or
  // contenteditable.
  // - This inputting is by IME
  // - Current text transaction is replacement or deletion.
  // - Selection isn't collapsed

  if (!mEnableTextSubstitution) {
    return;
  }

  MOZ_LOG(gIMELog, LogLevel::Verbose,
          ("%p IMEInputHandler::HandleTextSubstitution, "
           "aIMENotification.mTextChangeData=%s",
           this, ToString(aIMENotification.mTextChangeData).c_str()));

  if (aIMENotification.mTextChangeData.mCausedOnlyByComposition) {
    return;
  }

  if (aIMENotification.mTextChangeData.mStartOffset !=
      aIMENotification.mTextChangeData.mRemovedEndOffset) {
    // Text substitution runs on adding text only
    return;
  }

  NS_DispatchToCurrentThreadQueue(
      NewRunnableMethod<uint32_t>(
          "IMEInputHandler::OnTextSubstitution", this,
          &IMEInputHandler::OnTextSubstitution,
          aIMENotification.mTextChangeData.mAddedEndOffset),
      100, EventQueuePriority::Idle);

  // OnTextSubstitution might be called on non-e10s during dispatching a key
  // events. If during it (mBlockDismissTextSubstitutionPanel is true), we have
  // to mark that text substitution panel s going to dismiss. Then after
  // dispatching a key event, it should be dismissed.
  if (mBlockDismissTextSubstitutionPanel) {
    mPendingDismissTextSubstitution = true;
  }
}

void IMEInputHandler::OnTextSubstitution(uint32_t aStartOffset) {
  NS_OBJC_BEGIN_TRY_IGNORE_BLOCK;

  MOZ_LOG_FMT(gIMELog, LogLevel::Info,
              "{} IMEInputHandler::OnTextSubstitution, aStartOffset={}",
              static_cast<void*>(this), aStartOffset);

  if (Destroyed() || !IsFocused() || !mDispatcher) [[unlikely]] {
    return;
  }

  NSTextCheckingType checkingTypes = GetTextCheckingTypes();
  if (!checkingTypes) {
    return;
  }

  // We fetch previous 10 characters to analyze text contextual for text
  // replacement.
  const uint32_t fetchLength =
      StaticPrefs::widget_macos_automatic_text_substitution_fetch_length();
  uint32_t startFetch =
      std::max<uint32_t>(fetchLength, aStartOffset) - fetchLength;
  const OwningNonNull<TextEventDispatcher> dispatcher = *mDispatcher;
  const Maybe<QueryContentResult> queryTextContentResult =
      dispatcher->QueryTextContent(startFetch, aStartOffset - startFetch);
  if (NS_WARN_IF(queryTextContentResult.isNothing()) || NS_WARN_IF(!mWidget))
      [[unlikely]] {
    return;
  }

  NSString* str;
  if (!queryTextContentResult->DataRef().IsEmpty() &&
      IsLowSurrogate(queryTextContentResult->DataRef().CharAt(0))) {
    str = nsCocoaUtils::ToNSString(
        Substring(queryTextContentResult->DataRef(), 1));
  } else {
    str = nsCocoaUtils::ToNSString(queryTextContentResult->DataRef());
  }
  NSSpellChecker* spellchecker = [NSSpellChecker sharedSpellChecker];
  if (!spellchecker) {
    return;
  }
  NSArray* results = [spellchecker checkString:str
                                         range:NSMakeRange(0, [str length])
                                         types:checkingTypes
                                       options:nil
                        inSpellDocumentWithTag:0
                                   orthography:nil
                                     wordCount:nil];
  if (!results.count) {
    return;
  }

  MOZ_LOG_FMT(gIMELog, LogLevel::Info,
              "{}   IMEInputHandler::HandleTextSubstitution, found text "
              "substitution for correction.",
              static_cast<void*>(this));

  NSTextCheckingResult* candidate = nullptr;
  for (NSTextCheckingResult* checkingResult in results) {
    NSRange candidateRange = checkingResult.range;
    if (!NSLocationInRange(
            aStartOffset - startFetch,
            NSMakeRange(candidateRange.location, candidateRange.length + 1))) {
      continue;
    }
    if (checkingResult.resultType &
        (NSTextCheckingTypeQuote | NSTextCheckingTypeDash)) {
      // XXX We might have to adjust caret position on out of loop.
      //     But dash/quote replacement is on caret position now.
      const nsDependentSubstring& originalStr =
          Substring(queryTextContentResult->DataRef(), candidateRange.location,
                    candidateRange.length);
      candidateRange.location += startFetch;
      ReplaceTextForTextSubstitution(originalStr,
                                     checkingResult.replacementString,
                                     candidateRange, PreventSetSelection::No);
      if (NS_WARN_IF(!mWidget)) [[unlikely]] {
        return;
      }
      continue;
    }
    // Found text replacement data.
    candidate = checkingResult;
    break;
  }
  if (!candidate) {
    return;
  }

  // NSTextCheckingResult.range is read only, so re-create this result object.
  NSRange candidatedRange = NSMakeRange(candidate.range.location + startFetch,
                                        candidate.range.length);
  [mCandidatedTextSubstitutionResult release];
  mCandidatedTextSubstitutionResult = [[NSTextCheckingResult
      correctionCheckingResultWithRange:candidatedRange
                      replacementString:candidate.replacementString
                     alternativeStrings:candidate.alternativeStrings] retain];
  mOriginalTextForTextSubstitution =
      Substring(queryTextContentResult->DataRef(),
                candidatedRange.location - startFetch, candidatedRange.length);

  // Allow to work text substitution for mouse operation. If any key event is
  // dispatched, mProcessTextSubstitution will be updated.
  mProcessTextSubstitution = true;

  NS_OBJC_END_TRY_IGNORE_BLOCK;
}

void IMEInputHandler::ShowTextSubstitutionPanel() {
  NS_OBJC_BEGIN_TRY_IGNORE_BLOCK;

  MOZ_LOG_FMT(
      gIMELog, LogLevel::Info,
      "{} IMEInputHandler::ShowTextSubstitutionPanel, "
      "mCandidatedTextSubstitutionResult={}, mWidget={}, mDispatcher={}",
      static_cast<void*>(this),
      static_cast<void*>(mCandidatedTextSubstitutionResult),
      static_cast<void*>(mWidget), static_cast<void*>(mDispatcher));

  if (!mCandidatedTextSubstitutionResult || !mWidget || !mDispatcher)
      [[unlikely]] {
    return;
  }
  const OwningNonNull<TextEventDispatcher> dispatcher = *mDispatcher;

  // We won't get text rect of non-composition string on e10s. So we get caret
  // rect instead.
  NSRect rect = [&]() {
    CheckedUint32 offset(mCandidatedTextSubstitutionResult.range.location);
    offset += mCandidatedTextSubstitutionResult.range.length;
    if (!offset.isValid()) [[unlikely]] {
      return NSMakeRect(0, 0, 0, 0);
    }
    const Maybe<QueryContentResult> queryCaretRectResult =
        dispatcher->QueryCaretRect(offset.value());
    if (queryCaretRectResult.isNothing() || !mWidget) [[unlikely]] {
      return NSMakeRect(0, 0, 0, 0);
    }
    return nsCocoaUtils::DevPixelsToCocoaPoints(queryCaretRectResult->mRect,
                                                mWidget->BackingScaleFactor());
  }();

  if (Destroyed()) [[unlikely]] {
    return;
  }

  uint32_t replacementLocation =
      mCandidatedTextSubstitutionResult.range.location;
  NSSpellChecker* spellchecker = [NSSpellChecker sharedSpellChecker];
  if (!spellchecker) [[unlikely]] {
    return;
  }
  [spellchecker
      showCorrectionIndicatorOfType:NSCorrectionIndicatorTypeDefault
                      primaryString:mCandidatedTextSubstitutionResult
                                        .replacementString
                 alternativeStrings:mCandidatedTextSubstitutionResult
                                        .alternativeStrings
                    forStringInRect:rect
                               view:mView
                  completionHandler:^(NSString* aAcceptedString) {
                    // This is always called when panel is closed.
                    MOZ_LOG_FMT(
                        gIMELog, LogLevel::Info,
                        "{}   IMEInputHandler::ShowTextSubstitutionPanel, "
                        "dismissing correction panel",
                        static_cast<void*>(this));

                    if (!mProcessTextSubstitution) {
                      return;
                    }

                    mProcessTextSubstitution = false;

                    // Replace text when this panel is closed by space or return
                    // key.
                    if (!aAcceptedString || Destroyed() || !IsFocused() ||
                        mOriginalTextForTextSubstitution.IsEmpty()) {
                      return;
                    }

                    NSRange replacementRange =
                        NSMakeRange(replacementLocation,
                                    mOriginalTextForTextSubstitution.Length());

                    // Do replace text then set caret to new position
                    ReplaceTextForTextSubstitution(
                        mOriginalTextForTextSubstitution, aAcceptedString,
                        replacementRange, PreventSetSelection::Yes);

                    mOriginalTextForTextSubstitution.Truncate();
                  }];

  [mCandidatedTextSubstitutionResult release];
  mCandidatedTextSubstitutionResult = nullptr;

  NS_OBJC_END_TRY_IGNORE_BLOCK;
}

void IMEInputHandler::DismissTextSubstitutionPanel() {
  NS_OBJC_BEGIN_TRY_IGNORE_BLOCK;

  MOZ_LOG(gIMELog, LogLevel::Info,
          ("%p IMEInputHandler::DismissTextSubstitutionPanel, "
           "mProcessTextSubstitution=%s",
           this, mProcessTextSubstitution ? "true" : "false"));

  NSSpellChecker* spellchecker = [NSSpellChecker sharedSpellChecker];
  if (!spellchecker) {
    return;
  }
  [spellchecker dismissCorrectionIndicatorForView:mView];

  if (mCandidatedTextSubstitutionResult) {
    [mCandidatedTextSubstitutionResult release];
    mCandidatedTextSubstitutionResult = nullptr;
  }

  NS_OBJC_END_TRY_IGNORE_BLOCK;
}

void IMEInputHandler::ReplaceTextForTextSubstitution(
    const nsAString& aOriginalString, NSString* aNewString,
    const NSRange& aRange, PreventSetSelection aPreventSetSelection) {
  nsAutoString insertStr;
  nsCocoaUtils::GetStringForNSString(aNewString, insertStr);

  MOZ_LOG(gIMELog, LogLevel::Info,
          ("%p IMEInputHandler::ReplaceTextForTextsubstitution, "
           "aOriginalString=\"%s\", aNewString=\"%s\", aRange=%s",
           this, NS_ConvertUTF16toUTF8(aOriginalString).get(),
           NS_ConvertUTF16toUTF8(insertStr).get(), ToString(aRange).c_str()));

  const RefPtr<TextEventDispatcher> dispatcher(mDispatcher);
  MOZ_ASSERT(dispatcher);
  const Result<bool, nsresult> replaceTextResult =
      dispatcher->DispatchReplaceTextCommandEvent(
          insertStr, aOriginalString, aRange.location, aPreventSetSelection);
  if (replaceTextResult.isErr() || Destroyed()) [[unlikely]] {
    MOZ_LOG(
        gIMELog, LogLevel::Error,
        ("%p   IMEInputHandler::ReplaceTextForTextsubstitution, FAILED", this));
  }
}

bool IMEInputHandler::OnHandleEvent(NSEvent* aEvent) {
  if (!IsFocused()) {
    return false;
  }

  bool allowConsumeEvent = true;
  if (!IsIMEComposing()) {
    // Hack for bug of Korean IMEs on Catalina (10.15).
    // If we are inactivated during composition, active Korean IME keeps
    // consuming all mousedown events of any mouse buttons.  So, we should
    // allow Korean IMEs to handle mousedown events only when there is
    // composition string.
    // List of ID of Korean IME:
    // * com.apple.inputmethod.Korean.2SetKorean
    // * com.apple.inputmethod.Korean.3SetKorean
    // * com.apple.inputmethod.Korean.390Sebulshik
    // * com.apple.inputmethod.Korean.GongjinCheongRomaja
    // * com.apple.inputmethod.Korean.HNCRomaja
    TISInputSourceWrapper tis;
    tis.InitByCurrentInputSource();
    nsAutoString inputSourceID;
    tis.GetInputSourceID(inputSourceID);
    allowConsumeEvent =
        !StringBeginsWith(inputSourceID, u"com.apple.inputmethod.Korean."_ns);
  }
  NSTextInputContext* inputContext = [mView inputContext];
  return [inputContext handleEvent:aEvent] && allowConsumeEvent;
}

#pragma mark -

/******************************************************************************
 *
 *  TextInputHandlerBase implementation
 *
 ******************************************************************************/

int32_t TextInputHandlerBase::sSecureEventInputCount = 0;

NS_IMPL_ISUPPORTS(TextInputHandlerBase, TextEventDispatcherListener,
                  nsISupportsWeakReference)

TextInputHandlerBase::TextInputHandlerBase(nsCocoaWindow* aWidget,
                                           NSView<mozView>* aNativeView)
    : mWidget(aWidget), mDispatcher(aWidget->GetTextEventDispatcher()) {
  gHandlerInstanceCount++;
  mView = [aNativeView retain];
}

TextInputHandlerBase::~TextInputHandlerBase() {
  [mView release];
  if (--gHandlerInstanceCount == 0) {
    TISInputSourceWrapper::Shutdown();
  }
}

bool TextInputHandlerBase::OnDestroyWidget(nsCocoaWindow* aDestroyingWidget) {
  MOZ_LOG_KEY_OR_IME(LogLevel::Info,
                     ("%p TextInputHandlerBase::OnDestroyWidget, "
                      "aDestroyingWidget=%p, mWidget=%p",
                      this, aDestroyingWidget, mWidget));

  if (aDestroyingWidget != mWidget) {
    return false;
  }

  mWidget = nullptr;
  mDispatcher = nullptr;
  return true;
}

bool TextInputHandlerBase::DispatchEvent(WidgetGUIEvent& aEvent) {
  return mWidget->DispatchWindowEvent(aEvent);
}

void TextInputHandlerBase::InitKeyEvent(NSEvent* aNativeKeyEvent,
                                        WidgetKeyboardEvent& aKeyEvent,
                                        bool aIsProcessedByIME,
                                        const nsAString* aInsertString) {
  NS_ASSERTION(aNativeKeyEvent, "aNativeKeyEvent must not be NULL");

  if (mKeyboardOverride.mOverrideEnabled) {
    TISInputSourceWrapper tis;
    tis.InitByLayoutID(mKeyboardOverride.mKeyboardLayout, true);
    tis.InitKeyEvent(aNativeKeyEvent, aKeyEvent, aIsProcessedByIME,
                     aInsertString);
    return;
  }
  TISInputSourceWrapper::CurrentInputSource().InitKeyEvent(
      aNativeKeyEvent, aKeyEvent, aIsProcessedByIME, aInsertString);
}

nsresult TextInputHandlerBase::SynthesizeNativeKeyEvent(
    int32_t aNativeKeyboardLayout, int32_t aNativeKeyCode,
    nsIWidget::NativeModifiers aModifierFlags, const nsAString& aCharacters,
    const nsAString& aUnmodifiedCharacters) {
  NS_OBJC_BEGIN_TRY_BLOCK_RETURN;

  uint32_t modifierFlags =
      nsCocoaUtils::ConvertWidgetModifiersToMacModifierFlags(aModifierFlags);
  NSInteger windowNumber = [[mView window] windowNumber];
  bool sendFlagsChangedEvent = IsModifierKey(aNativeKeyCode);
  NSEventType eventType =
      sendFlagsChangedEvent ? NSEventTypeFlagsChanged : NSEventTypeKeyDown;
  NSEvent* downEvent = [NSEvent
                 keyEventWithType:eventType
                         location:NSMakePoint(0, 0)
                    modifierFlags:modifierFlags
                        timestamp:0
                     windowNumber:windowNumber
                          context:nil
                       characters:XPCOMStringToNSString(aCharacters)
      charactersIgnoringModifiers:XPCOMStringToNSString(aUnmodifiedCharacters)
                        isARepeat:NO
                          keyCode:aNativeKeyCode];

  NSEvent* upEvent = sendFlagsChangedEvent
                         ? nil
                         : nsCocoaUtils::MakeNewCocoaEventWithType(
                               NSEventTypeKeyUp, downEvent);

  if (downEvent && (sendFlagsChangedEvent || upEvent)) {
    KeyboardLayoutOverride currentLayout = mKeyboardOverride;
    mKeyboardOverride.mKeyboardLayout = aNativeKeyboardLayout;
    mKeyboardOverride.mOverrideEnabled = true;
    [NSApp sendEvent:downEvent];
    if (upEvent) {
      [NSApp sendEvent:upEvent];
    }
    // processKeyDownEvent and keyUp block exceptions so we're sure to
    // reach here to restore mKeyboardOverride
    mKeyboardOverride = currentLayout;
  }

  return NS_OK;

  NS_OBJC_END_TRY_BLOCK_RETURN(NS_ERROR_FAILURE);
}

NSInteger TextInputHandlerBase::GetWindowLevel() {
  NS_OBJC_BEGIN_TRY_BLOCK_RETURN;

  MOZ_LOG_KEY_OR_IME(LogLevel::Info,
                     ("%p TextInputHandlerBase::GetWindowLevel, Destroyed()=%s",
                      this, TrueOrFalse(Destroyed())));

  if (Destroyed()) {
    return NSNormalWindowLevel;
  }

  // When an <input> element on a XUL <panel> is focused, the actual focused
  // view is the panel's parent view (mView). But the editor is displayed on the
  // popped-up widget's view (editorView).  We want the latter's window level.
  NSView<mozView>* editorView = mWidget->GetEditorView();
  NS_ENSURE_TRUE(editorView, NSNormalWindowLevel);
  NSInteger windowLevel = [[editorView window] level];

  MOZ_LOG_KEY_OR_IME(
      LogLevel::Info,
      ("%p TextInputHandlerBase::GetWindowLevel, windowLevel=%s (%lX)", this,
       GetWindowLevelName(windowLevel),
       static_cast<unsigned long>(windowLevel)));

  return windowLevel;

  NS_OBJC_END_TRY_BLOCK_RETURN(NSNormalWindowLevel);
}

NS_IMETHODIMP
TextInputHandlerBase::AttachNativeKeyEvent(WidgetKeyboardEvent& aKeyEvent) {
  NS_OBJC_BEGIN_TRY_BLOCK_RETURN;

  // Don't try to replace a native event if one already exists.
  // macOS doesn't have an OS modifier, can't make a native event.
  if (aKeyEvent.mNativeKeyEvent) {
    return NS_OK;
  }

  MOZ_LOG_KEY_OR_IME(
      LogLevel::Info,
      ("%p TextInputHandlerBase::AttachNativeKeyEvent, key=0x%X, char=0x%X, "
       "mod=0x%X",
       this, aKeyEvent.mKeyCode, aKeyEvent.mCharCode, aKeyEvent.mModifiers));

  NSInteger windowNumber = [[mView window] windowNumber];
  NSGraphicsContext* context = [NSGraphicsContext currentContext];
  aKeyEvent.mNativeKeyEvent = nsCocoaUtils::MakeNewCocoaEventFromWidgetEvent(
      aKeyEvent, windowNumber, context);

  return NS_OK;

  NS_OBJC_END_TRY_BLOCK_RETURN(NS_ERROR_FAILURE);
}

bool TextInputHandlerBase::SetSelection(NSRange& aRange) {
  MOZ_ASSERT(!Destroyed());
  MOZ_ASSERT(mDispatcher);
  MOZ_ASSERT(mDispatcher->GetWidget() == mWidget);

  RefPtr<TextEventDispatcher> dispatcher = mDispatcher;
  NS_ENSURE_TRUE(dispatcher->DispatchSetSelectionEvent(
                     aRange.location, aRange.length,
                     ExpandToClusterBoundary::No, RangeDirection::Normal),
                 false);
  return !Destroyed();
}

/* static */ bool TextInputHandlerBase::IsPrintableChar(char16_t aChar) {
  return (aChar >= 0x20 && aChar <= 0x7E) || aChar >= 0xA0;
}

/* static */ bool TextInputHandlerBase::IsSpecialGeckoKey(
    UInt32 aNativeKeyCode) {
  // this table is used to determine which keys are special and should not
  // generate a charCode
  switch (aNativeKeyCode) {
    // modifiers - we don't get separate events for these yet
    case kVK_Escape:
    case kVK_Shift:
    case kVK_RightShift:
    case kVK_Command:
    case kVK_RightCommand:
    case kVK_CapsLock:
    case kVK_Control:
    case kVK_RightControl:
    case kVK_Option:
    case kVK_RightOption:
    case kVK_ANSI_KeypadClear:
    case kVK_Function:

    // function keys
    case kVK_F1:
    case kVK_F2:
    case kVK_F3:
    case kVK_F4:
    case kVK_F5:
    case kVK_F6:
    case kVK_F7:
    case kVK_F8:
    case kVK_F9:
    case kVK_F10:
    case kVK_F11:
    case kVK_F12:
    case kVK_PC_Pause:
    case kVK_PC_ScrollLock:
    case kVK_PC_PrintScreen:
    case kVK_F16:
    case kVK_F17:
    case kVK_F18:
    case kVK_F19:

    case kVK_PC_Insert:
    case kVK_PC_Delete:
    case kVK_Tab:
    case kVK_PC_Backspace:
    case kVK_PC_ContextMenu:

    case kVK_JIS_Eisu:
    case kVK_JIS_Kana:

    case kVK_Home:
    case kVK_End:
    case kVK_PageUp:
    case kVK_PageDown:
    case kVK_LeftArrow:
    case kVK_RightArrow:
    case kVK_UpArrow:
    case kVK_DownArrow:
    case kVK_Return:
    case kVK_ANSI_KeypadEnter:
    case kVK_Powerbook_KeypadEnter:
      return true;
  }
  return false;
}

/* static */ bool TextInputHandlerBase::IsNormalCharInputtingEvent(
    NSEvent* aNativeEvent) {
  if ([aNativeEvent type] != NSEventTypeKeyDown &&
      [aNativeEvent type] != NSEventTypeKeyUp) {
    return false;
  }
  nsAutoString nativeChars;
  CopyNSStringToXPCOMString([aNativeEvent characters], nativeChars);

  // this is not character inputting event, simply.
  if (nativeChars.IsEmpty() ||
      ([aNativeEvent modifierFlags] & NSEventModifierFlagCommand)) {
    return false;
  }
  return !IsControlChar(nativeChars[0]);
}

/* static */ bool TextInputHandlerBase::IsModifierKey(UInt32 aNativeKeyCode) {
  switch (aNativeKeyCode) {
    case kVK_CapsLock:
    case kVK_RightCommand:
    case kVK_Command:
    case kVK_Shift:
    case kVK_Option:
    case kVK_Control:
    case kVK_RightShift:
    case kVK_RightOption:
    case kVK_RightControl:
    case kVK_Function:
      return true;
  }
  return false;
}

/* static */ void TextInputHandlerBase::EnableSecureEventInput() {
  sSecureEventInputCount++;
  ::EnableSecureEventInput();
  MOZ_LOG(gIMELog, LogLevel::Debug,
          ("EnableSecureEventInput() called (%d)", sSecureEventInputCount));
}

/* static */ void TextInputHandlerBase::DisableSecureEventInput() {
  if (!sSecureEventInputCount) {
    return;
  }
  sSecureEventInputCount--;
  ::DisableSecureEventInput();
  MOZ_LOG(gIMELog, LogLevel::Debug,
          ("DisableSecureEventInput() called (%d)", sSecureEventInputCount));
}

/* static */ bool TextInputHandlerBase::IsSecureEventInputEnabled() {
  // sSecureEventInputCount is our mechanism to track when Secure Event Input
  //   is enabled. Non-zero indicates we have enabled Secure Input. But
  //   zero does not mean that Secure Input is _disabled_ because another
  //   application may have enabled it.  If the OS reports Secure Event
  //   Input is disabled though, a non-zero sSecureEventInputCount is an error.
  NS_ASSERTION(::IsSecureEventInputEnabled() || 0 == sSecureEventInputCount,
               "sSecureEventInputCount is not zero when the OS thinks "
               "SecureEventInput is disabled.");
  return !!sSecureEventInputCount;
}

/* static */ void TextInputHandlerBase::EnsureSecureEventInputDisabled() {
  while (sSecureEventInputCount) {
    TextInputHandlerBase::DisableSecureEventInput();
  }
}

#pragma mark -

/******************************************************************************
 *
 *  TextInputHandlerBase::KeyEventState implementation
 *
 ******************************************************************************/

void TextInputHandlerBase::KeyEventState::InitKeyEvent(
    TextInputHandlerBase* aHandler, WidgetKeyboardEvent& aKeyEvent,
    bool aIsProcessedByIME) {
  NS_OBJC_BEGIN_TRY_IGNORE_BLOCK;

  MOZ_ASSERT(aHandler);
  MOZ_RELEASE_ASSERT(mKeyEvent);

  NSEvent* nativeEvent = mKeyEvent;
  if (!mInsertedString.IsEmpty()) {
    nsAutoString unhandledString;
    GetUnhandledString(unhandledString);
    NSString* unhandledNSString = XPCOMStringToNSString(unhandledString);
    // If the key event's some characters were already handled by
    // InsertString() calls, we need to create a dummy event which doesn't
    // include the handled characters.
    nativeEvent =
        [NSEvent keyEventWithType:[mKeyEvent type]
                               location:[mKeyEvent locationInWindow]
                          modifierFlags:[mKeyEvent modifierFlags]
                              timestamp:[mKeyEvent timestamp]
                           windowNumber:[mKeyEvent windowNumber]
                                context:nil
                             characters:unhandledNSString
            charactersIgnoringModifiers:[mKeyEvent charactersIgnoringModifiers]
                              isARepeat:[mKeyEvent isARepeat]
                                keyCode:[mKeyEvent keyCode]];
  }

  aKeyEvent.mUniqueId = mUniqueId;
  aHandler->InitKeyEvent(nativeEvent, aKeyEvent, aIsProcessedByIME,
                         mInsertString);

  NS_OBJC_END_TRY_IGNORE_BLOCK;
}

void TextInputHandlerBase::KeyEventState::GetUnhandledString(
    nsAString& aUnhandledString) const {
  aUnhandledString.Truncate();
  if (NS_WARN_IF(!mKeyEvent)) {
    return;
  }
  nsAutoString characters;
  CopyNSStringToXPCOMString([mKeyEvent characters], characters);
  if (characters.IsEmpty()) {
    return;
  }
  if (mInsertedString.IsEmpty()) {
    aUnhandledString = characters;
    return;
  }

  // The insertes string must match with the start of characters.
  MOZ_ASSERT(StringBeginsWith(characters, mInsertedString));

  aUnhandledString = nsDependentSubstring(characters, mInsertedString.Length());
}

#pragma mark -

/******************************************************************************
 *
 *  TextInputHandlerBase::AutoInsertStringClearer implementation
 *
 ******************************************************************************/

TextInputHandlerBase::AutoInsertStringClearer::~AutoInsertStringClearer() {
  if (mState && mState->mInsertString) {
    // If inserting string is a part of characters of the event,
    // we should record it as inserted string.
    nsAutoString characters;
    CopyNSStringToXPCOMString([mState->mKeyEvent characters], characters);
    nsAutoString insertedString(mState->mInsertedString);
    insertedString += *mState->mInsertString;
    if (StringBeginsWith(characters, insertedString)) {
      mState->mInsertedString = insertedString;
    }
  }
  if (mState) {
    mState->mInsertString = nullptr;
  }
}

#undef MOZ_LOG_KEY_OR_IME
