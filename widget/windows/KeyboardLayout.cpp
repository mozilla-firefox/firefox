/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "KeyboardLayout.h"

#include "mozilla/AutoRestore.h"
#include "mozilla/Logging.h"
#include "mozilla/MouseEvents.h"
#include "mozilla/MiscEvents.h"
#include "mozilla/RefPtr.h"
#include "mozilla/StaticPrefs_ui.h"
#include "mozilla/StaticPtr.h"
#include "mozilla/TextEvents.h"
#include "mozilla/widget/WinRegistry.h"

#include "nsExceptionHandler.h"
#include "nsGkAtoms.h"
#include "nsIUserIdleServiceInternal.h"
#include "nsIWindowsRegKey.h"
#include "nsPrintfCString.h"
#include "nsReadableUtils.h"
#include "nsServiceManagerUtils.h"
#include "nsTArray.h"
#include "nsUnicharUtils.h"
#include "nsWindowDbg.h"

#include "WidgetUtils.h"
#include "WinUtils.h"
#include "InputFilter.h"

#include "npapi.h"

#include <windows.h>
#include <winnls.h>
#include <winuser.h>
#include <algorithm>

#ifndef WINABLEAPI
#  include <winable.h>
#endif

// For collecting other people's log, tell them `MOZ_LOG=KeyboardHandler:4,sync`
// rather than `MOZ_LOG=KeyboardHandler:5,sync` since using `5` may create too
// big file.
// Therefore you shouldn't use `LogLevel::Verbose` for logging usual behavior.
mozilla::LazyLogModule gKeyLog("KeyboardHandler");

namespace mozilla {
namespace widget {

static const char* const kVirtualKeyName[] = {
    "NULL",
    "VK_LBUTTON",
    "VK_RBUTTON",
    "VK_CANCEL",
    "VK_MBUTTON",
    "VK_XBUTTON1",
    "VK_XBUTTON2",
    "0x07",
    "VK_BACK",
    "VK_TAB",
    "0x0A",
    "0x0B",
    "VK_CLEAR",
    "VK_RETURN",
    "0x0E",
    "0x0F",

    "VK_SHIFT",
    "VK_CONTROL",
    "VK_MENU",
    "VK_PAUSE",
    "VK_CAPITAL",
    "VK_KANA, VK_HANGUL",
    "0x16",
    "VK_JUNJA",
    "VK_FINAL",
    "VK_HANJA, VK_KANJI",
    "0x1A",
    "VK_ESCAPE",
    "VK_CONVERT",
    "VK_NONCONVERT",
    "VK_ACCEPT",
    "VK_MODECHANGE",

    "VK_SPACE",
    "VK_PRIOR",
    "VK_NEXT",
    "VK_END",
    "VK_HOME",
    "VK_LEFT",
    "VK_UP",
    "VK_RIGHT",
    "VK_DOWN",
    "VK_SELECT",
    "VK_PRINT",
    "VK_EXECUTE",
    "VK_SNAPSHOT",
    "VK_INSERT",
    "VK_DELETE",
    "VK_HELP",

    "VK_0",
    "VK_1",
    "VK_2",
    "VK_3",
    "VK_4",
    "VK_5",
    "VK_6",
    "VK_7",
    "VK_8",
    "VK_9",
    "0x3A",
    "0x3B",
    "0x3C",
    "0x3D",
    "0x3E",
    "0x3F",

    "0x40",
    "VK_A",
    "VK_B",
    "VK_C",
    "VK_D",
    "VK_E",
    "VK_F",
    "VK_G",
    "VK_H",
    "VK_I",
    "VK_J",
    "VK_K",
    "VK_L",
    "VK_M",
    "VK_N",
    "VK_O",

    "VK_P",
    "VK_Q",
    "VK_R",
    "VK_S",
    "VK_T",
    "VK_U",
    "VK_V",
    "VK_W",
    "VK_X",
    "VK_Y",
    "VK_Z",
    "VK_LWIN",
    "VK_RWIN",
    "VK_APPS",
    "0x5E",
    "VK_SLEEP",

    "VK_NUMPAD0",
    "VK_NUMPAD1",
    "VK_NUMPAD2",
    "VK_NUMPAD3",
    "VK_NUMPAD4",
    "VK_NUMPAD5",
    "VK_NUMPAD6",
    "VK_NUMPAD7",
    "VK_NUMPAD8",
    "VK_NUMPAD9",
    "VK_MULTIPLY",
    "VK_ADD",
    "VK_SEPARATOR",
    "VK_SUBTRACT",
    "VK_DECIMAL",
    "VK_DIVIDE",

    "VK_F1",
    "VK_F2",
    "VK_F3",
    "VK_F4",
    "VK_F5",
    "VK_F6",
    "VK_F7",
    "VK_F8",
    "VK_F9",
    "VK_F10",
    "VK_F11",
    "VK_F12",
    "VK_F13",
    "VK_F14",
    "VK_F15",
    "VK_F16",

    "VK_F17",
    "VK_F18",
    "VK_F19",
    "VK_F20",
    "VK_F21",
    "VK_F22",
    "VK_F23",
    "VK_F24",
    "0x88",
    "0x89",
    "0x8A",
    "0x8B",
    "0x8C",
    "0x8D",
    "0x8E",
    "0x8F",

    "VK_NUMLOCK",
    "VK_SCROLL",
    "VK_OEM_NEC_EQUAL, VK_OEM_FJ_JISHO",
    "VK_OEM_FJ_MASSHOU",
    "VK_OEM_FJ_TOUROKU",
    "VK_OEM_FJ_LOYA",
    "VK_OEM_FJ_ROYA",
    "0x97",
    "0x98",
    "0x99",
    "0x9A",
    "0x9B",
    "0x9C",
    "0x9D",
    "0x9E",
    "0x9F",

    "VK_LSHIFT",
    "VK_RSHIFT",
    "VK_LCONTROL",
    "VK_RCONTROL",
    "VK_LMENU",
    "VK_RMENU",
    "VK_BROWSER_BACK",
    "VK_BROWSER_FORWARD",
    "VK_BROWSER_REFRESH",
    "VK_BROWSER_STOP",
    "VK_BROWSER_SEARCH",
    "VK_BROWSER_FAVORITES",
    "VK_BROWSER_HOME",
    "VK_VOLUME_MUTE",
    "VK_VOLUME_DOWN",
    "VK_VOLUME_UP",

    "VK_MEDIA_NEXT_TRACK",
    "VK_MEDIA_PREV_TRACK",
    "VK_MEDIA_STOP",
    "VK_MEDIA_PLAY_PAUSE",
    "VK_LAUNCH_MAIL",
    "VK_LAUNCH_MEDIA_SELECT",
    "VK_LAUNCH_APP1",
    "VK_LAUNCH_APP2",
    "0xB8",
    "0xB9",
    "VK_OEM_1",
    "VK_OEM_PLUS",
    "VK_OEM_COMMA",
    "VK_OEM_MINUS",
    "VK_OEM_PERIOD",
    "VK_OEM_2",

    "VK_OEM_3",
    "VK_ABNT_C1",
    "VK_ABNT_C2",
    "0xC3",
    "0xC4",
    "0xC5",
    "0xC6",
    "0xC7",
    "0xC8",
    "0xC9",
    "0xCA",
    "0xCB",
    "0xCC",
    "0xCD",
    "0xCE",
    "0xCF",

    "0xD0",
    "0xD1",
    "0xD2",
    "0xD3",
    "0xD4",
    "0xD5",
    "0xD6",
    "0xD7",
    "0xD8",
    "0xD9",
    "0xDA",
    "VK_OEM_4",
    "VK_OEM_5",
    "VK_OEM_6",
    "VK_OEM_7",
    "VK_OEM_8",

    "0xE0",
    "VK_OEM_AX",
    "VK_OEM_102",
    "VK_ICO_HELP",
    "VK_ICO_00",
    "VK_PROCESSKEY",
    "VK_ICO_CLEAR",
    "VK_PACKET",
    "0xE8",
    "VK_OEM_RESET",
    "VK_OEM_JUMP",
    "VK_OEM_PA1",
    "VK_OEM_PA2",
    "VK_OEM_PA3",
    "VK_OEM_WSCTRL",
    "VK_OEM_CUSEL",

    "VK_OEM_ATTN",
    "VK_OEM_FINISH",
    "VK_OEM_COPY",
    "VK_OEM_AUTO",
    "VK_OEM_ENLW",
    "VK_OEM_BACKTAB",
    "VK_ATTN",
    "VK_CRSEL",
    "VK_EXSEL",
    "VK_EREOF",
    "VK_PLAY",
    "VK_ZOOM",
    "VK_NONAME",
    "VK_PA1",
    "VK_OEM_CLEAR",
    "0xFF"};

static_assert(sizeof(kVirtualKeyName) / sizeof(const char*) == 0x100,
              "The virtual key name must be defined just 256 keys");

static const nsCString GetCharacterCodeName(WPARAM aCharCode) {
  switch (aCharCode) {
    case 0x0000:
      return "NULL (0x0000)"_ns;
    case 0x0008:
      return "BACKSPACE (0x0008)"_ns;
    case 0x0009:
      return "CHARACTER TABULATION (0x0009)"_ns;
    case 0x000A:
      return "LINE FEED (0x000A)"_ns;
    case 0x000B:
      return "LINE TABULATION (0x000B)"_ns;
    case 0x000C:
      return "FORM FEED (0x000C)"_ns;
    case 0x000D:
      return "CARRIAGE RETURN (0x000D)"_ns;
    case 0x0018:
      return "CANCEL (0x0018)"_ns;
    case 0x001B:
      return "ESCAPE (0x001B)"_ns;
    case 0x0020:
      return "SPACE (0x0020)"_ns;
    case 0x007F:
      return "DELETE (0x007F)"_ns;
    case 0x00A0:
      return "NO-BREAK SPACE (0x00A0)"_ns;
    case 0x00AD:
      return "SOFT HYPHEN (0x00AD)"_ns;
    case 0x2000:
      return "EN QUAD (0x2000)"_ns;
    case 0x2001:
      return "EM QUAD (0x2001)"_ns;
    case 0x2002:
      return "EN SPACE (0x2002)"_ns;
    case 0x2003:
      return "EM SPACE (0x2003)"_ns;
    case 0x2004:
      return "THREE-PER-EM SPACE (0x2004)"_ns;
    case 0x2005:
      return "FOUR-PER-EM SPACE (0x2005)"_ns;
    case 0x2006:
      return "SIX-PER-EM SPACE (0x2006)"_ns;
    case 0x2007:
      return "FIGURE SPACE (0x2007)"_ns;
    case 0x2008:
      return "PUNCTUATION SPACE (0x2008)"_ns;
    case 0x2009:
      return "THIN SPACE (0x2009)"_ns;
    case 0x200A:
      return "HAIR SPACE (0x200A)"_ns;
    case 0x200B:
      return "ZERO WIDTH SPACE (0x200B)"_ns;
    case 0x200C:
      return "ZERO WIDTH NON-JOINER (0x200C)"_ns;
    case 0x200D:
      return "ZERO WIDTH JOINER (0x200D)"_ns;
    case 0x200E:
      return "LEFT-TO-RIGHT MARK (0x200E)"_ns;
    case 0x200F:
      return "RIGHT-TO-LEFT MARK (0x200F)"_ns;
    case 0x2029:
      return "PARAGRAPH SEPARATOR (0x2029)"_ns;
    case 0x202A:
      return "LEFT-TO-RIGHT EMBEDDING (0x202A)"_ns;
    case 0x202B:
      return "RIGHT-TO-LEFT EMBEDDING (0x202B)"_ns;
    case 0x202D:
      return "LEFT-TO-RIGHT OVERRIDE (0x202D)"_ns;
    case 0x202E:
      return "RIGHT-TO-LEFT OVERRIDE (0x202E)"_ns;
    case 0x202F:
      return "NARROW NO-BREAK SPACE (0x202F)"_ns;
    case 0x205F:
      return "MEDIUM MATHEMATICAL SPACE (0x205F)"_ns;
    case 0x2060:
      return "WORD JOINER (0x2060)"_ns;
    case 0x2066:
      return "LEFT-TO-RIGHT ISOLATE (0x2066)"_ns;
    case 0x2067:
      return "RIGHT-TO-LEFT ISOLATE (0x2067)"_ns;
    case 0x3000:
      return "IDEOGRAPHIC SPACE (0x3000)"_ns;
    case 0xFEFF:
      return "ZERO WIDTH NO-BREAK SPACE (0xFEFF)"_ns;
    default: {
      if (aCharCode < ' ' || (aCharCode >= 0x80 && aCharCode < 0xA0)) {
        return nsPrintfCString("control (0x%04zX)", aCharCode);
      }
      if (NS_IS_HIGH_SURROGATE(aCharCode)) {
        return nsPrintfCString("high surrogate (0x%04zX)", aCharCode);
      }
      if (NS_IS_LOW_SURROGATE(aCharCode)) {
        return nsPrintfCString("low surrogate (0x%04zX)", aCharCode);
      }
      return IS_IN_BMP(aCharCode)
                 ? nsPrintfCString(
                       "'%s' (0x%04zX)",
                       NS_ConvertUTF16toUTF8(nsAutoString(aCharCode)).get(),
                       aCharCode)
                 : nsPrintfCString(
                       "'%s' (0x%08zX)",
                       NS_ConvertUTF16toUTF8(nsAutoString(aCharCode)).get(),
                       aCharCode);
    }
  }
}

static const nsCString GetKeyLocationName(uint32_t aLocation) {
  switch (aLocation) {
    case eKeyLocationLeft:
      return "KEY_LOCATION_LEFT"_ns;
    case eKeyLocationRight:
      return "KEY_LOCATION_RIGHT"_ns;
    case eKeyLocationStandard:
      return "KEY_LOCATION_STANDARD"_ns;
    case eKeyLocationNumpad:
      return "KEY_LOCATION_NUMPAD"_ns;
    default:
      return nsPrintfCString("Unknown (0x%04X)", aLocation);
  }
}

static const nsCString GetCharacterCodeNames(const char16_t* aChars,
                                             uint32_t aLength) {
  if (!aLength) {
    return ""_ns;
  }
  nsCString result;
  result.AssignLiteral("\"");
  StringJoinAppend(result, ", "_ns, Span{aChars, aLength},
                   [](nsACString& dest, const char16_t charValue) {
                     dest.Append(GetCharacterCodeName(charValue));
                   });
  result.AppendLiteral("\"");
  return result;
}

static const nsCString GetCharacterCodeNames(
    const UniCharsAndModifiers& aUniCharsAndModifiers) {
  if (aUniCharsAndModifiers.IsEmpty()) {
    return ""_ns;
  }
  nsCString result;
  result.AssignLiteral("\"");
  StringJoinAppend(result, ", "_ns, Span{aUniCharsAndModifiers.ToString()},
                   [](nsACString& dest, const char16_t charValue) {
                     dest.Append(GetCharacterCodeName(charValue));
                   });
  result.AppendLiteral("\"");
  return result;
}

class MOZ_STACK_CLASS GetShiftStateName final : public nsAutoCString {
 public:
  explicit GetShiftStateName(VirtualKey::ShiftState aShiftState) {
    if (!aShiftState) {
      AssignLiteral("none");
      return;
    }
    if (aShiftState & VirtualKey::STATE_SHIFT) {
      AssignLiteral("Shift");
      aShiftState &= ~VirtualKey::STATE_SHIFT;
    }
    if (aShiftState & VirtualKey::STATE_CONTROL) {
      MaybeAppendSeparator();
      AssignLiteral("Ctrl");
      aShiftState &= ~VirtualKey::STATE_CONTROL;
    }
    if (aShiftState & VirtualKey::STATE_ALT) {
      MaybeAppendSeparator();
      AssignLiteral("Alt");
      aShiftState &= ~VirtualKey::STATE_ALT;
    }
    if (aShiftState & VirtualKey::STATE_CAPSLOCK) {
      MaybeAppendSeparator();
      AssignLiteral("CapsLock");
      aShiftState &= ~VirtualKey::STATE_CAPSLOCK;
    }
    MOZ_ASSERT(!aShiftState);
  }

 private:
  void MaybeAppendSeparator() {
    if (!IsEmpty()) {
      AppendLiteral(" | ");
    }
  }
};

static const nsCString GetMessageName(UINT aMessage) {
  switch (aMessage) {
    case WM_NULL:
      return "WM_NULL"_ns;
    case WM_KEYDOWN:
      return "WM_KEYDOWN"_ns;
    case WM_KEYUP:
      return "WM_KEYUP"_ns;
    case WM_SYSKEYDOWN:
      return "WM_SYSKEYDOWN"_ns;
    case WM_SYSKEYUP:
      return "WM_SYSKEYUP"_ns;
    case WM_CHAR:
      return "WM_CHAR"_ns;
    case WM_UNICHAR:
      return "WM_UNICHAR"_ns;
    case WM_SYSCHAR:
      return "WM_SYSCHAR"_ns;
    case WM_DEADCHAR:
      return "WM_DEADCHAR"_ns;
    case WM_SYSDEADCHAR:
      return "WM_SYSDEADCHAR"_ns;
    case WM_APPCOMMAND:
      return "WM_APPCOMMAND"_ns;
    case WM_QUIT:
      return "WM_QUIT"_ns;
    default:
      return nsPrintfCString("Unknown Message (0x%04X)", aMessage);
  }
}

static const nsCString GetVirtualKeyCodeName(WPARAM aVK) {
  if (aVK >= std::size(kVirtualKeyName)) {
    return nsPrintfCString("Invalid (0x%08zX)", aVK);
  }
  return nsCString(kVirtualKeyName[aVK]);
}

static const nsCString GetAppCommandName(WPARAM aCommand) {
  switch (aCommand) {
    case APPCOMMAND_BASS_BOOST:
      return "APPCOMMAND_BASS_BOOST"_ns;
    case APPCOMMAND_BASS_DOWN:
      return "APPCOMMAND_BASS_DOWN"_ns;
    case APPCOMMAND_BASS_UP:
      return "APPCOMMAND_BASS_UP"_ns;
    case APPCOMMAND_BROWSER_BACKWARD:
      return "APPCOMMAND_BROWSER_BACKWARD"_ns;
    case APPCOMMAND_BROWSER_FAVORITES:
      return "APPCOMMAND_BROWSER_FAVORITES"_ns;
    case APPCOMMAND_BROWSER_FORWARD:
      return "APPCOMMAND_BROWSER_FORWARD"_ns;
    case APPCOMMAND_BROWSER_HOME:
      return "APPCOMMAND_BROWSER_HOME"_ns;
    case APPCOMMAND_BROWSER_REFRESH:
      return "APPCOMMAND_BROWSER_REFRESH"_ns;
    case APPCOMMAND_BROWSER_SEARCH:
      return "APPCOMMAND_BROWSER_SEARCH"_ns;
    case APPCOMMAND_BROWSER_STOP:
      return "APPCOMMAND_BROWSER_STOP"_ns;
    case APPCOMMAND_CLOSE:
      return "APPCOMMAND_CLOSE"_ns;
    case APPCOMMAND_COPY:
      return "APPCOMMAND_COPY"_ns;
    case APPCOMMAND_CORRECTION_LIST:
      return "APPCOMMAND_CORRECTION_LIST"_ns;
    case APPCOMMAND_CUT:
      return "APPCOMMAND_CUT"_ns;
    case APPCOMMAND_DICTATE_OR_COMMAND_CONTROL_TOGGLE:
      return "APPCOMMAND_DICTATE_OR_COMMAND_CONTROL_TOGGLE"_ns;
    case APPCOMMAND_FIND:
      return "APPCOMMAND_FIND"_ns;
    case APPCOMMAND_FORWARD_MAIL:
      return "APPCOMMAND_FORWARD_MAIL"_ns;
    case APPCOMMAND_HELP:
      return "APPCOMMAND_HELP"_ns;
    case APPCOMMAND_LAUNCH_APP1:
      return "APPCOMMAND_LAUNCH_APP1"_ns;
    case APPCOMMAND_LAUNCH_APP2:
      return "APPCOMMAND_LAUNCH_APP2"_ns;
    case APPCOMMAND_LAUNCH_MAIL:
      return "APPCOMMAND_LAUNCH_MAIL"_ns;
    case APPCOMMAND_LAUNCH_MEDIA_SELECT:
      return "APPCOMMAND_LAUNCH_MEDIA_SELECT"_ns;
    case APPCOMMAND_MEDIA_CHANNEL_DOWN:
      return "APPCOMMAND_MEDIA_CHANNEL_DOWN"_ns;
    case APPCOMMAND_MEDIA_CHANNEL_UP:
      return "APPCOMMAND_MEDIA_CHANNEL_UP"_ns;
    case APPCOMMAND_MEDIA_FAST_FORWARD:
      return "APPCOMMAND_MEDIA_FAST_FORWARD"_ns;
    case APPCOMMAND_MEDIA_NEXTTRACK:
      return "APPCOMMAND_MEDIA_NEXTTRACK"_ns;
    case APPCOMMAND_MEDIA_PAUSE:
      return "APPCOMMAND_MEDIA_PAUSE"_ns;
    case APPCOMMAND_MEDIA_PLAY:
      return "APPCOMMAND_MEDIA_PLAY"_ns;
    case APPCOMMAND_MEDIA_PLAY_PAUSE:
      return "APPCOMMAND_MEDIA_PLAY_PAUSE"_ns;
    case APPCOMMAND_MEDIA_PREVIOUSTRACK:
      return "APPCOMMAND_MEDIA_PREVIOUSTRACK"_ns;
    case APPCOMMAND_MEDIA_RECORD:
      return "APPCOMMAND_MEDIA_RECORD"_ns;
    case APPCOMMAND_MEDIA_REWIND:
      return "APPCOMMAND_MEDIA_REWIND"_ns;
    case APPCOMMAND_MEDIA_STOP:
      return "APPCOMMAND_MEDIA_STOP"_ns;
    case APPCOMMAND_MIC_ON_OFF_TOGGLE:
      return "APPCOMMAND_MIC_ON_OFF_TOGGLE"_ns;
    case APPCOMMAND_MICROPHONE_VOLUME_DOWN:
      return "APPCOMMAND_MICROPHONE_VOLUME_DOWN"_ns;
    case APPCOMMAND_MICROPHONE_VOLUME_MUTE:
      return "APPCOMMAND_MICROPHONE_VOLUME_MUTE"_ns;
    case APPCOMMAND_MICROPHONE_VOLUME_UP:
      return "APPCOMMAND_MICROPHONE_VOLUME_UP"_ns;
    case APPCOMMAND_NEW:
      return "APPCOMMAND_NEW"_ns;
    case APPCOMMAND_OPEN:
      return "APPCOMMAND_OPEN"_ns;
    case APPCOMMAND_PASTE:
      return "APPCOMMAND_PASTE"_ns;
    case APPCOMMAND_PRINT:
      return "APPCOMMAND_PRINT"_ns;
    case APPCOMMAND_REDO:
      return "APPCOMMAND_REDO"_ns;
    case APPCOMMAND_REPLY_TO_MAIL:
      return "APPCOMMAND_REPLY_TO_MAIL"_ns;
    case APPCOMMAND_SAVE:
      return "APPCOMMAND_SAVE"_ns;
    case APPCOMMAND_SEND_MAIL:
      return "APPCOMMAND_SEND_MAIL"_ns;
    case APPCOMMAND_SPELL_CHECK:
      return "APPCOMMAND_SPELL_CHECK"_ns;
    case APPCOMMAND_TREBLE_DOWN:
      return "APPCOMMAND_TREBLE_DOWN"_ns;
    case APPCOMMAND_TREBLE_UP:
      return "APPCOMMAND_TREBLE_UP"_ns;
    case APPCOMMAND_UNDO:
      return "APPCOMMAND_UNDO"_ns;
    case APPCOMMAND_VOLUME_DOWN:
      return "APPCOMMAND_VOLUME_DOWN"_ns;
    case APPCOMMAND_VOLUME_MUTE:
      return "APPCOMMAND_VOLUME_MUTE"_ns;
    case APPCOMMAND_VOLUME_UP:
      return "APPCOMMAND_VOLUME_UP"_ns;
    default:
      return nsPrintfCString("Unknown app command (0x%08zX)", aCommand);
  }
}

static const nsCString GetAppCommandDeviceName(LPARAM aDevice) {
  switch (aDevice) {
    case FAPPCOMMAND_KEY:
      return "FAPPCOMMAND_KEY"_ns;
    case FAPPCOMMAND_MOUSE:
      return "FAPPCOMMAND_MOUSE"_ns;
    case FAPPCOMMAND_OEM:
      return "FAPPCOMMAND_OEM"_ns;
    default:
      return nsPrintfCString("Unknown app command device (0x%04" PRIXLPTR ")",
                             aDevice);
  }
};

class MOZ_STACK_CLASS GetAppCommandKeysName final : public nsAutoCString {
 public:
  explicit GetAppCommandKeysName(WPARAM aKeys) {
    if (aKeys & MK_CONTROL) {
      AppendLiteral("MK_CONTROL");
      aKeys &= ~MK_CONTROL;
    }
    if (aKeys & MK_LBUTTON) {
      MaybeAppendSeparator();
      AppendLiteral("MK_LBUTTON");
      aKeys &= ~MK_LBUTTON;
    }
    if (aKeys & MK_MBUTTON) {
      MaybeAppendSeparator();
      AppendLiteral("MK_MBUTTON");
      aKeys &= ~MK_MBUTTON;
    }
    if (aKeys & MK_RBUTTON) {
      MaybeAppendSeparator();
      AppendLiteral("MK_RBUTTON");
      aKeys &= ~MK_RBUTTON;
    }
    if (aKeys & MK_SHIFT) {
      MaybeAppendSeparator();
      AppendLiteral("MK_SHIFT");
      aKeys &= ~MK_SHIFT;
    }
    if (aKeys & MK_XBUTTON1) {
      MaybeAppendSeparator();
      AppendLiteral("MK_XBUTTON1");
      aKeys &= ~MK_XBUTTON1;
    }
    if (aKeys & MK_XBUTTON2) {
      MaybeAppendSeparator();
      AppendLiteral("MK_XBUTTON2");
      aKeys &= ~MK_XBUTTON2;
    }
    if (aKeys) {
      MaybeAppendSeparator();
      AppendPrintf("Unknown Flags (0x%04zX)", aKeys);
    }
    if (IsEmpty()) {
      AssignLiteral("none (0x0000)");
    }
  }

 private:
  void MaybeAppendSeparator() {
    if (!IsEmpty()) {
      AppendLiteral(" | ");
    }
  }
};

static const nsCString ToString(const MSG& aMSG) {
  nsCString result;
  result.AssignLiteral("{ message=");
  result.Append(GetMessageName(aMSG.message).get());
  result.AppendLiteral(", ");
  switch (aMSG.message) {
    case WM_KEYDOWN:
    case WM_KEYUP:
    case WM_SYSKEYDOWN:
    case WM_SYSKEYUP:
      result.AppendPrintf(
          "virtual keycode=%s, repeat count=%" PRIdLPTR
          ", "
          "scancode=0x%02X, extended key=%s, "
          "context code=%s, previous key state=%s, "
          "transition state=%s",
          GetVirtualKeyCodeName(aMSG.wParam).get(), aMSG.lParam & 0xFFFF,
          WinUtils::GetScanCode(aMSG.lParam),
          TrueOrFalse(WinUtils::IsExtendedScanCode(aMSG.lParam)),
          TrueOrFalse((aMSG.lParam & (1 << 29)) != 0),
          TrueOrFalse((aMSG.lParam & (1 << 30)) != 0),
          TrueOrFalse((aMSG.lParam & (1 << 31)) != 0));
      break;
    case WM_CHAR:
    case WM_DEADCHAR:
    case WM_SYSCHAR:
    case WM_SYSDEADCHAR:
      result.AppendPrintf(
          "character code=%s, repeat count=%" PRIdLPTR
          ", "
          "scancode=0x%02X, extended key=%s, "
          "context code=%s, previous key state=%s, "
          "transition state=%s",
          GetCharacterCodeName(aMSG.wParam).get(), aMSG.lParam & 0xFFFF,
          WinUtils::GetScanCode(aMSG.lParam),
          TrueOrFalse(WinUtils::IsExtendedScanCode(aMSG.lParam)),
          TrueOrFalse((aMSG.lParam & (1 << 29)) != 0),
          TrueOrFalse((aMSG.lParam & (1 << 30)) != 0),
          TrueOrFalse((aMSG.lParam & (1 << 31)) != 0));
      break;
    case WM_APPCOMMAND:
      result.AppendPrintf(
          "window handle=0x%zx, app command=%s, device=%s, dwKeys=%s",
          aMSG.wParam,
          GetAppCommandName(GET_APPCOMMAND_LPARAM(aMSG.lParam)).get(),
          GetAppCommandDeviceName(GET_DEVICE_LPARAM(aMSG.lParam)).get(),
          GetAppCommandKeysName(GET_KEYSTATE_LPARAM(aMSG.lParam)).get());
      break;
    default:
      result.AppendPrintf("wParam=%zu, lParam=%" PRIdLPTR, aMSG.wParam,
                          aMSG.lParam);
      break;
  }
  result.AppendPrintf(", hwnd=0x%p", aMSG.hwnd);
  return result;
}

static const nsCString ToString(
    const UniCharsAndModifiers& aUniCharsAndModifiers) {
  if (aUniCharsAndModifiers.IsEmpty()) {
    return "{}"_ns;
  }
  nsCString result;
  result.AssignLiteral("{ ");
  result.Append(GetCharacterCodeName(aUniCharsAndModifiers.CharAt(0)));
  for (size_t i = 1; i < aUniCharsAndModifiers.Length(); ++i) {
    if (aUniCharsAndModifiers.ModifiersAt(i - 1) !=
        aUniCharsAndModifiers.ModifiersAt(i)) {
      result.AppendLiteral(" [");
      result.Append(GetModifiersName(aUniCharsAndModifiers.ModifiersAt(0)));
      result.AppendLiteral("]");
    }
    result.AppendLiteral(", ");
    result.Append(GetCharacterCodeName(aUniCharsAndModifiers.CharAt(i)));
  }
  result.AppendLiteral(" [");
  uint32_t lastIndex = aUniCharsAndModifiers.Length() - 1;
  result.Append(GetModifiersName(aUniCharsAndModifiers.ModifiersAt(lastIndex)));
  result.AppendLiteral("] }");
  return result;
}

const nsCString ToString(const ModifierKeyState& aModifierKeyState) {
  nsCString result;
  result.AssignLiteral("{ ");
  result.Append(GetModifiersName(aModifierKeyState.GetModifiers()).get());
  result.AppendLiteral(" }");
  return result;
}

// Unique id counter associated with a keydown / keypress events. Used in
// identifing keypress events for removal from async event dispatch queue
// in metrofx after preventDefault is called on keydown events.
static uint32_t sUniqueKeyEventId = 0;

/*****************************************************************************
 * mozilla::widget::ModifierKeyState
 *****************************************************************************/

ModifierKeyState::ModifierKeyState() { Update(); }

ModifierKeyState::ModifierKeyState(Modifiers aModifiers)
    : mModifiers(aModifiers) {
  MOZ_ASSERT(!(mModifiers & MODIFIER_ALTGRAPH) || (!IsControl() && !IsAlt()),
             "Neither MODIFIER_CONTROL nor MODIFIER_ALT should be set "
             "if MODIFIER_ALTGRAPH is set");
}

void ModifierKeyState::Update() {
  mModifiers = 0;
  if (IS_VK_DOWN(VK_SHIFT)) {
    mModifiers |= MODIFIER_SHIFT;
  }
  // If AltGr key (i.e., VK_RMENU on some keyboard layout) is pressed, only
  // MODIFIER_ALTGRAPH should be set.  Otherwise, i.e., if both Ctrl and Alt
  // keys are pressed to emulate AltGr key, MODIFIER_CONTROL and MODIFIER_ALT
  // keys should be set separately.
  if (IS_VK_DOWN(VK_RMENU) && KeyboardLayout::GetInstance()->HasAltGr()) {
    mModifiers |= MODIFIER_ALTGRAPH;
  } else {
    if (IS_VK_DOWN(VK_CONTROL)) {
      mModifiers |= MODIFIER_CONTROL;
    }
    if (IS_VK_DOWN(VK_MENU)) {
      mModifiers |= MODIFIER_ALT;
    }
  }
  if (IS_VK_DOWN(VK_LWIN) || IS_VK_DOWN(VK_RWIN)) {
    mModifiers |= MODIFIER_META;
  }
  if (::GetKeyState(VK_CAPITAL) & 1) {
    mModifiers |= MODIFIER_CAPSLOCK;
  }
  if (::GetKeyState(VK_NUMLOCK) & 1) {
    mModifiers |= MODIFIER_NUMLOCK;
  }
  if (::GetKeyState(VK_SCROLL) & 1) {
    mModifiers |= MODIFIER_SCROLLLOCK;
  }
}

void ModifierKeyState::Unset(Modifiers aRemovingModifiers) {
  mModifiers &= ~aRemovingModifiers;
}

void ModifierKeyState::Set(Modifiers aAddingModifiers) {
  mModifiers |= aAddingModifiers;
  MOZ_ASSERT(!(mModifiers & MODIFIER_ALTGRAPH) || (!IsControl() && !IsAlt()),
             "Neither MODIFIER_CONTROL nor MODIFIER_ALT should be set "
             "if MODIFIER_ALTGRAPH is set");
}

void ModifierKeyState::InitInputEvent(WidgetInputEvent& aInputEvent) const {
  aInputEvent.mModifiers = mModifiers;

  switch (aInputEvent.mClass) {
    case eMouseEventClass:
    case ePointerEventClass:
    case eMouseScrollEventClass:
    case eWheelEventClass:
    case eDragEventClass:
    case eSimpleGestureEventClass:
      InitMouseEvent(aInputEvent);
      break;
    default:
      break;
  }
}

void ModifierKeyState::InitMouseEvent(WidgetInputEvent& aMouseEvent) const {
  NS_ASSERTION(aMouseEvent.mClass == eMouseEventClass ||
                   aMouseEvent.mClass == ePointerEventClass ||
                   aMouseEvent.mClass == eWheelEventClass ||
                   aMouseEvent.mClass == eDragEventClass ||
                   aMouseEvent.mClass == eSimpleGestureEventClass,
               "called with non-mouse event");

  WidgetMouseEventBase& mouseEvent = *aMouseEvent.AsMouseEventBase();
  mouseEvent.mButtons = 0;

  // Try to get button state from InputFilter (MouseMux per-window state)
  uint16_t mouseMuxButtons = 0;
  if (InputFilter::GetCurrentMouseButtons(&mouseMuxButtons)) {
    // Use MouseMux tracked button state instead of native GetKeyState
    mouseEvent.mButtons = static_cast<int16_t>(mouseMuxButtons);
    return;
  }

  // Fall back to native GetKeyState for non-MouseMux windows
  if (::GetKeyState(VK_LBUTTON) < 0) {
    mouseEvent.mButtons |= MouseButtonsFlag::ePrimaryFlag;
  }
  if (::GetKeyState(VK_RBUTTON) < 0) {
    mouseEvent.mButtons |= MouseButtonsFlag::eSecondaryFlag;
  }
  if (::GetKeyState(VK_MBUTTON) < 0) {
    mouseEvent.mButtons |= MouseButtonsFlag::eMiddleFlag;
  }
  if (::GetKeyState(VK_XBUTTON1) < 0) {
    mouseEvent.mButtons |= MouseButtonsFlag::e4thFlag;
  }
  if (::GetKeyState(VK_XBUTTON2) < 0) {
    mouseEvent.mButtons |= MouseButtonsFlag::e5thFlag;
  }
}

bool ModifierKeyState::IsShift() const {
  return (mModifiers & MODIFIER_SHIFT) != 0;
}

bool ModifierKeyState::IsControl() const {
  return (mModifiers & MODIFIER_CONTROL) != 0;
}

bool ModifierKeyState::IsAlt() const {
  return (mModifiers & MODIFIER_ALT) != 0;
}

bool ModifierKeyState::IsWin() const {
  return (mModifiers & MODIFIER_META) != 0;
}

bool ModifierKeyState::MaybeMatchShortcutKey() const {
  // If Windows key is pressed, even if both Ctrl key and Alt key are pressed,
  // it's possible to match a shortcut key.
  if (IsWin()) {
    return true;
  }
  // Otherwise, when both Ctrl key and Alt key are pressed, it shouldn't be
  // a shortcut key for Windows since it means pressing AltGr key on
  // some keyboard layouts.
  if (IsControl() ^ IsAlt()) {
    return true;
  }
  // If no modifier key is active except a lockable modifier nor Shift key,
  // the key shouldn't match any shortcut keys (there are Space and
  // Shift+Space, though, let's ignore these special case...).
  return false;
}

bool ModifierKeyState::IsCapsLocked() const {
  return (mModifiers & MODIFIER_CAPSLOCK) != 0;
}

bool ModifierKeyState::IsNumLocked() const {
  return (mModifiers & MODIFIER_NUMLOCK) != 0;
}

bool ModifierKeyState::IsScrollLocked() const {
  return (mModifiers & MODIFIER_SCROLLLOCK) != 0;
}

/*****************************************************************************
 * mozilla::widget::UniCharsAndModifiers
 *****************************************************************************/

void UniCharsAndModifiers::Append(char16_t aUniChar, Modifiers aModifiers) {
  mChars.Append(aUniChar);
  mModifiers.AppendElement(aModifiers);
}

void UniCharsAndModifiers::FillModifiers(Modifiers aModifiers) {
  for (size_t i = 0; i < Length(); i++) {
    mModifiers[i] = aModifiers;
  }
}

void UniCharsAndModifiers::OverwriteModifiersIfBeginsWith(
    const UniCharsAndModifiers& aOther) {
  if (!BeginsWith(aOther)) {
    return;
  }
  for (size_t i = 0; i < aOther.Length(); ++i) {
    mModifiers[i] = aOther.mModifiers[i];
  }
}

bool UniCharsAndModifiers::UniCharsEqual(
    const UniCharsAndModifiers& aOther) const {
  return mChars.Equals(aOther.mChars);
}

bool UniCharsAndModifiers::UniCharsCaseInsensitiveEqual(
    const UniCharsAndModifiers& aOther) const {
  return mChars.Equals(aOther.mChars, nsCaseInsensitiveStringComparator);
}

bool UniCharsAndModifiers::BeginsWith(
    const UniCharsAndModifiers& aOther) const {
  return StringBeginsWith(mChars, aOther.mChars);
}

UniCharsAndModifiers& UniCharsAndModifiers::operator+=(
    const UniCharsAndModifiers& aOther) {
  mChars.Append(aOther.mChars);
  mModifiers.AppendElements(aOther.mModifiers);
  return *this;
}

UniCharsAndModifiers UniCharsAndModifiers::operator+(
    const UniCharsAndModifiers& aOther) const {
  UniCharsAndModifiers result(*this);
  result += aOther;
  return result;
}

/*****************************************************************************
 * mozilla::widget::VirtualKey
 *****************************************************************************/

// static
VirtualKey::ShiftState VirtualKey::ModifiersToShiftState(Modifiers aModifiers) {
  ShiftState state = 0;
  if (aModifiers & MODIFIER_SHIFT) {
    state |= STATE_SHIFT;
  }
  if (aModifiers & MODIFIER_ALTGRAPH) {
    state |= STATE_ALTGRAPH;
  } else {
    if (aModifiers & MODIFIER_CONTROL) {
      state |= STATE_CONTROL;
    }
    if (aModifiers & MODIFIER_ALT) {
      state |= STATE_ALT;
    }
  }
  if (aModifiers & MODIFIER_CAPSLOCK) {
    state |= STATE_CAPSLOCK;
  }
  return state;
}

// static
Modifiers VirtualKey::ShiftStateToModifiers(ShiftState aShiftState) {
  Modifiers modifiers = 0;
  if (aShiftState & STATE_SHIFT) {
    modifiers |= MODIFIER_SHIFT;
  }
  if (aShiftState & STATE_ALTGRAPH) {
    modifiers |= MODIFIER_ALTGRAPH;
  } else {
    if (aShiftState & STATE_CONTROL) {
      modifiers |= MODIFIER_CONTROL;
    }
    if (aShiftState & STATE_ALT) {
      modifiers |= MODIFIER_ALT;
    }
  }
  if (aShiftState & STATE_CAPSLOCK) {
    modifiers |= MODIFIER_CAPSLOCK;
  }
  return modifiers;
}

const DeadKeyTable* VirtualKey::MatchingDeadKeyTable(
    const DeadKeyTable& aDeadKeyTable) const {
  if (!mIsDeadKey) {
    return nullptr;
  }

  for (ShiftState shiftState = 0; shiftState < 16; shiftState++) {
    if (!IsDeadKey(shiftState)) {
      continue;
    }
    const DeadKeyTable* dkt = mShiftStates[shiftState].DeadKey.Table;
    if (dkt && *dkt == aDeadKeyTable) {
      return dkt;
    }
  }

  return nullptr;
}

void VirtualKey::SetNormalChars(ShiftState aShiftState, const char16_t* aChars,
                                uint32_t aNumOfChars) {
  MOZ_ASSERT(aShiftState == ToShiftStateIndex(aShiftState));

  SetDeadKey(aShiftState, false);

  for (uint32_t index = 0; index < aNumOfChars; index++) {
    // Ignore legacy non-printable control characters
    mShiftStates[aShiftState].Normal.Chars[index] =
        (aChars[index] >= 0x20) ? aChars[index] : 0;
  }

  uint32_t len = std::size(mShiftStates[aShiftState].Normal.Chars);
  for (uint32_t index = aNumOfChars; index < len; index++) {
    mShiftStates[aShiftState].Normal.Chars[index] = 0;
  }
}

void VirtualKey::SetDeadChar(ShiftState aShiftState, char16_t aDeadChar) {
  MOZ_ASSERT(aShiftState == ToShiftStateIndex(aShiftState));

  SetDeadKey(aShiftState, true);

  mShiftStates[aShiftState].DeadKey.DeadChar = aDeadChar;
  mShiftStates[aShiftState].DeadKey.Table = nullptr;
}

UniCharsAndModifiers VirtualKey::GetUniChars(ShiftState aShiftState) const {
  UniCharsAndModifiers result = GetNativeUniChars(aShiftState);

  const uint8_t kShiftStateIndex = ToShiftStateIndex(aShiftState);
  if (!(kShiftStateIndex & STATE_CONTROL_ALT)) {
    // If neither Alt nor Ctrl key is pressed, just return stored data
    // for the key.
    return result;
  }

  if (result.IsEmpty()) {
    // If Alt and/or Control are pressed and the key produces no
    // character, return characters which is produced by the key without
    // Alt and Control, and return given modifiers as is.
    result = GetNativeUniChars(kShiftStateIndex & ~STATE_CONTROL_ALT);
    result.FillModifiers(ShiftStateToModifiers(aShiftState));
    return result;
  }

  if (IsAltGrIndex(kShiftStateIndex)) {
    // If AltGr or both Ctrl and Alt are pressed and the key produces
    // character(s), we need to clear MODIFIER_ALT and MODIFIER_CONTROL
    // since TextEditor won't handle eKeyPress event whose mModifiers
    // has MODIFIER_ALT or MODIFIER_CONTROL.  Additionally, we need to
    // use MODIFIER_ALTGRAPH when a key produces character(s) with
    // AltGr or both Ctrl and Alt on Windows.  See following spec issue:
    // <https://github.com/w3c/uievents/issues/147>
    Modifiers finalModifiers = ShiftStateToModifiers(aShiftState);
    finalModifiers &= ~(MODIFIER_ALT | MODIFIER_CONTROL);
    finalModifiers |= MODIFIER_ALTGRAPH;
    result.FillModifiers(finalModifiers);
    return result;
  }

  // Otherwise, i.e., Alt or Ctrl is pressed and it produces character(s),
  // check if different character(s) is produced by the key without Alt/Ctrl.
  // If it produces different character, we need to consume the Alt and
  // Ctrl modifier for TextEditor.  Otherwise, the key does not produces the
  // character actually.  So, keep setting Alt and Ctrl modifiers.
  UniCharsAndModifiers unmodifiedReslt =
      GetNativeUniChars(kShiftStateIndex & ~STATE_CONTROL_ALT);
  if (!result.UniCharsEqual(unmodifiedReslt)) {
    Modifiers finalModifiers = ShiftStateToModifiers(aShiftState);
    finalModifiers &= ~(MODIFIER_ALT | MODIFIER_CONTROL);
    result.FillModifiers(finalModifiers);
  }
  return result;
}

UniCharsAndModifiers VirtualKey::GetNativeUniChars(
    ShiftState aShiftState) const {
  const uint8_t kShiftStateIndex = ToShiftStateIndex(aShiftState);
  UniCharsAndModifiers result;
  Modifiers modifiers = ShiftStateToModifiers(aShiftState);
  if (IsDeadKey(aShiftState)) {
    result.Append(mShiftStates[kShiftStateIndex].DeadKey.DeadChar, modifiers);
    return result;
  }

  uint32_t len = std::size(mShiftStates[kShiftStateIndex].Normal.Chars);
  for (uint32_t i = 0;
       i < len && mShiftStates[kShiftStateIndex].Normal.Chars[i]; i++) {
    result.Append(mShiftStates[kShiftStateIndex].Normal.Chars[i], modifiers);
  }
  return result;
}

// static
void VirtualKey::FillKbdState(PBYTE aKbdState, const ShiftState aShiftState) {
  if (aShiftState & STATE_SHIFT) {
    aKbdState[VK_SHIFT] |= 0x80;
  } else {
    aKbdState[VK_SHIFT] &= ~0x80;
    aKbdState[VK_LSHIFT] &= ~0x80;
    aKbdState[VK_RSHIFT] &= ~0x80;
  }

  if (aShiftState & STATE_ALTGRAPH) {
    aKbdState[VK_CONTROL] |= 0x80;
    aKbdState[VK_LCONTROL] |= 0x80;
    aKbdState[VK_RCONTROL] &= ~0x80;
    aKbdState[VK_MENU] |= 0x80;
    aKbdState[VK_LMENU] &= ~0x80;
    aKbdState[VK_RMENU] |= 0x80;
  } else {
    if (aShiftState & STATE_CONTROL) {
      aKbdState[VK_CONTROL] |= 0x80;
    } else {
      aKbdState[VK_CONTROL] &= ~0x80;
      aKbdState[VK_LCONTROL] &= ~0x80;
      aKbdState[VK_RCONTROL] &= ~0x80;
    }

    if (aShiftState & STATE_ALT) {
      aKbdState[VK_MENU] |= 0x80;
    } else {
      aKbdState[VK_MENU] &= ~0x80;
      aKbdState[VK_LMENU] &= ~0x80;
      aKbdState[VK_RMENU] &= ~0x80;
    }
  }

  if (aShiftState & STATE_CAPSLOCK) {
    aKbdState[VK_CAPITAL] |= 0x01;
  } else {
    aKbdState[VK_CAPITAL] &= ~0x01;
  }
}

/*****************************************************************************
 * mozilla::widget::NativeKey
 *****************************************************************************/

uint8_t NativeKey::sDispatchedKeyOfAppCommand = 0;
NativeKey* NativeKey::sLatestInstance = nullptr;
const MSG NativeKey::sEmptyMSG = {};
MSG NativeKey::sLastKeyOrCharMSG = {};
MSG NativeKey::sLastKeyMSG = {};
char16_t NativeKey::sPendingHighSurrogate = 0;

NativeKey::NativeKey(nsWindow* aWidget, const MSG& aMessage,
                     const ModifierKeyState& aModKeyState,
                     HKL aOverrideKeyboardLayout,
                     nsTArray<FakeCharMsg>* aFakeCharMsgs)
    : mLastInstance(sLatestInstance),
      mRemovingMsg(sEmptyMSG),
      mReceivedMsg(sEmptyMSG),
      mWidget(aWidget),
      mDispatcher(aWidget->GetTextEventDispatcher()),
      mMsg(aMessage),
      mFocusedWndBeforeDispatch(::GetFocus()),
      mDOMKeyCode(0),
      mKeyNameIndex(KEY_NAME_INDEX_Unidentified),
      mCodeNameIndex(CODE_NAME_INDEX_UNKNOWN),
      mModKeyState(aModKeyState),
      mVirtualKeyCode(0),
      mOriginalVirtualKeyCode(0),
      mShiftedLatinChar(0),
      mUnshiftedLatinChar(0),
      mScanCode(0),
      mIsExtended(false),
      mIsRepeat(false),
      mIsDeadKey(false),
      mIsPrintableKey(false),
      mIsSkippableInRemoteProcess(false),
      mCharMessageHasGone(false),
      mCanIgnoreModifierStateAtKeyPress(true),
      mFakeCharMsgs(aFakeCharMsgs && aFakeCharMsgs->Length() ? aFakeCharMsgs
                                                             : nullptr) {
  MOZ_LOG(gKeyLog, LogLevel::Info,
          ("%p NativeKey::NativeKey(aWidget=0x%p { GetWindowHandle()=0x%p }, "
           "aMessage=%s, aModKeyState=%s), sLatestInstance=0x%p",
           this, aWidget, aWidget->GetWindowHandle(), ToString(aMessage).get(),
           ToString(aModKeyState).get(), sLatestInstance));

  MOZ_ASSERT(aWidget);
  MOZ_ASSERT(mDispatcher);
  sLatestInstance = this;
  KeyboardLayout* keyboardLayout = KeyboardLayout::GetInstance();
  mKeyboardLayout = KeyboardLayout::GetLayout();
  if (aOverrideKeyboardLayout && mKeyboardLayout != aOverrideKeyboardLayout) {
    keyboardLayout->OverrideLayout(aOverrideKeyboardLayout);
    mKeyboardLayout = keyboardLayout->GetLoadedLayout();
    MOZ_ASSERT(mKeyboardLayout == aOverrideKeyboardLayout);
    mIsOverridingKeyboardLayout = true;
  } else {
    mIsOverridingKeyboardLayout = false;
    sLastKeyOrCharMSG = aMessage;
  }

  if (mMsg.message == WM_APPCOMMAND) {
    InitWithAppCommand();
  } else {
    InitWithKeyOrChar();
  }

  MOZ_LOG(gKeyLog, LogLevel::Info,
          ("%p   NativeKey::NativeKey(), mKeyboardLayout=0x%p, "
           "mFocusedWndBeforeDispatch=0x%p, mDOMKeyCode=%s, "
           "mKeyNameIndex=%s, mCodeNameIndex=%s, mModKeyState=%s, "
           "mVirtualKeyCode=%s, mOriginalVirtualKeyCode=%s, "
           "mCommittedCharsAndModifiers=%s, mInputtingStringAndModifiers=%s, "
           "mShiftedString=%s, mUnshiftedString=%s, mShiftedLatinChar=%s, "
           "mUnshiftedLatinChar=%s, mScanCode=0x%04X, mIsExtended=%s, "
           "mIsRepeat=%s, mIsDeadKey=%s, mIsPrintableKey=%s, "
           "mIsSkippableInRemoteProcess=%s, mCharMessageHasGone=%s, "
           "mIsOverridingKeyboardLayout=%s",
           this, mKeyboardLayout, mFocusedWndBeforeDispatch,
           GetDOMKeyCodeName(mDOMKeyCode).get(), ToString(mKeyNameIndex).get(),
           ToString(mCodeNameIndex).get(), ToString(mModKeyState).get(),
           GetVirtualKeyCodeName(mVirtualKeyCode).get(),
           GetVirtualKeyCodeName(mOriginalVirtualKeyCode).get(),
           ToString(mCommittedCharsAndModifiers).get(),
           ToString(mInputtingStringAndModifiers).get(),
           ToString(mShiftedString).get(), ToString(mUnshiftedString).get(),
           GetCharacterCodeName(mShiftedLatinChar).get(),
           GetCharacterCodeName(mUnshiftedLatinChar).get(), mScanCode,
           TrueOrFalse(mIsExtended), TrueOrFalse(mIsRepeat),
           TrueOrFalse(mIsDeadKey), TrueOrFalse(mIsPrintableKey),
           TrueOrFalse(mIsSkippableInRemoteProcess),
           TrueOrFalse(mCharMessageHasGone),
           TrueOrFalse(mIsOverridingKeyboardLayout)));
}

void NativeKey::InitIsSkippableForKeyOrChar(const MSG& aLastKeyMSG) {
  mIsSkippableInRemoteProcess = false;

  if (!mIsRepeat) {
    // If the message is not repeated key message, the event should be always
    // handled in remote process even if it's too old.
    return;
  }

  // Keyboard utilities may send us some generated messages and such messages
  // may be marked as "repeated", e.g., SendInput() calls with
  // KEYEVENTF_UNICODE but without KEYEVENTF_KEYUP.   However, key sequence
  // comes from such utilities may be really important.  For example, utilities
  // may send WM_KEYDOWN for VK_BACK to remove previous character and send
  // WM_KEYDOWN for VK_PACKET to insert a composite character.  Therefore, we
  // should check if current message and previous key message are caused by
  // same physical key.  If not, the message may be generated by such
  // utility.
  // XXX With this approach, if VK_BACK messages are generated with known
  //     scancode, we cannot distinguish whether coming VK_BACK message is
  //     actually repeated by the auto-repeat feature.  Currently, we need
  //     this hack only for "SinhalaTamil IME" and fortunately, it generates
  //     VK_BACK messages with odd scancode.  So, we don't need to handle
  //     VK_BACK specially at least for now.

  if (mCodeNameIndex == CODE_NAME_INDEX_UNKNOWN) {
    // If current event is not caused by physical key operation, it may be
    // caused by a keyboard utility.  If so, the event shouldn't be ignored by
    // BrowserChild since it want to insert the character, delete a character or
    // move caret.
    return;
  }

  if (mOriginalVirtualKeyCode == VK_PACKET) {
    // If the message is VK_PACKET, that means that a keyboard utility
    // tries to insert a character.
    return;
  }

  switch (mMsg.message) {
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
    case WM_CHAR:
    case WM_SYSCHAR:
    case WM_DEADCHAR:
    case WM_SYSDEADCHAR:
      // However, some keyboard layouts may send some keyboard messages with
      // activating the bit.  If we dispatch repeated keyboard events, they
      // may be ignored by BrowserChild due to performance reason.  So, we need
      // to check if actually a physical key is repeated by the auto-repeat
      // feature.
      switch (aLastKeyMSG.message) {
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
          if (aLastKeyMSG.wParam == VK_PACKET) {
            // If the last message was VK_PACKET, that means that a keyboard
            // utility tried to insert a character.  So, current message is
            // not repeated key event of the previous event.
            return;
          }
          // Let's check whether current message and previous message are
          // caused by same physical key.
          mIsSkippableInRemoteProcess =
              mScanCode == WinUtils::GetScanCode(aLastKeyMSG.lParam) &&
              mIsExtended == WinUtils::IsExtendedScanCode(aLastKeyMSG.lParam);
          return;
        default:
          // If previous message is not a keydown, this must not be generated
          // by the auto-repeat feature.
          return;
      }
    case WM_APPCOMMAND:
      MOZ_ASSERT_UNREACHABLE(
          "WM_APPCOMMAND should be handled in "
          "InitWithAppCommand()");
      return;
    default:
      // keyup message shouldn't be repeated by the auto-repeat feature.
      return;
  }
}

void NativeKey::InitWithKeyOrChar() {
  MSG lastKeyMSG = sLastKeyMSG;
  char16_t pendingHighSurrogate = sPendingHighSurrogate;
  mScanCode = WinUtils::GetScanCode(mMsg.lParam);
  mIsExtended = WinUtils::IsExtendedScanCode(mMsg.lParam);
  switch (mMsg.message) {
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
      sPendingHighSurrogate = 0;
      [[fallthrough]];
    case WM_KEYUP:
    case WM_SYSKEYUP: {
      // Modify sLastKeyMSG now since retrieving following char messages may
      // cause sending another key message if odd tool hooks GetMessage(),
      // PeekMessage().
      sLastKeyMSG = mMsg;

      // Note that we don't need to compute raw virtual keycode here even when
      // it's VK_PROCESS (i.e., already handled by IME) because we need to
      // export it as DOM_VK_PROCESS and KEY_NAME_INDEX_Process.
      mOriginalVirtualKeyCode = static_cast<uint8_t>(mMsg.wParam);

      // If the key message is sent from other application like a11y tools, the
      // scancode value might not be set proper value.  Then, probably the value
      // is 0.
      // NOTE: If the virtual keycode can be caused by both non-extended key
      //       and extended key, the API returns the non-extended key's
      //       scancode.  E.g., VK_LEFT causes "4" key on numpad.
      if (!mScanCode && mOriginalVirtualKeyCode != VK_PACKET) {
        uint16_t scanCodeEx = ComputeScanCodeExFromVirtualKeyCode(mMsg.wParam);
        if (scanCodeEx) {
          mScanCode = static_cast<uint8_t>(scanCodeEx & 0xFF);
          uint8_t extended = static_cast<uint8_t>((scanCodeEx & 0xFF00) >> 8);
          mIsExtended = (extended == 0xE0) || (extended == 0xE1);
        }
      }

      // Most keys are not distinguished as left or right keys.
      bool isLeftRightDistinguishedKey = false;

      // mOriginalVirtualKeyCode must not distinguish left or right of
      // Shift, Control or Alt.
      switch (mOriginalVirtualKeyCode) {
        case VK_SHIFT:
        case VK_CONTROL:
        case VK_MENU:
          isLeftRightDistinguishedKey = true;
          break;
        case VK_LSHIFT:
        case VK_RSHIFT:
          mVirtualKeyCode = mOriginalVirtualKeyCode;
          mOriginalVirtualKeyCode = VK_SHIFT;
          isLeftRightDistinguishedKey = true;
          break;
        case VK_LCONTROL:
        case VK_RCONTROL:
          mVirtualKeyCode = mOriginalVirtualKeyCode;
          mOriginalVirtualKeyCode = VK_CONTROL;
          isLeftRightDistinguishedKey = true;
          break;
        case VK_LMENU:
        case VK_RMENU:
          mVirtualKeyCode = mOriginalVirtualKeyCode;
          mOriginalVirtualKeyCode = VK_MENU;
          isLeftRightDistinguishedKey = true;
          break;
      }

      // If virtual keycode (left-right distinguished keycode) is already
      // computed, we don't need to do anymore.
      if (mVirtualKeyCode) {
        break;
      }

      // If the keycode doesn't have LR distinguished keycode, we just set
      // mOriginalVirtualKeyCode to mVirtualKeyCode.  Note that don't compute
      // it from MapVirtualKeyEx() because the scan code might be wrong if
      // the message is sent/posted by other application.  Then, we will compute
      // unexpected keycode from the scan code.
      if (!isLeftRightDistinguishedKey) {
        break;
      }

      NS_ASSERTION(!mVirtualKeyCode,
                   "mVirtualKeyCode has been computed already");

      // Otherwise, compute the virtual keycode with MapVirtualKeyEx().
      mVirtualKeyCode = ComputeVirtualKeyCodeFromScanCodeEx();

      // Following code shouldn't be used now because we compute scancode value
      // if we detect that the sender doesn't set proper scancode.
      // However, the detection might fail. Therefore, let's keep using this.
      switch (mOriginalVirtualKeyCode) {
        case VK_CONTROL:
          if (mVirtualKeyCode != VK_LCONTROL &&
              mVirtualKeyCode != VK_RCONTROL) {
            mVirtualKeyCode = mIsExtended ? VK_RCONTROL : VK_LCONTROL;
          }
          break;
        case VK_MENU:
          if (mVirtualKeyCode != VK_LMENU && mVirtualKeyCode != VK_RMENU) {
            mVirtualKeyCode = mIsExtended ? VK_RMENU : VK_LMENU;
          }
          break;
        case VK_SHIFT:
          if (mVirtualKeyCode != VK_LSHIFT && mVirtualKeyCode != VK_RSHIFT) {
            // Neither left shift nor right shift is an extended key,
            // let's use VK_LSHIFT for unknown mapping.
            mVirtualKeyCode = VK_LSHIFT;
          }
          break;
        default:
          MOZ_CRASH("Unsupported mOriginalVirtualKeyCode");
      }
      break;
    }
    case WM_CHAR:
    case WM_UNICHAR:
    case WM_SYSCHAR:
      sPendingHighSurrogate = 0;
      // If there is another instance and it is trying to remove a char message
      // from the queue, this message should be handled in the old instance.
      if (IsAnotherInstanceRemovingCharMessage()) {
        // XXX Do we need to make mReceivedMsg an array?
        MOZ_ASSERT(IsEmptyMSG(mLastInstance->mReceivedMsg));
        MOZ_LOG(
            gKeyLog, LogLevel::Warning,
            ("%p   NativeKey::InitWithKeyOrChar(), WARNING, detecting another "
             "instance is trying to remove a char message, so, this instance "
             "should do nothing, mLastInstance=0x%p, mRemovingMsg=%s, "
             "mReceivedMsg=%s",
             this, mLastInstance, ToString(mLastInstance->mRemovingMsg).get(),
             ToString(mLastInstance->mReceivedMsg).get()));
        mLastInstance->mReceivedMsg = mMsg;
        return;
      }

      // NOTE: If other applications like a11y tools sends WM_*CHAR without
      //       scancode, we cannot compute virtual keycode.  I.e., with such
      //       applications, we cannot generate proper KeyboardEvent.code value.

      mVirtualKeyCode = mOriginalVirtualKeyCode =
          ComputeVirtualKeyCodeFromScanCodeEx();
      NS_ASSERTION(mVirtualKeyCode, "Failed to compute virtual keycode");
      break;
    default: {
      MOZ_CRASH_UNSAFE_PRINTF("Unsupported message: 0x%04X", mMsg.message);
      break;
    }
  }

  if (!mVirtualKeyCode) {
    mVirtualKeyCode = mOriginalVirtualKeyCode;
  }

  KeyboardLayout* keyboardLayout = KeyboardLayout::GetInstance();
  mDOMKeyCode =
      keyboardLayout->ConvertNativeKeyCodeToDOMKeyCode(mVirtualKeyCode);
  // Be aware, keyboard utilities can change non-printable keys to printable
  // keys.  In such case, we should make the key value as a printable key.
  // FYI: IsFollowedByPrintableCharMessage() returns true only when it's
  //      handling a keydown message.
  mKeyNameIndex =
      IsFollowedByPrintableCharMessage()
          ? KEY_NAME_INDEX_USE_STRING
          : keyboardLayout->ConvertNativeKeyCodeToKeyNameIndex(mVirtualKeyCode);
  mCodeNameIndex = KeyboardLayout::ConvertScanCodeToCodeNameIndex(
      GetScanCodeWithExtendedFlag());

  // If next message of WM_(SYS)KEYDOWN is WM_*CHAR message and the key
  // combination is not reserved by the system, let's consume it now.
  // TODO: We cannot initialize mCommittedCharsAndModifiers for VK_PACKET
  //       if the message is WM_KEYUP because we don't have preceding
  //       WM_CHAR message.
  // TODO: Like Edge, we shouldn't dispatch two sets of keyboard events
  //       for a Unicode character in non-BMP because its key value looks
  //       broken and not good thing for our editor if only one keydown or
  //       keypress event's default is prevented.  I guess, we should store
  //       key message information globally and we should wait following
  //       WM_KEYDOWN if following WM_CHAR is a part of a Unicode character.
  if ((mMsg.message == WM_KEYDOWN || mMsg.message == WM_SYSKEYDOWN) &&
      !IsReservedBySystem()) {
    MSG charMsg;
    while (GetFollowingCharMessage(charMsg)) {
      // Although, got message shouldn't be WM_NULL in desktop apps,
      // we should keep checking this.  FYI: This was added for Metrofox.
      if (charMsg.message == WM_NULL) {
        continue;
      }
      MOZ_LOG(gKeyLog, LogLevel::Info,
              ("%p   NativeKey::InitWithKeyOrChar(), removed char message, %s",
               this, ToString(charMsg).get()));
      NS_WARNING_ASSERTION(
          charMsg.hwnd == mMsg.hwnd,
          "The retrieved char message was targeted to different window");
      mFollowingCharMsgs.AppendElement(charMsg);
    }
    if (mFollowingCharMsgs.Length() == 1) {
      // If we receive a keydown message for a high-surrogate, a low-surrogate
      // keydown message **will** and should follow it.  We cannot translate the
      // following WM_KEYDOWN message for the low-surrogate right now since
      // it's not yet queued into the message queue yet.  Therefore, we need to
      // wait next one to dispatch keypress event with setting its `.key` value
      // to a surrogate pair rather than setting it to a lone surrogate.
      // FYI: This may happen with typing a non-BMP character on the touch
      // keyboard on Windows 10 or later except when an IME is installed. (If
      // IME is installed, composition is used instead.)
      if (IS_HIGH_SURROGATE(mFollowingCharMsgs[0].wParam)) {
        if (pendingHighSurrogate) {
          MOZ_LOG(gKeyLog, LogLevel::Warning,
                  ("%p   NativeKey::InitWithKeyOrChar(), there is pending "
                   "high surrogate input, but received another high surrogate "
                   "input.  The previous one is discarded",
                   this));
        }
        sPendingHighSurrogate = mFollowingCharMsgs[0].wParam;
        mFollowingCharMsgs.Clear();
      } else if (IS_LOW_SURROGATE(mFollowingCharMsgs[0].wParam)) {
        // If we stopped dispathing a keypress event for a preceding
        // high-surrogate, treat this keydown (for a low-surrogate) as
        // introducing both the high surrogate and the low surrogate.
        if (pendingHighSurrogate) {
          MSG charMsg = mFollowingCharMsgs[0];
          mFollowingCharMsgs[0].wParam = pendingHighSurrogate;
          mFollowingCharMsgs.AppendElement(std::move(charMsg));
        } else {
          MOZ_LOG(
              gKeyLog, LogLevel::Warning,
              ("%p   NativeKey::InitWithKeyOrChar(), there is no pending high "
               "surrogate input, but received lone low surrogate input",
               this));
        }
      } else if (MOZ_UNLIKELY(pendingHighSurrogate)) {
        MOZ_LOG(gKeyLog, LogLevel::Warning,
                ("%p   NativeKey::InitWithKeyOrChar(), there is pending "
                 "high surrogate input, but received non-surrogate input.  "
                 "The high surrogate input is discarded",
                 this));
      }
    } else if (MOZ_UNLIKELY(pendingHighSurrogate &&
                            !mFollowingCharMsgs.IsEmpty())) {
      MOZ_LOG(gKeyLog, LogLevel::Warning,
              ("%p   NativeKey::InitWithKeyOrChar(), there is pending "
               "high surrogate input, but received 2 or more character input.  "
               "The high surrogate input is discarded",
               this));
    }
  }

  keyboardLayout->InitNativeKey(*this);

  // Now, we can know if the key produces character(s) or a dead key with
  // AltGraph modifier.  When user emulates AltGr key press with pressing
  // both Ctrl and Alt and the key produces character(s) or a dead key, we
  // need to replace Control and Alt state with AltGraph if the keyboard
  // layout has AltGr key.
  // Note that if Ctrl and/or Alt are pressed (not to emulate to press AltGr),
  // we need to set actual modifiers to eKeyDown and eKeyUp.
  if (MaybeEmulatingAltGraph() &&
      (mCommittedCharsAndModifiers.IsProducingCharsWithAltGr() ||
       mKeyNameIndex == KEY_NAME_INDEX_Dead)) {
    mModKeyState.Unset(MODIFIER_CONTROL | MODIFIER_ALT);
    mModKeyState.Set(MODIFIER_ALTGRAPH);
  }

  mIsDeadKey =
      (IsFollowedByDeadCharMessage() ||
       keyboardLayout->IsDeadKey(mOriginalVirtualKeyCode, mModKeyState));
  mIsPrintableKey = mKeyNameIndex == KEY_NAME_INDEX_USE_STRING ||
                    KeyboardLayout::IsPrintableCharKey(mOriginalVirtualKeyCode);
  // The repeat count in mMsg.lParam isn't useful to check whether the event
  // is caused by the auto-repeat feature because it's not incremented even
  // if it's repeated twice or more (i.e., always 1).  Therefore, we need to
  // check previous key state (31th bit) instead.  If it's 1, the key was down
  // before the message was sent.
  mIsRepeat = (mMsg.lParam & (1 << 30)) != 0;
  InitIsSkippableForKeyOrChar(lastKeyMSG);

  if (IsKeyDownMessage()) {
    // Compute some strings which may be inputted by the key with various
    // modifier state if this key event won't cause text input actually.
    // They will be used for setting mAlternativeCharCodes in the callback
    // method which will be called by TextEventDispatcher.
    if (!IsFollowedByPrintableCharMessage()) {
      ComputeInputtingStringWithKeyboardLayout();
    }
    // Remove odd char messages if there are.
    RemoveFollowingOddCharMessages();
  }
}

void NativeKey::InitCommittedCharsAndModifiersWithFollowingCharMessages() {
  mCommittedCharsAndModifiers.Clear();
  // This should cause inputting text in focused editor.  However, it
  // ignores keypress events whose altKey or ctrlKey is true.
  // Therefore, we need to remove these modifier state here.
  Modifiers modifiers = mModKeyState.GetModifiers();
  if (IsFollowedByPrintableCharMessage()) {
    modifiers &= ~(MODIFIER_ALT | MODIFIER_CONTROL);
    if (MaybeEmulatingAltGraph()) {
      modifiers |= MODIFIER_ALTGRAPH;
    }
  }
  // NOTE: This method assumes that WM_CHAR and WM_SYSCHAR are never retrieved
  //       at same time.
  for (size_t i = 0; i < mFollowingCharMsgs.Length(); ++i) {
    // Ignore non-printable char messages.
    if (!IsPrintableCharOrSysCharMessage(mFollowingCharMsgs[i])) {
      continue;
    }
    char16_t ch = static_cast<char16_t>(mFollowingCharMsgs[i].wParam);
    mCommittedCharsAndModifiers.Append(ch, modifiers);
  }
}

NativeKey::~NativeKey() {
  MOZ_LOG(gKeyLog, LogLevel::Debug,
          ("%p   NativeKey::~NativeKey(), destroyed", this));
  if (mIsOverridingKeyboardLayout) {
    KeyboardLayout* keyboardLayout = KeyboardLayout::GetInstance();
    keyboardLayout->RestoreLayout();
  }
  sLatestInstance = mLastInstance;
}

void NativeKey::InitWithAppCommand() {
  if (GET_DEVICE_LPARAM(mMsg.lParam) != FAPPCOMMAND_KEY) {
    return;
  }

  uint32_t appCommand = GET_APPCOMMAND_LPARAM(mMsg.lParam);
  switch (GET_APPCOMMAND_LPARAM(mMsg.lParam)) {
#undef NS_APPCOMMAND_TO_DOM_KEY_NAME_INDEX
#define NS_APPCOMMAND_TO_DOM_KEY_NAME_INDEX(aAppCommand, aKeyNameIndex) \
  case aAppCommand:                                                     \
    mKeyNameIndex = aKeyNameIndex;                                      \
    break;

#include "NativeKeyToDOMKeyName.h"

#undef NS_APPCOMMAND_TO_DOM_KEY_NAME_INDEX

    default:
      mKeyNameIndex = KEY_NAME_INDEX_Unidentified;
  }

  // Guess the virtual keycode which caused this message.
  switch (appCommand) {
    case APPCOMMAND_BROWSER_BACKWARD:
      mVirtualKeyCode = mOriginalVirtualKeyCode = VK_BROWSER_BACK;
      break;
    case APPCOMMAND_BROWSER_FORWARD:
      mVirtualKeyCode = mOriginalVirtualKeyCode = VK_BROWSER_FORWARD;
      break;
    case APPCOMMAND_BROWSER_REFRESH:
      mVirtualKeyCode = mOriginalVirtualKeyCode = VK_BROWSER_REFRESH;
      break;
    case APPCOMMAND_BROWSER_STOP:
      mVirtualKeyCode = mOriginalVirtualKeyCode = VK_BROWSER_STOP;
      break;
    case APPCOMMAND_BROWSER_SEARCH:
      mVirtualKeyCode = mOriginalVirtualKeyCode = VK_BROWSER_SEARCH;
      break;
    case APPCOMMAND_BROWSER_FAVORITES:
      mVirtualKeyCode = mOriginalVirtualKeyCode = VK_BROWSER_FAVORITES;
      break;
    case APPCOMMAND_BROWSER_HOME:
      mVirtualKeyCode = mOriginalVirtualKeyCode = VK_BROWSER_HOME;
      break;
    case APPCOMMAND_VOLUME_MUTE:
      mVirtualKeyCode = mOriginalVirtualKeyCode = VK_VOLUME_MUTE;
      break;
    case APPCOMMAND_VOLUME_DOWN:
      mVirtualKeyCode = mOriginalVirtualKeyCode = VK_VOLUME_DOWN;
      break;
    case APPCOMMAND_VOLUME_UP:
      mVirtualKeyCode = mOriginalVirtualKeyCode = VK_VOLUME_UP;
      break;
    case APPCOMMAND_MEDIA_NEXTTRACK:
      mVirtualKeyCode = mOriginalVirtualKeyCode = VK_MEDIA_NEXT_TRACK;
      break;
    case APPCOMMAND_MEDIA_PREVIOUSTRACK:
      mVirtualKeyCode = mOriginalVirtualKeyCode = VK_MEDIA_PREV_TRACK;
      break;
    case APPCOMMAND_MEDIA_STOP:
      mVirtualKeyCode = mOriginalVirtualKeyCode = VK_MEDIA_STOP;
      break;
    case APPCOMMAND_MEDIA_PLAY_PAUSE:
      mVirtualKeyCode = mOriginalVirtualKeyCode = VK_MEDIA_PLAY_PAUSE;
      break;
    case APPCOMMAND_LAUNCH_MAIL:
      mVirtualKeyCode = mOriginalVirtualKeyCode = VK_LAUNCH_MAIL;
      break;
    case APPCOMMAND_LAUNCH_MEDIA_SELECT:
      mVirtualKeyCode = mOriginalVirtualKeyCode = VK_LAUNCH_MEDIA_SELECT;
      break;
    case APPCOMMAND_LAUNCH_APP1:
      mVirtualKeyCode = mOriginalVirtualKeyCode = VK_LAUNCH_APP1;
      break;
    case APPCOMMAND_LAUNCH_APP2:
      mVirtualKeyCode = mOriginalVirtualKeyCode = VK_LAUNCH_APP2;
      break;
    default:
      return;
  }

  uint16_t scanCodeEx = ComputeScanCodeExFromVirtualKeyCode(mVirtualKeyCode);
  mScanCode = static_cast<uint8_t>(scanCodeEx & 0xFF);
  uint8_t extended = static_cast<uint8_t>((scanCodeEx & 0xFF00) >> 8);
  mIsExtended = (extended == 0xE0) || (extended == 0xE1);
  mDOMKeyCode = KeyboardLayout::GetInstance()->ConvertNativeKeyCodeToDOMKeyCode(
      mOriginalVirtualKeyCode);
  mCodeNameIndex = KeyboardLayout::ConvertScanCodeToCodeNameIndex(
      GetScanCodeWithExtendedFlag());
  // If we can map the WM_APPCOMMAND to a virtual keycode, we can trust
  // the result of GetKeyboardState().  Otherwise, we dispatch both
  // keydown and keyup events from WM_APPCOMMAND handler.  Therefore,
  // even if WM_APPCOMMAND is caused by auto key repeat, web apps receive
  // a pair of DOM keydown and keyup events.  I.e., KeyboardEvent.repeat
  // should be never true of such keys.
  // XXX Isn't the key state always true?  If the key press caused this
  //     WM_APPCOMMAND, that means it's pressed at that time.
  if (mVirtualKeyCode) {
    BYTE kbdState[256];
    memset(kbdState, 0, sizeof(kbdState));
    ::GetKeyboardState(kbdState);
    mIsSkippableInRemoteProcess = mIsRepeat = !!kbdState[mVirtualKeyCode];
  }
}

bool NativeKey::MaybeEmulatingAltGraph() const {
  return IsControl() && IsAlt() && KeyboardLayout::GetInstance()->HasAltGr();
}

// static
bool NativeKey::IsControlChar(char16_t aChar) {
  static const char16_t U_SPACE = 0x20;
  static const char16_t U_DELETE = 0x7F;
  return aChar < U_SPACE || aChar == U_DELETE;
}

bool NativeKey::IsFollowedByDeadCharMessage() const {
  if (mFollowingCharMsgs.IsEmpty()) {
    return false;
  }
  return IsDeadCharMessage(mFollowingCharMsgs[0]);
}

bool NativeKey::IsFollowedByPrintableCharMessage() const {
  for (size_t i = 0; i < mFollowingCharMsgs.Length(); ++i) {
    if (IsPrintableCharMessage(mFollowingCharMsgs[i])) {
      return true;
    }
  }
  return false;
}

bool NativeKey::IsFollowedByPrintableCharOrSysCharMessage() const {
  for (size_t i = 0; i < mFollowingCharMsgs.Length(); ++i) {
    if (IsPrintableCharOrSysCharMessage(mFollowingCharMsgs[i])) {
      return true;
    }
  }
  return false;
}

bool NativeKey::IsReservedBySystem() const {
  // Alt+Space key is handled by OS, we shouldn't touch it.
  if (mModKeyState.IsAlt() && !mModKeyState.IsControl() &&
      mVirtualKeyCode == VK_SPACE) {
    return true;
  }

  // XXX How about Alt+F4? We receive WM_SYSKEYDOWN for F4 before closing the
  //     window.  Although, we don't prevent to close the window but the key
  //     event shouldn't be exposed to the web.

  return false;
}

bool NativeKey::IsIMEDoingKakuteiUndo() const {
  // Following message pattern is caused by "Kakutei-Undo" of ATOK or WXG:
  // ---------------------------------------------------------------------------
  // WM_KEYDOWN              * n (wParam = VK_BACK, lParam = 0x1)
  // WM_KEYUP                * 1 (wParam = VK_BACK, lParam = 0xC0000001) # ATOK
  // WM_IME_STARTCOMPOSITION * 1 (wParam = 0x0, lParam = 0x0)
  // WM_IME_COMPOSITION      * 1 (wParam = 0x0, lParam = 0x1BF)
  // WM_CHAR                 * n (wParam = VK_BACK, lParam = 0x1)
  // WM_KEYUP                * 1 (wParam = VK_BACK, lParam = 0xC00E0001)
  // ---------------------------------------------------------------------------
  // This doesn't match usual key message pattern such as:
  //   WM_KEYDOWN -> WM_CHAR -> WM_KEYDOWN -> WM_CHAR -> ... -> WM_KEYUP
  // See following bugs for the detail.
  // https://bugzilla.mozilla.gr.jp/show_bug.cgi?id=2885 (written in Japanese)
  // https://bugzilla.mozilla.org/show_bug.cgi?id=194559 (written in English)
  MSG startCompositionMsg, compositionMsg, charMsg;
  return WinUtils::PeekMessage(&startCompositionMsg, mMsg.hwnd,
                               WM_IME_STARTCOMPOSITION, WM_IME_STARTCOMPOSITION,
                               PM_NOREMOVE | PM_NOYIELD) &&
         WinUtils::PeekMessage(&compositionMsg, mMsg.hwnd, WM_IME_COMPOSITION,
                               WM_IME_COMPOSITION, PM_NOREMOVE | PM_NOYIELD) &&
         WinUtils::PeekMessage(&charMsg, mMsg.hwnd, WM_CHAR, WM_CHAR,
                               PM_NOREMOVE | PM_NOYIELD) &&
         startCompositionMsg.wParam == 0x0 &&
         startCompositionMsg.lParam == 0x0 && compositionMsg.wParam == 0x0 &&
         compositionMsg.lParam == 0x1BF && charMsg.wParam == VK_BACK &&
         charMsg.lParam == 0x1 &&
         startCompositionMsg.time <= compositionMsg.time &&
         compositionMsg.time <= charMsg.time;
}

void NativeKey::RemoveFollowingOddCharMessages() {
  MOZ_ASSERT(IsKeyDownMessage());

  // If the keydown message is synthesized for automated tests, there is
  // nothing to do here.
  if (mFakeCharMsgs) {
    return;
  }

  // If there are some following char messages before another key message,
  // there is nothing to do here.
  if (!mFollowingCharMsgs.IsEmpty()) {
    return;
  }

  // If the handling key isn't Backspace, there is nothing to do here.
  if (mOriginalVirtualKeyCode != VK_BACK) {
    return;
  }

  // If we don't see the odd message pattern, there is nothing to do here.
  if (!IsIMEDoingKakuteiUndo()) {
    return;
  }

  // Otherwise, we need to remove odd WM_CHAR messages for ATOK or WXG (both
  // of them are Japanese IME).
  MSG msg;
  while (WinUtils::PeekMessage(&msg, mMsg.hwnd, WM_CHAR, WM_CHAR,
                               PM_REMOVE | PM_NOYIELD)) {
    if (msg.message != WM_CHAR) {
      MOZ_RELEASE_ASSERT(msg.message == WM_NULL,
                         "Unexpected message was removed");
      continue;
    }
    MOZ_LOG(
        gKeyLog, LogLevel::Info,
        ("%p   NativeKey::RemoveFollowingOddCharMessages(), removed odd char "
         "message, %s",
         this, ToString(msg).get()));
    mRemovedOddCharMsgs.AppendElement(msg);
  }
}

UINT NativeKey::GetScanCodeWithExtendedFlag() const {
  if (!mIsExtended) {
    return mScanCode;
  }
  return (0xE000 | mScanCode);
}

uint32_t NativeKey::GetKeyLocation() const {
  switch (mVirtualKeyCode) {
    case VK_LSHIFT:
    case VK_LCONTROL:
    case VK_LMENU:
    case VK_LWIN:
      return eKeyLocationLeft;

    case VK_RSHIFT:
    case VK_RCONTROL:
    case VK_RMENU:
    case VK_RWIN:
      return eKeyLocationRight;

    case VK_RETURN:
      // XXX This code assumes that all keyboard drivers use same mapping.
      return !mIsExtended ? eKeyLocationStandard : eKeyLocationNumpad;

    case VK_INSERT:
    case VK_DELETE:
    case VK_END:
    case VK_DOWN:
    case VK_NEXT:
    case VK_LEFT:
    case VK_CLEAR:
    case VK_RIGHT:
    case VK_HOME:
    case VK_UP:
    case VK_PRIOR:
      // XXX This code assumes that all keyboard drivers use same mapping.
      return mIsExtended ? eKeyLocationStandard : eKeyLocationNumpad;

    // NumLock key isn't included due to IE9's behavior.
    case VK_NUMPAD0:
    case VK_NUMPAD1:
    case VK_NUMPAD2:
    case VK_NUMPAD3:
    case VK_NUMPAD4:
    case VK_NUMPAD5:
    case VK_NUMPAD6:
    case VK_NUMPAD7:
    case VK_NUMPAD8:
    case VK_NUMPAD9:
    case VK_DECIMAL:
    case VK_DIVIDE:
    case VK_MULTIPLY:
    case VK_SUBTRACT:
    case VK_ADD:
    // Separator key of Brazilian keyboard or JIS keyboard for Mac
    case VK_ABNT_C2:
      return eKeyLocationNumpad;

    case VK_SHIFT:
    case VK_CONTROL:
    case VK_MENU:
      NS_WARNING("Failed to decide the key location?");
      [[fallthrough]];

    default:
      return eKeyLocationStandard;
  }
}

uint8_t NativeKey::ComputeVirtualKeyCodeFromScanCode() const {
  return static_cast<uint8_t>(
      ::MapVirtualKeyEx(mScanCode, MAPVK_VSC_TO_VK, mKeyboardLayout));
}

uint8_t NativeKey::ComputeVirtualKeyCodeFromScanCodeEx() const {
  // MapVirtualKeyEx() has been improved for supporting extended keys since
  // Vista.  When we call it for mapping a scancode of an extended key and
  // a virtual keycode, we need to add 0xE000 to the scancode.
  return static_cast<uint8_t>(::MapVirtualKeyEx(
      GetScanCodeWithExtendedFlag(), MAPVK_VSC_TO_VK_EX, mKeyboardLayout));
}

uint16_t NativeKey::ComputeScanCodeExFromVirtualKeyCode(
    UINT aVirtualKeyCode) const {
  return static_cast<uint16_t>(
      ::MapVirtualKeyEx(aVirtualKeyCode, MAPVK_VK_TO_VSC_EX, mKeyboardLayout));
}

char16_t NativeKey::ComputeUnicharFromScanCode() const {
  return static_cast<char16_t>(::MapVirtualKeyEx(
      ComputeVirtualKeyCodeFromScanCode(), MAPVK_VK_TO_CHAR, mKeyboardLayout));
}

nsEventStatus NativeKey::InitKeyEvent(WidgetKeyboardEvent& aKeyEvent) const {
  return InitKeyEvent(aKeyEvent, mModKeyState);
}

nsEventStatus NativeKey::InitKeyEvent(
    WidgetKeyboardEvent& aKeyEvent,
    const ModifierKeyState& aModKeyState) const {
  if (mWidget->Destroyed()) {
    MOZ_CRASH("NativeKey tries to dispatch a key event on destroyed widget");
  }

  LayoutDeviceIntPoint point(0, 0);
  mWidget->InitEvent(aKeyEvent, &point);

  switch (aKeyEvent.mMessage) {
    case eKeyDown:
      // If it was followed by a char message but it was consumed by somebody,
      // we should mark it as consumed because somebody must have handled it
      // and we should prevent to do "double action" for the key operation.
      // However, for compatibility with older version and other browsers,
      // we should dispatch the events even in the web content.
      // And also if it's a WM_SYSKEYDOWN which is not followed by WM_SYSCHAR,
      // the input may be consumed by the builtin IME to input a Unicode
      // character from the code point.
      if (mCharMessageHasGone || (IsSysKeyDownMessage() && mIsPrintableKey &&
                                  mFollowingCharMsgs.IsEmpty())) {
        aKeyEvent.PreventDefaultBeforeDispatch(CrossProcessForwarding::eAllow);
      }
      aKeyEvent.mKeyCode = mDOMKeyCode;
      // Unique id for this keydown event and its associated keypress.
      sUniqueKeyEventId++;
      aKeyEvent.mUniqueId = sUniqueKeyEventId;
      break;
    case eKeyUp:
      aKeyEvent.mKeyCode = mDOMKeyCode;
      // Set defaultPrevented of the key event if the VK_MENU is not a system
      // key release, so that the menu bar does not trigger.  This helps avoid
      // triggering the menu bar for ALT key accelerators used in assistive
      // technologies such as Window-Eyes and ZoomText or for switching open
      // state of IME.  On the other hand, we should dispatch the events even
      // in the web content for compatibility with older version and other
      // browsers.
      if (mOriginalVirtualKeyCode == VK_MENU && mMsg.message != WM_SYSKEYUP) {
        aKeyEvent.PreventDefaultBeforeDispatch(CrossProcessForwarding::eAllow);
      }
      break;
    case eKeyPress:
      MOZ_ASSERT(!mCharMessageHasGone,
                 "If following char message was consumed by somebody, "
                 "keydown event should be consumed above");
      aKeyEvent.mUniqueId = sUniqueKeyEventId;
      break;
    default:
      MOZ_CRASH("Invalid event message");
  }

  aKeyEvent.mIsRepeat = mIsRepeat;
  aKeyEvent.mMaybeSkippableInRemoteProcess = mIsSkippableInRemoteProcess;
  aKeyEvent.mKeyNameIndex = mKeyNameIndex;
  if (mKeyNameIndex == KEY_NAME_INDEX_USE_STRING) {
    aKeyEvent.mKeyValue = mCommittedCharsAndModifiers.ToString();
  }
  aKeyEvent.mCodeNameIndex = mCodeNameIndex;
  MOZ_ASSERT(mCodeNameIndex != CODE_NAME_INDEX_USE_STRING);
  aKeyEvent.mLocation = GetKeyLocation();
  aModKeyState.InitInputEvent(aKeyEvent);

  KeyboardLayout::NotifyIdleServiceOfUserActivity();

  MOZ_LOG(
      gKeyLog, LogLevel::Info,
      ("%p   NativeKey::InitKeyEvent(), initialized, aKeyEvent={ "
       "mMessage=%s, mKeyNameIndex=%s, mKeyValue=\"%s\", mCodeNameIndex=%s, "
       "mKeyCode=%s, mLocation=%s, mModifiers=%s, DefaultPrevented()=%s }",
       this, ToChar(aKeyEvent.mMessage),
       ToString(aKeyEvent.mKeyNameIndex).get(),
       NS_ConvertUTF16toUTF8(aKeyEvent.mKeyValue).get(),
       ToString(aKeyEvent.mCodeNameIndex).get(),
       GetDOMKeyCodeName(aKeyEvent.mKeyCode).get(),
       GetKeyLocationName(aKeyEvent.mLocation).get(),
       GetModifiersName(aKeyEvent.mModifiers).get(),
       TrueOrFalse(aKeyEvent.DefaultPrevented())));

  return aKeyEvent.DefaultPrevented() ? nsEventStatus_eConsumeNoDefault
                                      : nsEventStatus_eIgnore;
}

bool NativeKey::DispatchCommandEvent(uint32_t aEventCommand) const {
  RefPtr<nsAtom> command;
  switch (aEventCommand) {
    case APPCOMMAND_BROWSER_BACKWARD:
      command = nsGkAtoms::Back;
      break;
    case APPCOMMAND_BROWSER_FORWARD:
      command = nsGkAtoms::Forward;
      break;
    case APPCOMMAND_BROWSER_REFRESH:
      command = nsGkAtoms::Reload;
      break;
    case APPCOMMAND_BROWSER_STOP:
      command = nsGkAtoms::Stop;
      break;
    case APPCOMMAND_BROWSER_SEARCH:
      command = nsGkAtoms::Search;
      break;
    case APPCOMMAND_BROWSER_FAVORITES:
      command = nsGkAtoms::Bookmarks;
      break;
    case APPCOMMAND_BROWSER_HOME:
      command = nsGkAtoms::Home;
      break;
    case APPCOMMAND_CLOSE:
      command = nsGkAtoms::Close;
      break;
    case APPCOMMAND_FIND:
      command = nsGkAtoms::Find;
      break;
    case APPCOMMAND_HELP:
      command = nsGkAtoms::Help;
      break;
    case APPCOMMAND_NEW:
      command = nsGkAtoms::New;
      break;
    case APPCOMMAND_OPEN:
      command = nsGkAtoms::Open;
      break;
    case APPCOMMAND_PRINT:
      command = nsGkAtoms::Print;
      break;
    case APPCOMMAND_SAVE:
      command = nsGkAtoms::Save;
      break;
    case APPCOMMAND_FORWARD_MAIL:
      command = nsGkAtoms::ForwardMail;
      break;
    case APPCOMMAND_REPLY_TO_MAIL:
      command = nsGkAtoms::ReplyToMail;
      break;
    case APPCOMMAND_SEND_MAIL:
      command = nsGkAtoms::SendMail;
      break;
    case APPCOMMAND_MEDIA_NEXTTRACK:
      command = nsGkAtoms::NextTrack;
      break;
    case APPCOMMAND_MEDIA_PREVIOUSTRACK:
      command = nsGkAtoms::PreviousTrack;
      break;
    case APPCOMMAND_MEDIA_STOP:
      command = nsGkAtoms::MediaStop;
      break;
    case APPCOMMAND_MEDIA_PLAY_PAUSE:
      command = nsGkAtoms::PlayPause;
      break;
    default:
      MOZ_LOG(
          gKeyLog, LogLevel::Info,
          ("%p   NativeKey::DispatchCommandEvent(), doesn't dispatch command "
           "event",
           this));
      return false;
  }
  WidgetCommandEvent appCommandEvent(true, command, mWidget);

  mWidget->InitEvent(appCommandEvent);
  MOZ_LOG(gKeyLog, LogLevel::Info,
          ("%p   NativeKey::DispatchCommandEvent(), dispatching "
           "%s app command event...",
           this, nsAtomCString(command).get()));
  bool ok =
      mWidget->DispatchWindowEvent(appCommandEvent) || mWidget->Destroyed();
  MOZ_LOG(
      gKeyLog, LogLevel::Info,
      ("%p   NativeKey::DispatchCommandEvent(), dispatched app command event, "
       "result=%s, mWidget->Destroyed()=%s",
       this, SucceededOrFailed(ok), TrueOrFalse(mWidget->Destroyed())));
  return ok;
}

bool NativeKey::HandleAppCommandMessage() const {
  // If the widget has gone, we should do nothing.
  if (mWidget->Destroyed()) {
    MOZ_LOG(gKeyLog, LogLevel::Warning,
            ("%p   NativeKey::HandleAppCommandMessage(), WARNING, not handled "
             "due to "
             "destroyed the widget",
             this));
    return false;
  }

  // NOTE: Typical behavior of WM_APPCOMMAND caused by key is, WM_APPCOMMAND
  //       message is _sent_ first.  Then, the DefaultWndProc will _post_
  //       WM_KEYDOWN message and WM_KEYUP message if the keycode for the
  //       command is available (i.e., mVirtualKeyCode is not 0).

  // NOTE: IntelliType (Microsoft's keyboard utility software) always consumes
  //       WM_KEYDOWN and WM_KEYUP.

  // Let's dispatch keydown message before our chrome handles the command
  // when the message is caused by a keypress.  This behavior makes handling
  // WM_APPCOMMAND be a default action of the keydown event.  This means that
  // web applications can handle multimedia keys and prevent our default action.
  // This allow web applications to provide better UX for multimedia keyboard
  // users.
  bool dispatchKeyEvent = (GET_DEVICE_LPARAM(mMsg.lParam) == FAPPCOMMAND_KEY);
  if (dispatchKeyEvent) {
    // If a plug-in window has focus but it didn't consume the message, our
    // window receive WM_APPCOMMAND message.  In this case, we shouldn't
    // dispatch KeyboardEvents because an event handler may access the
    // plug-in process synchronously.
    dispatchKeyEvent =
        WinUtils::IsOurProcessWindow(reinterpret_cast<HWND>(mMsg.wParam));
  }

  bool consumed = false;

  if (dispatchKeyEvent) {
    nsresult rv = mDispatcher->BeginNativeInputTransaction();
    if (NS_WARN_IF(NS_FAILED(rv))) {
      MOZ_LOG(gKeyLog, LogLevel::Error,
              ("%p   NativeKey::HandleAppCommandMessage(), FAILED due to "
               "BeginNativeInputTransaction() failure",
               this));
      return true;
    }
    MOZ_LOG(gKeyLog, LogLevel::Info,
            ("%p   NativeKey::HandleAppCommandMessage(), initializing keydown "
             "event...",
             this));
    WidgetKeyboardEvent keydownEvent(true, eKeyDown, mWidget);
    nsEventStatus status = InitKeyEvent(keydownEvent, mModKeyState);
    MOZ_LOG(gKeyLog, LogLevel::Info,
            ("%p   NativeKey::HandleAppCommandMessage(), tries to dispatch "
             "keydown event...",
             this));
    // NOTE: If the keydown event is consumed by web contents, we shouldn't
    //       continue to handle the command.
    if (!mDispatcher->DispatchKeyboardEvent(eKeyDown, keydownEvent, status,
                                            const_cast<NativeKey*>(this))) {
      MOZ_LOG(gKeyLog, LogLevel::Info,
              ("%p   NativeKey::HandleAppCommandMessage(), keydown event isn't "
               "dispatched",
               this));
      // If keyboard event wasn't fired, there must be composition.
      // So, we don't need to dispatch a command event.
      return true;
    }
    consumed = status == nsEventStatus_eConsumeNoDefault;
    MOZ_LOG(gKeyLog, LogLevel::Info,
            ("%p   NativeKey::HandleAppCommandMessage(), keydown event was "
             "dispatched, consumed=%s",
             this, TrueOrFalse(consumed)));
    sDispatchedKeyOfAppCommand = mVirtualKeyCode;
    if (mWidget->Destroyed()) {
      MOZ_LOG(
          gKeyLog, LogLevel::Info,
          ("%p   NativeKey::HandleAppCommandMessage(), keydown event caused "
           "destroying the widget",
           this));
      return true;
    }
  }

  // Dispatch a command event or a content command event if the command is
  // supported.
  if (!consumed) {
    uint32_t appCommand = GET_APPCOMMAND_LPARAM(mMsg.lParam);
    EventMessage contentCommandMessage = eVoidEvent;
    switch (appCommand) {
      case APPCOMMAND_BROWSER_BACKWARD:
      case APPCOMMAND_BROWSER_FORWARD:
      case APPCOMMAND_BROWSER_REFRESH:
      case APPCOMMAND_BROWSER_STOP:
      case APPCOMMAND_BROWSER_SEARCH:
      case APPCOMMAND_BROWSER_FAVORITES:
      case APPCOMMAND_BROWSER_HOME:
      case APPCOMMAND_CLOSE:
      case APPCOMMAND_FIND:
      case APPCOMMAND_HELP:
      case APPCOMMAND_NEW:
      case APPCOMMAND_OPEN:
      case APPCOMMAND_PRINT:
      case APPCOMMAND_SAVE:
      case APPCOMMAND_FORWARD_MAIL:
      case APPCOMMAND_REPLY_TO_MAIL:
      case APPCOMMAND_SEND_MAIL:
      case APPCOMMAND_MEDIA_NEXTTRACK:
      case APPCOMMAND_MEDIA_PREVIOUSTRACK:
      case APPCOMMAND_MEDIA_STOP:
      case APPCOMMAND_MEDIA_PLAY_PAUSE:
        // We shouldn't consume the message always because if we don't handle
        // the message, the sender (typically, utility of keyboard or mouse)
        // may send other key messages which indicate well known shortcut key.
        consumed = DispatchCommandEvent(appCommand);
        break;

      // Use content command for following commands:
      case APPCOMMAND_COPY:
        contentCommandMessage = eContentCommandCopy;
        break;
      case APPCOMMAND_CUT:
        contentCommandMessage = eContentCommandCut;
        break;
      case APPCOMMAND_PASTE:
        contentCommandMessage = eContentCommandPaste;
        break;
      case APPCOMMAND_REDO:
        contentCommandMessage = eContentCommandRedo;
        break;
      case APPCOMMAND_UNDO:
        contentCommandMessage = eContentCommandUndo;
        break;
    }

    if (contentCommandMessage) {
      MOZ_ASSERT(!mWidget->Destroyed());
      WidgetContentCommandEvent contentCommandEvent(true, contentCommandMessage,
                                                    mWidget);
      MOZ_LOG(
          gKeyLog, LogLevel::Info,
          ("%p   NativeKey::HandleAppCommandMessage(), dispatching %s event...",
           this, ToChar(contentCommandMessage)));
      mWidget->DispatchWindowEvent(contentCommandEvent);
      MOZ_LOG(gKeyLog, LogLevel::Info,
              ("%p   NativeKey::HandleAppCommandMessage(), dispatched %s event",
               this, ToChar(contentCommandMessage)));
      consumed = true;

      if (mWidget->Destroyed()) {
        MOZ_LOG(gKeyLog, LogLevel::Info,
                ("%p   NativeKey::HandleAppCommandMessage(), %s event caused "
                 "destroying the widget",
                 this, ToChar(contentCommandMessage)));
        return true;
      }
    } else {
      MOZ_LOG(gKeyLog, LogLevel::Info,
              ("%p   NativeKey::HandleAppCommandMessage(), doesn't dispatch "
               "content "
               "command event",
               this));
    }
  }

  // Dispatch a keyup event if the command is caused by pressing a key and
  // the key isn't mapped to a virtual keycode.
  if (dispatchKeyEvent && !mVirtualKeyCode) {
    MOZ_ASSERT(!mWidget->Destroyed());
    nsresult rv = mDispatcher->BeginNativeInputTransaction();
    if (NS_WARN_IF(NS_FAILED(rv))) {
      MOZ_LOG(gKeyLog, LogLevel::Error,
              ("%p   NativeKey::HandleAppCommandMessage(), FAILED due to "
               "BeginNativeInputTransaction() failure",
               this));
      return true;
    }
    MOZ_LOG(gKeyLog, LogLevel::Info,
            ("%p   NativeKey::HandleAppCommandMessage(), initializing keyup "
             "event...",
             this));
    WidgetKeyboardEvent keyupEvent(true, eKeyUp, mWidget);
    nsEventStatus status = InitKeyEvent(keyupEvent, mModKeyState);
    MOZ_LOG(gKeyLog, LogLevel::Info,
            ("%p   NativeKey::HandleAppCommandMessage(), dispatching keyup "
             "event...",
             this));
    // NOTE: Ignore if the keyup event is consumed because keyup event
    //       represents just a physical key event state change.
    mDispatcher->DispatchKeyboardEvent(eKeyUp, keyupEvent, status,
                                       const_cast<NativeKey*>(this));
    MOZ_LOG(
        gKeyLog, LogLevel::Info,
        ("%p   NativeKey::HandleAppCommandMessage(), dispatched keyup event",
         this));
    if (mWidget->Destroyed()) {
      MOZ_LOG(gKeyLog, LogLevel::Info,
              ("%p   NativeKey::HandleAppCommandMessage(), keyup event caused "
               "destroying the widget",
               this));
      return true;
    }
  }

  return consumed;
}

bool NativeKey::HandleKeyDownMessage(bool* aEventDispatched) const {
  MOZ_ASSERT(IsKeyDownMessage());

  if (aEventDispatched) {
    *aEventDispatched = false;
  }

  if (sDispatchedKeyOfAppCommand &&
      sDispatchedKeyOfAppCommand == mOriginalVirtualKeyCode) {
    // The multimedia key event has already been dispatch from
    // HandleAppCommandMessage().
    sDispatchedKeyOfAppCommand = 0;
    MOZ_LOG(gKeyLog, LogLevel::Info,
            ("%p   NativeKey::HandleKeyDownMessage(), doesn't dispatch keydown "
             "event due to already dispatched from HandleAppCommandMessage(), ",
             this));
    if (RedirectedKeyDownMessageManager::IsRedirectedMessage(mMsg)) {
      RedirectedKeyDownMessageManager::Forget();
    }
    return true;
  }

  if (IsReservedBySystem()) {
    MOZ_LOG(gKeyLog, LogLevel::Info,
            ("%p   NativeKey::HandleKeyDownMessage(), doesn't dispatch keydown "
             "event because the key combination is reserved by the system",
             this));
    if (RedirectedKeyDownMessageManager::IsRedirectedMessage(mMsg)) {
      RedirectedKeyDownMessageManager::Forget();
    }
    return false;
  }

  if (sPendingHighSurrogate) {
    MOZ_LOG(gKeyLog, LogLevel::Info,
            ("%p   NativeKey::HandleKeyDownMessage(), doesn't dispatch keydown "
             "event because the key introduced only a high surrotate, so we "
             "should wait the following low surrogate input",
             this));
    if (RedirectedKeyDownMessageManager::IsRedirectedMessage(mMsg)) {
      RedirectedKeyDownMessageManager::Forget();
    }
    return false;
  }

  // If the widget has gone, we should do nothing.
  if (mWidget->Destroyed()) {
    MOZ_LOG(
        gKeyLog, LogLevel::Warning,
        ("%p   NativeKey::HandleKeyDownMessage(), WARNING, not handled due to "
         "destroyed the widget",
         this));
    if (RedirectedKeyDownMessageManager::IsRedirectedMessage(mMsg)) {
      RedirectedKeyDownMessageManager::Forget();
    }
    return false;
  }

  bool defaultPrevented = false;
  if (mFakeCharMsgs ||
      !RedirectedKeyDownMessageManager::IsRedirectedMessage(mMsg)) {
    nsresult rv = mDispatcher->BeginNativeInputTransaction();
    if (NS_WARN_IF(NS_FAILED(rv))) {
      MOZ_LOG(gKeyLog, LogLevel::Error,
              ("%p   NativeKey::HandleKeyDownMessage(), FAILED due to "
               "BeginNativeInputTransaction() failure",
               this));
      return true;
    }

    bool isIMEEnabled = WinUtils::IsIMEEnabled(mWidget->GetInputContext());

    MOZ_LOG(gKeyLog, LogLevel::Debug,
            ("%p   NativeKey::HandleKeyDownMessage(), initializing keydown "
             "event...",
             this));

    WidgetKeyboardEvent keydownEvent(true, eKeyDown, mWidget);
    nsEventStatus status = InitKeyEvent(keydownEvent, mModKeyState);
    MOZ_LOG(
        gKeyLog, LogLevel::Info,
        ("%p   NativeKey::HandleKeyDownMessage(), dispatching keydown event...",
         this));
    bool dispatched = mDispatcher->DispatchKeyboardEvent(
        eKeyDown, keydownEvent, status, const_cast<NativeKey*>(this));
    if (aEventDispatched) {
      *aEventDispatched = dispatched;
    }
    if (!dispatched) {
      // If the keydown event wasn't fired, there must be composition.
      // we don't need to do anything anymore.
      MOZ_LOG(
          gKeyLog, LogLevel::Info,
          ("%p   NativeKey::HandleKeyDownMessage(), doesn't dispatch keypress "
           "event(s) because keydown event isn't dispatched actually",
           this));
      return false;
    }
    defaultPrevented = status == nsEventStatus_eConsumeNoDefault;

    if (mWidget->Destroyed() || IsFocusedWindowChanged()) {
      MOZ_LOG(gKeyLog, LogLevel::Info,
              ("%p   NativeKey::HandleKeyDownMessage(), keydown event caused "
               "destroying the widget",
               this));
      return true;
    }

    MOZ_LOG(
        gKeyLog, LogLevel::Info,
        ("%p   NativeKey::HandleKeyDownMessage(), dispatched keydown event, "
         "dispatched=%s, defaultPrevented=%s",
         this, TrueOrFalse(dispatched), TrueOrFalse(defaultPrevented)));

    // If IMC wasn't associated to the window but is associated it now (i.e.,
    // focus is moved from a non-editable editor to an editor by keydown
    // event handler), WM_CHAR and WM_SYSCHAR shouldn't cause first character
    // inputting if IME is opened.  But then, we should redirect the native
    // keydown message to IME.
    // However, note that if focus has been already moved to another
    // application, we shouldn't redirect the message to it because the keydown
    // message is processed by us, so, nobody shouldn't process it.
    HWND focusedWnd = ::GetFocus();
    if (!defaultPrevented && !mFakeCharMsgs && focusedWnd && !isIMEEnabled &&
        WinUtils::IsIMEEnabled(mWidget->GetInputContext())) {
      RedirectedKeyDownMessageManager::RemoveNextCharMessage(focusedWnd);

      INPUT keyinput;
      keyinput.type = INPUT_KEYBOARD;
      keyinput.ki.wVk = mOriginalVirtualKeyCode;
      keyinput.ki.wScan = mScanCode;
      keyinput.ki.dwFlags = KEYEVENTF_SCANCODE;
      if (mIsExtended) {
        keyinput.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
      }
      keyinput.ki.time = 0;
      keyinput.ki.dwExtraInfo = 0;

      RedirectedKeyDownMessageManager::WillRedirect(mMsg, defaultPrevented);

      MOZ_LOG(gKeyLog, LogLevel::Info,
              ("%p   NativeKey::HandleKeyDownMessage(), redirecting %s...",
               this, ToString(mMsg).get()));

      ::SendInput(1, &keyinput, sizeof(keyinput));

      MOZ_LOG(gKeyLog, LogLevel::Info,
              ("%p   NativeKey::HandleKeyDownMessage(), redirected %s", this,
               ToString(mMsg).get()));

      // Return here.  We shouldn't dispatch keypress event for this WM_KEYDOWN.
      // If it's needed, it will be dispatched after next (redirected)
      // WM_KEYDOWN.
      return true;
    }
  } else {
    MOZ_LOG(gKeyLog, LogLevel::Info,
            ("%p   NativeKey::HandleKeyDownMessage(), received a redirected %s",
             this, ToString(mMsg).get()));

    defaultPrevented = RedirectedKeyDownMessageManager::DefaultPrevented();
    // If this is redirected keydown message, we have dispatched the keydown
    // event already.
    if (aEventDispatched) {
      *aEventDispatched = true;
    }
  }

  RedirectedKeyDownMessageManager::Forget();

  MOZ_ASSERT(!mWidget->Destroyed());

  // If the key was processed by IME and didn't cause WM_(SYS)CHAR messages, we
  // shouldn't dispatch keypress event.
  if (mOriginalVirtualKeyCode == VK_PROCESSKEY &&
      !IsFollowedByPrintableCharOrSysCharMessage()) {
    MOZ_LOG(gKeyLog, LogLevel::Info,
            ("%p   NativeKey::HandleKeyDownMessage(), not dispatching keypress "
             "event because the key was already handled by IME, "
             "defaultPrevented=%s",
             this, TrueOrFalse(defaultPrevented)));
    return defaultPrevented;
  }

  if (defaultPrevented) {
    MOZ_LOG(gKeyLog, LogLevel::Info,
            ("%p   NativeKey::HandleKeyDownMessage(), not dispatching keypress "
             "event because preceding keydown event was consumed",
             this));
    return true;
  }

  MOZ_ASSERT(!mCharMessageHasGone,
             "If following char message was consumed by somebody, "
             "keydown event should have been consumed before dispatch");

  // If mCommittedCharsAndModifiers was initialized with following char
  // messages, we should dispatch keypress events with its information.
  if (IsFollowedByPrintableCharOrSysCharMessage()) {
    MOZ_LOG(gKeyLog, LogLevel::Info,
            ("%p   NativeKey::HandleKeyDownMessage(), tries to be dispatching "
             "keypress events with retrieved char messages...",
             this));
    return DispatchKeyPressEventsWithRetrievedCharMessages();
  }

  // If we won't be getting a WM_CHAR, WM_SYSCHAR or WM_DEADCHAR, synthesize a
  // keypress for almost all keys
  if (NeedsToHandleWithoutFollowingCharMessages()) {
    MOZ_LOG(gKeyLog, LogLevel::Info,
            ("%p   NativeKey::HandleKeyDownMessage(), tries to be dispatching "
             "keypress events...",
             this));
    return DispatchKeyPressEventsWithoutCharMessage();
  }

  // If WM_KEYDOWN of VK_PACKET isn't followed by WM_CHAR, we don't need to
  // dispatch keypress events.
  if (mVirtualKeyCode == VK_PACKET) {
    MOZ_LOG(gKeyLog, LogLevel::Info,
            ("%p   NativeKey::HandleKeyDownMessage(), not dispatching keypress "
             "event "
             "because the key is VK_PACKET and there are no char messages",
             this));
    return false;
  }

  if (!mModKeyState.IsControl() && !mModKeyState.IsAlt() &&
      !mModKeyState.IsWin() && mIsPrintableKey) {
    // If this is simple KeyDown event but next message is not WM_CHAR,
    // this event may not input text, so we should ignore this event.
    // See bug 314130.
    MOZ_LOG(gKeyLog, LogLevel::Info,
            ("%p   NativeKey::HandleKeyDownMessage(), not dispatching keypress "
             "event "
             "because the key event is simple printable key's event but not "
             "followed "
             "by char messages",
             this));
    return false;
  }

  if (mIsDeadKey) {
    MOZ_LOG(gKeyLog, LogLevel::Info,
            ("%p   NativeKey::HandleKeyDownMessage(), not dispatching keypress "
             "event "
             "because the key is a dead key and not followed by char messages",
             this));
    return false;
  }

  MOZ_LOG(gKeyLog, LogLevel::Info,
          ("%p   NativeKey::HandleKeyDownMessage(), tries to be dispatching "
           "keypress events due to no following char messages...",
           this));
  return DispatchKeyPressEventsWithoutCharMessage();
}

bool NativeKey::HandleCharMessage(bool* aEventDispatched) const {
  MOZ_ASSERT(IsCharOrSysCharMessage(mMsg));
  return HandleCharMessage(mMsg, aEventDispatched);
}

bool NativeKey::HandleCharMessage(const MSG& aCharMsg,
                                  bool* aEventDispatched) const {
  MOZ_ASSERT(IsKeyDownMessage() || IsCharOrSysCharMessage(mMsg));
  MOZ_ASSERT(IsCharOrSysCharMessage(aCharMsg.message));

  if (aEventDispatched) {
    *aEventDispatched = false;
  }

  if ((IsCharOrSysCharMessage(mMsg) || IsEnterKeyPressCharMessage(mMsg)) &&
      IsAnotherInstanceRemovingCharMessage()) {
    MOZ_LOG(
        gKeyLog, LogLevel::Warning,
        ("%p   NativeKey::HandleCharMessage(), WARNING, does nothing because "
         "the message should be handled in another instance removing this "
         "message",
         this));
    // Consume this for now because it will be handled by another instance.
    return true;
  }

  // If the key combinations is reserved by the system, we shouldn't dispatch
  // eKeyPress event for it and passes the message to next wndproc.
  if (IsReservedBySystem()) {
    MOZ_LOG(gKeyLog, LogLevel::Info,
            ("%p   NativeKey::HandleCharMessage(), doesn't dispatch keypress "
             "event because the key combination is reserved by the system",
             this));
    return false;
  }

  // If the widget has gone, we should do nothing.
  if (mWidget->Destroyed()) {
    MOZ_LOG(gKeyLog, LogLevel::Warning,
            ("%p   NativeKey::HandleCharMessage(), WARNING, not handled due to "
             "destroyed the widget",
             this));
    return false;
  }

  // When a control key is inputted by a key, it should be handled without
  // WM_*CHAR messages at receiving WM_*KEYDOWN message.  So, when we receive
  // WM_*CHAR message directly, we see a control character here.
  // Note that when the char is '\r', it means that the char message should
  // cause "Enter" keypress event for inserting a line break.
  if (IsControlCharMessage(aCharMsg) && !IsEnterKeyPressCharMessage(aCharMsg)) {
    // In this case, we don't need to dispatch eKeyPress event because:
    // 1. We're the only browser which dispatches "keypress" event for
    //    non-printable characters (Although, both Chrome and Edge dispatch
    //    "keypress" event for some keys accidentally.  For example, "IntlRo"
    //    key with Ctrl of Japanese keyboard layout).
    // 2. Currently, we may handle shortcut keys with "keydown" event if
    //    it's reserved or something.  So, we shouldn't dispatch "keypress"
    //    event without it.
    // Note that this does NOT mean we stop dispatching eKeyPress event for
    // key presses causes a control character when Ctrl is pressed.  In such
    // case, DispatchKeyPressEventsWithoutCharMessage() dispatches eKeyPress
    // instead of this method.
    MOZ_LOG(
        gKeyLog, LogLevel::Info,
        ("%p   NativeKey::HandleCharMessage(), doesn't dispatch keypress "
         "event because received a control character input without WM_KEYDOWN",
         this));
    return false;
  }

  // XXXmnakano I think that if mMsg is WM_CHAR, i.e., it comes without
  //            preceding WM_KEYDOWN, we should should dispatch composition
  //            events instead of eKeyPress because they are not caused by
  //            actual keyboard operation.

  // First, handle normal text input or non-printable key case here.
  WidgetKeyboardEvent keypressEvent(true, eKeyPress, mWidget);
  if (IsEnterKeyPressCharMessage(aCharMsg)) {
    keypressEvent.mKeyCode = NS_VK_RETURN;
  } else {
    keypressEvent.mCharCode = static_cast<uint32_t>(aCharMsg.wParam);
  }
  nsresult rv = mDispatcher->BeginNativeInputTransaction();
  if (NS_WARN_IF(NS_FAILED(rv))) {
    MOZ_LOG(gKeyLog, LogLevel::Error,
            ("%p   NativeKey::HandleCharMessage(), FAILED due to "
             "BeginNativeInputTransaction() failure",
             this));
    return true;
  }

  MOZ_LOG(gKeyLog, LogLevel::Debug,
          ("%p   NativeKey::HandleCharMessage(), initializing keypress "
           "event...",
           this));

  ModifierKeyState modKeyState(mModKeyState);
  // When AltGr is pressed, both Alt and Ctrl are active.  However, when they
  // are active, TextEditor won't treat the keypress event as inputting a
  // character.  Therefore, when AltGr is pressed and the key tries to input
  // a character, let's set them to false.
  if (modKeyState.IsControl() && modKeyState.IsAlt() &&
      IsPrintableCharMessage(aCharMsg)) {
    modKeyState.Unset(MODIFIER_ALT | MODIFIER_CONTROL);
  }
  nsEventStatus status = InitKeyEvent(keypressEvent, modKeyState);
  MOZ_LOG(gKeyLog, LogLevel::Info,
          ("%p   NativeKey::HandleCharMessage(), dispatching keypress event...",
           this));
  bool dispatched = mDispatcher->MaybeDispatchKeypressEvents(
      keypressEvent, status, const_cast<NativeKey*>(this));
  if (aEventDispatched) {
    *aEventDispatched = dispatched;
  }
  if (mWidget->Destroyed()) {
    MOZ_LOG(gKeyLog, LogLevel::Info,
            ("%p   NativeKey::HandleCharMessage(), keypress event caused "
             "destroying the widget",
             this));
    return true;
  }
  bool consumed = status == nsEventStatus_eConsumeNoDefault;
  MOZ_LOG(gKeyLog, LogLevel::Info,
          ("%p   NativeKey::HandleCharMessage(), dispatched keypress event, "
           "dispatched=%s, consumed=%s",
           this, TrueOrFalse(dispatched), TrueOrFalse(consumed)));
  return consumed;
}

bool NativeKey::HandleKeyUpMessage(bool* aEventDispatched) const {
  MOZ_ASSERT(IsKeyUpMessage());

  if (aEventDispatched) {
    *aEventDispatched = false;
  }

  // If the key combinations is reserved by the system, we shouldn't dispatch
  // eKeyUp event for it and passes the message to next wndproc.
  if (IsReservedBySystem()) {
    MOZ_LOG(gKeyLog, LogLevel::Info,
            ("%p   NativeKey::HandleKeyUpMessage(), doesn't dispatch keyup "
             "event because the key combination is reserved by the system",
             this));
    return false;
  }

  if (sPendingHighSurrogate) {
    MOZ_LOG(gKeyLog, LogLevel::Info,
            ("%p   NativeKey::HandleKeyUpMessage(), doesn't dispatch keyup "
             "event because the key introduced only a high surrotate, so we "
             "should wait the following low surrogate input",
             this));
    return false;
  }

  // If the widget has gone, we should do nothing.
  if (mWidget->Destroyed()) {
    MOZ_LOG(
        gKeyLog, LogLevel::Warning,
        ("%p   NativeKey::HandleKeyUpMessage(), WARNING, not handled due to "
         "destroyed the widget",
         this));
    return false;
  }

  nsresult rv = mDispatcher->BeginNativeInputTransaction();
  if (NS_WARN_IF(NS_FAILED(rv))) {
    MOZ_LOG(gKeyLog, LogLevel::Error,
            ("%p   NativeKey::HandleKeyUpMessage(), FAILED due to "
             "BeginNativeInputTransaction() failure",
             this));
    return true;
  }

  MOZ_LOG(gKeyLog, LogLevel::Debug,
          ("%p   NativeKey::HandleKeyUpMessage(), initializing keyup event...",
           this));
  WidgetKeyboardEvent keyupEvent(true, eKeyUp, mWidget);
  nsEventStatus status = InitKeyEvent(keyupEvent, mModKeyState);
  MOZ_LOG(gKeyLog, LogLevel::Info,
          ("%p   NativeKey::HandleKeyUpMessage(), dispatching keyup event...",
           this));
  bool dispatched = mDispatcher->DispatchKeyboardEvent(
      eKeyUp, keyupEvent, status, const_cast<NativeKey*>(this));
  if (aEventDispatched) {
    *aEventDispatched = dispatched;
  }
  if (mWidget->Destroyed()) {
    MOZ_LOG(gKeyLog, LogLevel::Info,
            ("%p   NativeKey::HandleKeyUpMessage(), keyup event caused "
             "destroying the widget",
             this));
    return true;
  }
  bool consumed = status == nsEventStatus_eConsumeNoDefault;
  MOZ_LOG(gKeyLog, LogLevel::Info,
          ("%p   NativeKey::HandleKeyUpMessage(), dispatched keyup event, "
           "dispatched=%s, consumed=%s",
           this, TrueOrFalse(dispatched), TrueOrFalse(consumed)));
  return consumed;
}

bool NativeKey::NeedsToHandleWithoutFollowingCharMessages() const {
  MOZ_ASSERT(IsKeyDownMessage());

  // If the key combination is reserved by the system, the caller shouldn't
  // do anything with following WM_*CHAR messages.  So, let's return true here.
  if (IsReservedBySystem()) {
    return true;
  }

  // If the keydown message is generated for inputting some Unicode characters
  // via SendInput() API, we need to handle it only with WM_*CHAR messages.
  if (mVirtualKeyCode == VK_PACKET) {
    return false;
  }

  // If following char message is for a control character, it should be handled
  // without WM_CHAR message.  This is typically Ctrl + [a-z].
  if (mFollowingCharMsgs.Length() == 1 &&
      IsControlCharMessage(mFollowingCharMsgs[0])) {
    return true;
  }

  // If keydown message is followed by WM_CHAR or WM_SYSCHAR whose wParam isn't
  // a control character, we should dispatch keypress event with the char
  // message even with any modifier state.
  if (IsFollowedByPrintableCharOrSysCharMessage()) {
    return false;
  }

  // If any modifier keys which may cause printable keys becoming non-printable
  // are not pressed, we don't need special handling for the key.
  // Note that if the key does not produce a character with AltGr and when
  // AltGr key is pressed, we don't need to dispatch eKeyPress event for it
  // because AltGr shouldn't be used for a modifier for a shortcut without
  // Ctrl, Alt or Win.  That means that we should treat it in same path for
  // Shift key.
  if (!mModKeyState.IsControl() && !mModKeyState.IsAlt() &&
      !mModKeyState.IsWin()) {
    return false;
  }

  // If the key event causes dead key event, we don't need to dispatch keypress
  // event.
  if (mIsDeadKey && mCommittedCharsAndModifiers.IsEmpty()) {
    return false;
  }

  // Even if the key is a printable key, it might cause non-printable character
  // input with modifier key(s).
  return mIsPrintableKey;
}

static nsCString GetResultOfInSendMessageEx() {
  DWORD ret = ::InSendMessageEx(nullptr);
  if (!ret) {
    return "ISMEX_NOSEND"_ns;
  }
  nsCString result;
  if (ret & ISMEX_CALLBACK) {
    result = "ISMEX_CALLBACK";
  }
  if (ret & ISMEX_NOTIFY) {
    if (!result.IsEmpty()) {
      result += " | ";
    }
    result += "ISMEX_NOTIFY";
  }
  if (ret & ISMEX_REPLIED) {
    if (!result.IsEmpty()) {
      result += " | ";
    }
    result += "ISMEX_REPLIED";
  }
  if (ret & ISMEX_SEND) {
    if (!result.IsEmpty()) {
      result += " | ";
    }
    result += "ISMEX_SEND";
  }
  return result;
}

bool NativeKey::MayBeSameCharMessage(const MSG& aCharMsg1,
                                     const MSG& aCharMsg2) const {
  // NOTE: Although, we don't know when this case occurs, the scan code value
  //       in lParam may be changed from 0 to something.  The changed value
  //       is different from the scan code of handling keydown message.
  static const LPARAM kScanCodeMask = 0x00FF0000;
  return aCharMsg1.message == aCharMsg2.message &&
         aCharMsg1.wParam == aCharMsg2.wParam &&
         (aCharMsg1.lParam & ~kScanCodeMask) ==
             (aCharMsg2.lParam & ~kScanCodeMask);
}

bool NativeKey::IsSamePhysicalKeyMessage(const MSG& aKeyOrCharMsg1,
                                         const MSG& aKeyOrCharMsg2) const {
  if (NS_WARN_IF(aKeyOrCharMsg1.message < WM_KEYFIRST) ||
      NS_WARN_IF(aKeyOrCharMsg1.message > WM_KEYLAST) ||
      NS_WARN_IF(aKeyOrCharMsg2.message < WM_KEYFIRST) ||
      NS_WARN_IF(aKeyOrCharMsg2.message > WM_KEYLAST)) {
    return false;
  }
  return WinUtils::GetScanCode(aKeyOrCharMsg1.lParam) ==
             WinUtils::GetScanCode(aKeyOrCharMsg2.lParam) &&
         WinUtils::IsExtendedScanCode(aKeyOrCharMsg1.lParam) ==
             WinUtils::IsExtendedScanCode(aKeyOrCharMsg2.lParam);
}

bool NativeKey::GetFollowingCharMessage(MSG& aCharMsg) {
  MOZ_ASSERT(IsKeyDownMessage());

  aCharMsg.message = WM_NULL;

  if (mFakeCharMsgs) {
    for (size_t i = 0; i < mFakeCharMsgs->Length(); i++) {
      FakeCharMsg& fakeCharMsg = mFakeCharMsgs->ElementAt(i);
      if (fakeCharMsg.mConsumed) {
        continue;
      }
      MSG charMsg = fakeCharMsg.GetCharMsg(mMsg.hwnd);
      fakeCharMsg.mConsumed = true;
      if (!IsCharMessage(charMsg)) {
        return false;
      }
      aCharMsg = charMsg;
      return true;
    }
    return false;
  }

  // If next key message is not char message, we should give up to find a
  // related char message for the handling keydown event for now.
  // Note that it's possible other applications may send other key message
  // after we call TranslateMessage(). That may cause PeekMessage() failing
  // to get char message for the handling keydown message.
  MSG nextKeyMsg;
  if (!WinUtils::PeekMessage(&nextKeyMsg, mMsg.hwnd, WM_KEYFIRST, WM_KEYLAST,
                             PM_NOREMOVE | PM_NOYIELD) ||
      !IsCharMessage(nextKeyMsg)) {
    MOZ_LOG(gKeyLog, LogLevel::Debug,
            ("%p   NativeKey::GetFollowingCharMessage(), there are no char "
             "messages",
             this));
    return false;
  }
  const MSG kFoundCharMsg = nextKeyMsg;

  AutoRestore<MSG> saveLastRemovingMsg(mRemovingMsg);
  mRemovingMsg = nextKeyMsg;

  mReceivedMsg = sEmptyMSG;
  AutoRestore<MSG> ensureToClearRecivedMsg(mReceivedMsg);

  // On Metrofox, PeekMessage() sometimes returns WM_NULL even if we specify
  // the message range.  So, if it returns WM_NULL, we should retry to get
  // the following char message it was found above.
  for (uint32_t i = 0; i < 50; i++) {
    MSG removedMsg, nextKeyMsgInAllWindows;
    bool doCrash = false;
    if (!WinUtils::PeekMessage(&removedMsg, mMsg.hwnd, nextKeyMsg.message,
                               nextKeyMsg.message, PM_REMOVE | PM_NOYIELD)) {
      // We meets unexpected case.  We should collect the message queue state
      // and crash for reporting the bug.
      doCrash = true;

      // If another instance was created for the removing message during trying
      // to remove a char message, the instance didn't handle it for preventing
      // recursive handling.  So, let's handle it in this instance.
      if (!IsEmptyMSG(mReceivedMsg)) {
        // If focus is moved to different window, we shouldn't handle it on
        // the widget.  Let's discard it for now.
        if (mReceivedMsg.hwnd != nextKeyMsg.hwnd) {
          MOZ_LOG(
              gKeyLog, LogLevel::Warning,
              ("%p   NativeKey::GetFollowingCharMessage(), WARNING, received a "
               "char message during removing it from the queue, but it's for "
               "different window, mReceivedMsg=%s, nextKeyMsg=%s, "
               "kFoundCharMsg=%s",
               this, ToString(mReceivedMsg).get(), ToString(nextKeyMsg).get(),
               ToString(kFoundCharMsg).get()));
          // There might still exist char messages, the loop of calling
          // this method should be continued.
          aCharMsg.message = WM_NULL;
          return true;
        }
        // Even if the received message is different from what we tried to
        // remove from the queue, let's take the received message as a part of
        // the result of this key sequence.
        if (mReceivedMsg.message != nextKeyMsg.message ||
            mReceivedMsg.wParam != nextKeyMsg.wParam ||
            mReceivedMsg.lParam != nextKeyMsg.lParam) {
          MOZ_LOG(
              gKeyLog, LogLevel::Warning,
              ("%p   NativeKey::GetFollowingCharMessage(), WARNING, received a "
               "char message during removing it from the queue, but it's "
               "differnt from what trying to remove from the queue, "
               "aCharMsg=%s, nextKeyMsg=%s, kFoundCharMsg=%s",
               this, ToString(mReceivedMsg).get(), ToString(nextKeyMsg).get(),
               ToString(kFoundCharMsg).get()));
        } else {
          MOZ_LOG(
              gKeyLog, LogLevel::Debug,
              ("%p   NativeKey::GetFollowingCharMessage(), succeeded to "
               "retrieve next char message via another instance, aCharMsg=%s, "
               "kFoundCharMsg=%s",
               this, ToString(mReceivedMsg).get(),
               ToString(kFoundCharMsg).get()));
        }
        aCharMsg = mReceivedMsg;
        return true;
      }

      // The char message is redirected to different thread's window by focus
      // move or something or just cancelled by external application.
      if (!WinUtils::PeekMessage(&nextKeyMsgInAllWindows, 0, WM_KEYFIRST,
                                 WM_KEYLAST, PM_NOREMOVE | PM_NOYIELD)) {
        MOZ_LOG(
            gKeyLog, LogLevel::Warning,
            ("%p   NativeKey::GetFollowingCharMessage(), WARNING, failed to "
             "remove a char message, but it's already gone from all message "
             "queues, nextKeyMsg=%s, kFoundCharMsg=%s",
             this, ToString(nextKeyMsg).get(), ToString(kFoundCharMsg).get()));
        return true;  // XXX should return false in this case
      }
      // The next key message is redirected to different window created by our
      // thread, we should do nothing because we must not have focus.
      if (nextKeyMsgInAllWindows.hwnd != mMsg.hwnd) {
        aCharMsg = nextKeyMsgInAllWindows;
        MOZ_LOG(
            gKeyLog, LogLevel::Warning,
            ("%p   NativeKey::GetFollowingCharMessage(), WARNING, failed to "
             "remove a char message, but found in another message queue, "
             "nextKeyMsgInAllWindows=%s, nextKeyMsg=%s, kFoundCharMsg=%s",
             this, ToString(nextKeyMsgInAllWindows).get(),
             ToString(nextKeyMsg).get(), ToString(kFoundCharMsg).get()));
        return true;
      }
      // If next key message becomes non-char message, this key operation
      // may have already been consumed or canceled.
      if (!IsCharMessage(nextKeyMsgInAllWindows)) {
        MOZ_LOG(
            gKeyLog, LogLevel::Warning,
            ("%p   NativeKey::GetFollowingCharMessage(), WARNING, failed to "
             "remove a char message and next key message becomes non-char "
             "message, nextKeyMsgInAllWindows=%s, nextKeyMsg=%s, "
             "kFoundCharMsg=%s",
             this, ToString(nextKeyMsgInAllWindows).get(),
             ToString(nextKeyMsg).get(), ToString(kFoundCharMsg).get()));
        MOZ_ASSERT(!mCharMessageHasGone);
        mFollowingCharMsgs.Clear();
        mCharMessageHasGone = true;
        return false;
      }
      // If next key message is still a char message but different key message,
      // we should treat current key operation is consumed or canceled and
      // next char message should be handled as an orphan char message later.
      if (!IsSamePhysicalKeyMessage(nextKeyMsgInAllWindows, kFoundCharMsg)) {
        MOZ_LOG(
            gKeyLog, LogLevel::Warning,
            ("%p   NativeKey::GetFollowingCharMessage(), WARNING, failed to "
             "remove a char message and next key message becomes differnt "
             "key's char message, nextKeyMsgInAllWindows=%s, nextKeyMsg=%s, "
             "kFoundCharMsg=%s",
             this, ToString(nextKeyMsgInAllWindows).get(),
             ToString(nextKeyMsg).get(), ToString(kFoundCharMsg).get()));
        MOZ_ASSERT(!mCharMessageHasGone);
        mFollowingCharMsgs.Clear();
        mCharMessageHasGone = true;
        return false;
      }
      // If next key message is still a char message but the message is changed,
      // we should retry to remove the new message with PeekMessage() again.
      if (nextKeyMsgInAllWindows.message != nextKeyMsg.message) {
        MOZ_LOG(
            gKeyLog, LogLevel::Warning,
            ("%p   NativeKey::GetFollowingCharMessage(), WARNING, failed to "
             "remove a char message due to message change, let's retry to "
             "remove the message with newly found char message, "
             "nextKeyMsgInAllWindows=%s, nextKeyMsg=%s, kFoundCharMsg=%s",
             this, ToString(nextKeyMsgInAllWindows).get(),
             ToString(nextKeyMsg).get(), ToString(kFoundCharMsg).get()));
        nextKeyMsg = nextKeyMsgInAllWindows;
        continue;
      }
      // If there is still existing a char message caused by same physical key
      // in the queue but PeekMessage(PM_REMOVE) failed to remove it from the
      // queue, it might be possible that the odd keyboard layout or utility
      // hooks only PeekMessage(PM_NOREMOVE) and GetMessage().  So, let's try
      // remove the char message with GetMessage() again.
      // FYI: The wParam might be different from the found message, but it's
      //      okay because we assume that odd keyboard layouts return actual
      //      inputting character at removing the char message.
      if (WinUtils::GetMessage(&removedMsg, mMsg.hwnd, nextKeyMsg.message,
                               nextKeyMsg.message)) {
        MOZ_LOG(
            gKeyLog, LogLevel::Warning,
            ("%p   NativeKey::GetFollowingCharMessage(), WARNING, failed to "
             "remove a char message, but succeeded with GetMessage(), "
             "removedMsg=%s, kFoundCharMsg=%s",
             this, ToString(removedMsg).get(), ToString(kFoundCharMsg).get()));
        // Cancel to crash, but we need to check the removed message value.
        doCrash = false;
      }
      // If we've already removed some WM_NULL messages from the queue and
      // the found message has already gone from the queue, let's treat the key
      // as inputting no characters and already consumed.
      else if (i > 0) {
        MOZ_LOG(
            gKeyLog, LogLevel::Warning,
            ("%p   NativeKey::GetFollowingCharMessage(), WARNING, failed to "
             "remove a char message, but removed %d WM_NULL messages",
             this, i));
        // If the key is a printable key or a control key but tried to input
        // a character, mark mCharMessageHasGone true for handling the keydown
        // event as inputting empty string.
        MOZ_ASSERT(!mCharMessageHasGone);
        mFollowingCharMsgs.Clear();
        mCharMessageHasGone = true;
        return false;
      }
      MOZ_LOG(gKeyLog, LogLevel::Error,
              ("%p   NativeKey::GetFollowingCharMessage(), FAILED, lost target "
               "message to remove, nextKeyMsg=%s",
               this, ToString(nextKeyMsg).get()));
    }

    if (doCrash) {
      nsPrintfCString info(
          "\nPeekMessage() failed to remove char message! "
          "\nActive keyboard layout=0x%p (%s), "
          "\nHandling message: %s, InSendMessageEx()=%s, "
          "\nFound message: %s, "
          "\nWM_NULL has been removed: %d, "
          "\nNext key message in all windows: %s, "
          "time=%ld, ",
          KeyboardLayout::GetInstance()->GetLoadedLayout(),
          KeyboardLayout::GetInstance()->GetLoadedLayoutName().get(),
          ToString(mMsg).get(), GetResultOfInSendMessageEx().get(),
          ToString(kFoundCharMsg).get(), i,
          ToString(nextKeyMsgInAllWindows).get(), nextKeyMsgInAllWindows.time);
      CrashReporter::AppendAppNotesToCrashReport(info);
      MSG nextMsg;
      if (WinUtils::PeekMessage(&nextMsg, 0, 0, 0, PM_NOREMOVE | PM_NOYIELD)) {
        nsPrintfCString info("\nNext message in all windows: %s, time=%ld",
                             ToString(nextMsg).get(), nextMsg.time);
        CrashReporter::AppendAppNotesToCrashReport(info);
      } else {
        CrashReporter::AppendAppNotesToCrashReport(
            "\nThere is no message in any window"_ns);
      }

      MOZ_CRASH("We lost the following char message");
    }

    // We're still not sure why ::PeekMessage() may return WM_NULL even with
    // its first message and its last message are same message.  However,
    // at developing Metrofox, we met this case even with usual keyboard
    // layouts.  So, it might be possible in desktop application or it really
    // occurs with some odd keyboard layouts which perhaps hook API.
    if (removedMsg.message == WM_NULL) {
      MOZ_LOG(gKeyLog, LogLevel::Warning,
              ("%p   NativeKey::GetFollowingCharMessage(), WARNING, failed to "
               "remove a char message, instead, removed WM_NULL message, "
               "removedMsg=%s",
               this, ToString(removedMsg).get()));
      // Check if there is the message which we're trying to remove.
      MSG newNextKeyMsg;
      if (!WinUtils::PeekMessage(&newNextKeyMsg, mMsg.hwnd, WM_KEYFIRST,
                                 WM_KEYLAST, PM_NOREMOVE | PM_NOYIELD)) {
        // If there is no key message, we should mark this keydown as consumed
        // because the key operation may have already been handled or canceled.
        MOZ_LOG(
            gKeyLog, LogLevel::Warning,
            ("%p   NativeKey::GetFollowingCharMessage(), WARNING, failed to "
             "remove a char message because it's gone during removing it from "
             "the queue, nextKeyMsg=%s, kFoundCharMsg=%s",
             this, ToString(nextKeyMsg).get(), ToString(kFoundCharMsg).get()));
        MOZ_ASSERT(!mCharMessageHasGone);
        mFollowingCharMsgs.Clear();
        mCharMessageHasGone = true;
        return false;
      }
      if (!IsCharMessage(newNextKeyMsg)) {
        // If next key message becomes a non-char message, we should mark this
        // keydown as consumed because the key operation may have already been
        // handled or canceled.
        MOZ_LOG(
            gKeyLog, LogLevel::Warning,
            ("%p   NativeKey::GetFollowingCharMessage(), WARNING, failed to "
             "remove a char message because it's gone during removing it from "
             "the queue, nextKeyMsg=%s, newNextKeyMsg=%s, kFoundCharMsg=%s",
             this, ToString(nextKeyMsg).get(), ToString(newNextKeyMsg).get(),
             ToString(kFoundCharMsg).get()));
        MOZ_ASSERT(!mCharMessageHasGone);
        mFollowingCharMsgs.Clear();
        mCharMessageHasGone = true;
        return false;
      }
      MOZ_LOG(
          gKeyLog, LogLevel::Debug,
          ("%p   NativeKey::GetFollowingCharMessage(), there is the message "
           "which is being tried to be removed from the queue, trying again...",
           this));
      continue;
    }

    // wParam of WM_DEADCHAR may be 0, but for the other char messages, this is
    // an odd case because it means that the key event shouldn't cause text
    // input.  So, let's ignore the strange char message.
    if (removedMsg.message != WM_DEADCHAR &&
        removedMsg.message == nextKeyMsg.message && !removedMsg.wParam) {
      MOZ_LOG(
          gKeyLog, LogLevel::Warning,
          ("%p   NativeKey::GetFollowingCharMessage(), WARNING, succeeded to "
           "remove a char message, but the removed message's wParam is 0, "
           "removedMsg=%s",
           this, ToString(removedMsg).get()));
      return false;
    }

    // This is normal case.
    if (MayBeSameCharMessage(removedMsg, nextKeyMsg)) {
      aCharMsg = removedMsg;
      MOZ_LOG(
          gKeyLog, LogLevel::Debug,
          ("%p   NativeKey::GetFollowingCharMessage(), succeeded to retrieve "
           "next char message, aCharMsg=%s",
           this, ToString(aCharMsg).get()));
      return true;
    }

    // Even if removed message is different char message from the found char
    // message, when the scan code is same, we can assume that the message
    // is overwritten by somebody who hooks API.  See bug 1336028 comment 0 for
    // the possible scenarios.
    if (IsCharMessage(removedMsg) &&
        IsSamePhysicalKeyMessage(removedMsg, nextKeyMsg)) {
      aCharMsg = removedMsg;
      MOZ_LOG(
          gKeyLog, LogLevel::Warning,
          ("%p   NativeKey::GetFollowingCharMessage(), WARNING, succeeded to "
           "remove a char message, but the removed message was changed from "
           "the found message except their scancode, aCharMsg=%s, "
           "nextKeyMsg=%s, kFoundCharMsg=%s",
           this, ToString(aCharMsg).get(), ToString(nextKeyMsg).get(),
           ToString(kFoundCharMsg).get()));
      return true;
    }

    // When found message's wParam is 0 and its scancode is 0xFF, we may remove
    // usual char message actually.  In such case, we should use the removed
    // char message.
    if (IsCharMessage(removedMsg) && !nextKeyMsg.wParam &&
        WinUtils::GetScanCode(nextKeyMsg.lParam) == 0xFF) {
      aCharMsg = removedMsg;
      MOZ_LOG(
          gKeyLog, LogLevel::Warning,
          ("%p   NativeKey::GetFollowingCharMessage(), WARNING, succeeded to "
           "remove a char message, but the removed message was changed from "
           "the found message but the found message was odd, so, ignoring the "
           "odd found message and respecting the removed message, aCharMsg=%s, "
           "nextKeyMsg=%s, kFoundCharMsg=%s",
           this, ToString(aCharMsg).get(), ToString(nextKeyMsg).get(),
           ToString(kFoundCharMsg).get()));
      return true;
    }

    const auto nextKeyMsgAfter = [&]() -> Maybe<MSG> {
      MSG nextKeyMsgAfter;
      if (WinUtils::PeekMessage(&nextKeyMsgAfter, mMsg.hwnd, WM_KEYFIRST,
                                WM_KEYLAST, PM_NOREMOVE | PM_NOYIELD)) {
        return Some(nextKeyMsgAfter);
      }
      return Nothing();
    }();

    // We have some crash reports for Ethiopic Syllable.  In the case, we found
    // a WM_DEADCHAR whose wParam may be not NULL, first.  Then, we remove the
    // message from the queue.  However, the keyboard or IME makes us to remove
    // a dummy message whose wParam (inputting character) is NULL and scancode
    // is 0. Finally, the found message keeps staying in the queue.  The crash
    // report comes after we started accepting WM_DEADCHAR with NULL character
    // above. However, we haven't handled only a part of dead key sequence but
    // the user tried to input Ethiopic Syllable.  Therefore, it's hard to guess
    // that the user starts using Firefox with the keyboard/IME at the timing
    // and the the user keeps using Firefox with unfunctional keyboard/IME.
    // Perhaps the keyboard/IME does something different hack for us to make
    // itself available on Firefox.  Therefore, we should just return `false` to
    // make the caller give up removing the following messages.
    if (IsDeadCharMessage(removedMsg.message) && !removedMsg.wParam &&
        !WinUtils::GetScanCode(removedMsg.lParam) && nextKeyMsgAfter.isSome() &&
        MayBeSameCharMessage(nextKeyMsgAfter.ref(), nextKeyMsg)) {
      MOZ_LOG(
          gKeyLog, LogLevel::Warning,
          ("%p   NativeKey::GetFollowingCharMessage(), WARNING, succeeded to "
           "remove a dead char message, but the removed message's wParam is 0 "
           "and the found message still in the queue, nextKeyMsg=%s but "
           "removedMsg=%s",
           this, ToString(nextKeyMsg).get(), ToString(removedMsg).get()));
      return false;
    }

    // NOTE: Although, we don't know when this case occurs, the scan code value
    //       in lParam may be changed from 0 to something.  The changed value
    //       is different from the scan code of handling keydown message.
    MOZ_LOG(
        gKeyLog, LogLevel::Error,
        ("%p   NativeKey::GetFollowingCharMessage(), FAILED, removed message "
         "is really different from what we have already found, removedMsg=%s, "
         "nextKeyMsg=%s, kFoundCharMsg=%s",
         this, ToString(removedMsg).get(), ToString(nextKeyMsg).get(),
         ToString(kFoundCharMsg).get()));
    nsPrintfCString info(
        "\nPeekMessage() removed unexpcted char message! "
        "\nActive keyboard layout=0x%p (%s), "
        "\nHandling message: %s, InSendMessageEx()=%s, "
        "\nFound message: %s, "
        "\nRemoved message: %s, ",
        KeyboardLayout::GetInstance()->GetLoadedLayout(),
        KeyboardLayout::GetInstance()->GetLoadedLayoutName().get(),
        ToString(mMsg).get(), GetResultOfInSendMessageEx().get(),
        ToString(kFoundCharMsg).get(), ToString(removedMsg).get());
    CrashReporter::AppendAppNotesToCrashReport(info);
    // What's the next key message?
    if (nextKeyMsgAfter.isSome()) {
      nsPrintfCString info(
          "\nNext key message after unexpected char message "
          "removed: %s, ",
          ToString(nextKeyMsgAfter.ref()).get());
      CrashReporter::AppendAppNotesToCrashReport(info);
    } else {
      CrashReporter::AppendAppNotesToCrashReport(
          nsLiteralCString("\nThere is no key message after unexpected char "
                           "message removed, "));
    }
    // Another window has a key message?
    if (WinUtils::PeekMessage(&nextKeyMsgInAllWindows, 0, WM_KEYFIRST,
                              WM_KEYLAST, PM_NOREMOVE | PM_NOYIELD)) {
      nsPrintfCString info("\nNext key message in all windows: %s.",
                           ToString(nextKeyMsgInAllWindows).get());
      CrashReporter::AppendAppNotesToCrashReport(info);
    } else {
      CrashReporter::AppendAppNotesToCrashReport(
          "\nThere is no key message in any windows."_ns);
    }

    MOZ_CRASH("PeekMessage() removed unexpected message");
  }
  MOZ_LOG(
      gKeyLog, LogLevel::Error,
      ("%p   NativeKey::GetFollowingCharMessage(), FAILED, removed messages "
       "are all WM_NULL, nextKeyMsg=%s",
       this, ToString(nextKeyMsg).get()));
  nsPrintfCString info(
      "\nWe lost following char message! "
      "\nActive keyboard layout=0x%p (%s), "
      "\nHandling message: %s, InSendMessageEx()=%s, \n"
      "Found message: %s, removed a lot of WM_NULL",
      KeyboardLayout::GetInstance()->GetLoadedLayout(),
      KeyboardLayout::GetInstance()->GetLoadedLayoutName().get(),
      ToString(mMsg).get(), GetResultOfInSendMessageEx().get(),
      ToString(kFoundCharMsg).get());
  CrashReporter::AppendAppNotesToCrashReport(info);
  MOZ_CRASH("We lost the following char message");
  return false;
}

void NativeKey::ComputeInputtingStringWithKeyboardLayout() {
  KeyboardLayout* keyboardLayout = KeyboardLayout::GetInstance();

  if (KeyboardLayout::IsPrintableCharKey(mVirtualKeyCode) ||
      mCharMessageHasGone) {
    mInputtingStringAndModifiers = mCommittedCharsAndModifiers;
  } else {
    mInputtingStringAndModifiers.Clear();
  }
  mShiftedString.Clear();
  mUnshiftedString.Clear();
  mShiftedLatinChar = mUnshiftedLatinChar = 0;

  if (!mModKeyState.IsControl() && !mModKeyState.IsAlt() &&
      !mModKeyState.IsWin()) {
    return;
  }

  // If user is inputting a Unicode character with typing Alt + Numpad
  // keys, we shouldn't set alternative key codes because the key event
  // shouldn't match with a mnemonic.  However, we should set only
  // mUnshiftedString for keeping traditional behavior at WM_SYSKEYDOWN.
  // FYI: I guess that it's okay that mUnshiftedString stays empty.  So,
  //      if its value breaks something, you must be able to just return here.
  if (MaybeTypingUnicodeScalarValue()) {
    if (!mCommittedCharsAndModifiers.IsEmpty()) {
      MOZ_ASSERT(mMsg.message == WM_SYSKEYDOWN);
      char16_t num = mCommittedCharsAndModifiers.CharAt(0);
      MOZ_ASSERT(num >= '0' && num <= '9');
      mUnshiftedString.Append(num, MODIFIER_NONE);
      return;
    }
    // If user presses a function key without NumLock or 3rd party utility
    // synthesizes a function key on numpad, we should handle it as-is because
    // the user's intention may be performing `Alt` + foo.
    MOZ_ASSERT(!KeyboardLayout::IsPrintableCharKey(mVirtualKeyCode));
    return;
  }

  ModifierKeyState capsLockState(mModKeyState.GetModifiers() &
                                 MODIFIER_CAPSLOCK);

  mUnshiftedString =
      keyboardLayout->GetUniCharsAndModifiers(mVirtualKeyCode, capsLockState);
  capsLockState.Set(MODIFIER_SHIFT);
  mShiftedString =
      keyboardLayout->GetUniCharsAndModifiers(mVirtualKeyCode, capsLockState);

  // The current keyboard cannot input alphabets or numerics,
  // we should append them for Shortcut/Access keys.
  // E.g., for Cyrillic keyboard layout.
  capsLockState.Unset(MODIFIER_SHIFT);
  WidgetUtils::GetLatinCharCodeForKeyCode(
      mDOMKeyCode, capsLockState.GetModifiers(), &mUnshiftedLatinChar,
      &mShiftedLatinChar);

  // If the mShiftedLatinChar isn't 0, the key code is NS_VK_[A-Z].
  if (mShiftedLatinChar) {
    // If the produced characters of the key on current keyboard layout
    // are same as computed Latin characters, we shouldn't append the
    // Latin characters to alternativeCharCode.
    if (mUnshiftedLatinChar == mUnshiftedString.CharAt(0) &&
        mShiftedLatinChar == mShiftedString.CharAt(0)) {
      mShiftedLatinChar = mUnshiftedLatinChar = 0;
    }
  } else if (mUnshiftedLatinChar) {
    // If the mShiftedLatinChar is 0, the mKeyCode doesn't produce
    // alphabet character.  At that time, the character may be produced
    // with Shift key.  E.g., on French keyboard layout, NS_VK_PERCENT
    // key produces LATIN SMALL LETTER U WITH GRAVE (U+00F9) without
    // Shift key but with Shift key, it produces '%'.
    // If the mUnshiftedLatinChar is produced by the key on current
    // keyboard layout, we shouldn't append it to alternativeCharCode.
    if (mUnshiftedLatinChar == mUnshiftedString.CharAt(0) ||
        mUnshiftedLatinChar == mShiftedString.CharAt(0)) {
      mUnshiftedLatinChar = 0;
    }
  }

  if (!mModKeyState.IsControl()) {
    return;
  }

  // If the mCharCode is not ASCII character, we should replace the
  // mCharCode with ASCII character only when Ctrl is pressed.
  // But don't replace the mCharCode when the mCharCode is not same as
  // unmodified characters. In such case, Ctrl is sometimes used for a
  // part of character inputting key combination like Shift.
  uint32_t ch =
      mModKeyState.IsShift() ? mShiftedLatinChar : mUnshiftedLatinChar;
  if (!ch) {
    return;
  }
  if (mInputtingStringAndModifiers.IsEmpty() ||
      mInputtingStringAndModifiers.UniCharsCaseInsensitiveEqual(
          mModKeyState.IsShift() ? mShiftedString : mUnshiftedString)) {
    mInputtingStringAndModifiers.Clear();
    mInputtingStringAndModifiers.Append(ch, mModKeyState.GetModifiers());
  }
}

bool NativeKey::DispatchKeyPressEventsWithRetrievedCharMessages() const {
  MOZ_ASSERT(IsKeyDownMessage());
  MOZ_ASSERT(IsFollowedByPrintableCharOrSysCharMessage());
  MOZ_ASSERT(!mWidget->Destroyed());

  nsresult rv = mDispatcher->BeginNativeInputTransaction();
  if (NS_WARN_IF(NS_FAILED(rv))) {
    MOZ_LOG(
        gKeyLog, LogLevel::Error,
        ("%p   NativeKey::DispatchKeyPressEventsWithRetrievedCharMessages(), "
         "FAILED due to BeginNativeInputTransaction() failure",
         this));
    return true;
  }
  WidgetKeyboardEvent keypressEvent(true, eKeyPress, mWidget);
  MOZ_LOG(gKeyLog, LogLevel::Debug,
          ("%p   NativeKey::DispatchKeyPressEventsWithRetrievedCharMessages(), "
           "initializing keypress event...",
           this));
  ModifierKeyState modKeyState(mModKeyState);
  if (mCanIgnoreModifierStateAtKeyPress && IsFollowedByPrintableCharMessage()) {
    // If eKeyPress event should cause inputting text in focused editor,
    // we need to remove Alt and Ctrl and Meta state.
    modKeyState.Unset(MODIFIER_ALT | MODIFIER_CONTROL | MODIFIER_META);
  }
  // We don't need to send char message here if there are two or more retrieved
  // messages because we need to set each message to each eKeyPress event.
  bool needsCallback = mFollowingCharMsgs.Length() > 1;
  nsEventStatus status = InitKeyEvent(keypressEvent, modKeyState);
  MOZ_LOG(gKeyLog, LogLevel::Info,
          ("%p   NativeKey::DispatchKeyPressEventsWithRetrievedCharMessages(), "
           "dispatching keypress event(s)...",
           this));
  bool dispatched = mDispatcher->MaybeDispatchKeypressEvents(
      keypressEvent, status, const_cast<NativeKey*>(this), needsCallback);
  if (mWidget->Destroyed()) {
    MOZ_LOG(
        gKeyLog, LogLevel::Info,
        ("%p   NativeKey::DispatchKeyPressEventsWithRetrievedCharMessages(), "
         "keypress event(s) caused destroying the widget",
         this));
    return true;
  }
  bool consumed = status == nsEventStatus_eConsumeNoDefault;
  MOZ_LOG(gKeyLog, LogLevel::Info,
          ("%p   NativeKey::DispatchKeyPressEventsWithRetrievedCharMessages(), "
           "dispatched keypress event(s), dispatched=%s, consumed=%s",
           this, TrueOrFalse(dispatched), TrueOrFalse(consumed)));
  return consumed;
}

bool NativeKey::DispatchKeyPressEventsWithoutCharMessage() const {
  MOZ_ASSERT(IsKeyDownMessage());
  MOZ_ASSERT(!mIsDeadKey || !mCommittedCharsAndModifiers.IsEmpty());
  MOZ_ASSERT(!mWidget->Destroyed());

  nsresult rv = mDispatcher->BeginNativeInputTransaction();
  if (NS_WARN_IF(NS_FAILED(rv))) {
    MOZ_LOG(gKeyLog, LogLevel::Error,
            ("%p   NativeKey::DispatchKeyPressEventsWithoutCharMessage(), "
             "FAILED due "
             "to BeginNativeInputTransaction() failure",
             this));
    return true;
  }

  WidgetKeyboardEvent keypressEvent(true, eKeyPress, mWidget);
  if (mInputtingStringAndModifiers.IsEmpty() && mShiftedString.IsEmpty() &&
      mUnshiftedString.IsEmpty()) {
    keypressEvent.mKeyCode = mDOMKeyCode;
  }
  MOZ_LOG(gKeyLog, LogLevel::Debug,
          ("%p   NativeKey::DispatchKeyPressEventsWithoutCharMessage(), "
           "initializing "
           "keypress event...",
           this));
  nsEventStatus status = InitKeyEvent(keypressEvent, mModKeyState);
  MOZ_LOG(gKeyLog, LogLevel::Info,
          ("%p   NativeKey::DispatchKeyPressEventsWithoutCharMessage(), "
           "dispatching "
           "keypress event(s)...",
           this));
  bool dispatched = mDispatcher->MaybeDispatchKeypressEvents(
      keypressEvent, status, const_cast<NativeKey*>(this));
  if (mWidget->Destroyed()) {
    MOZ_LOG(gKeyLog, LogLevel::Info,
            ("%p   NativeKey::DispatchKeyPressEventsWithoutCharMessage(), "
             "keypress event(s) caused destroying the widget",
             this));
    return true;
  }
  bool consumed = status == nsEventStatus_eConsumeNoDefault;
  MOZ_LOG(
      gKeyLog, LogLevel::Info,
      ("%p   NativeKey::DispatchKeyPressEventsWithoutCharMessage(), dispatched "
       "keypress event(s), dispatched=%s, consumed=%s",
       this, TrueOrFalse(dispatched), TrueOrFalse(consumed)));
  return consumed;
}

void NativeKey::WillDispatchKeyboardEvent(WidgetKeyboardEvent& aKeyboardEvent,
                                          uint32_t aIndex) {
  // If it's an eKeyPress event and it's generated from retrieved char message,
  // we need to set raw message information for plugins.
  if (aKeyboardEvent.mMessage == eKeyPress &&
      IsFollowedByPrintableCharOrSysCharMessage()) {
    MOZ_RELEASE_ASSERT(aIndex < mCommittedCharsAndModifiers.Length());
    uint32_t foundPrintableCharMessages = 0;
    for (size_t i = 0; i < mFollowingCharMsgs.Length(); ++i) {
      if (!IsPrintableCharOrSysCharMessage(mFollowingCharMsgs[i])) {
        // XXX Should we dispatch a plugin event for WM_*DEADCHAR messages and
        //     WM_CHAR with a control character here?  But we're not sure
        //     how can we create such message queue (i.e., WM_CHAR or
        //     WM_SYSCHAR with a printable character and such message are
        //     generated by a keydown).  So, let's ignore such case until
        //     we'd get some bug reports.
        MOZ_LOG(gKeyLog, LogLevel::Warning,
                ("%p   NativeKey::WillDispatchKeyboardEvent(), WARNING, "
                 "ignoring %zuth message due to non-printable char message, %s",
                 this, i + 1, ToString(mFollowingCharMsgs[i]).get()));
        continue;
      }
      if (foundPrintableCharMessages++ == aIndex) {
        // Found message which caused the eKeyPress event.
        break;
      }
    }
    // Set modifier state from mCommittedCharsAndModifiers because some of them
    // might be different.  For example, Shift key was pressed at inputting
    // dead char but Shift key was released before inputting next character.
    if (mCanIgnoreModifierStateAtKeyPress) {
      ModifierKeyState modKeyState(mModKeyState);
      modKeyState.Unset(MODIFIER_SHIFT | MODIFIER_CONTROL | MODIFIER_ALT |
                        MODIFIER_ALTGRAPH | MODIFIER_CAPSLOCK);
      modKeyState.Set(mCommittedCharsAndModifiers.ModifiersAt(aIndex));
      modKeyState.InitInputEvent(aKeyboardEvent);
      MOZ_LOG(gKeyLog, LogLevel::Info,
              ("%p   NativeKey::WillDispatchKeyboardEvent(), "
               "setting %uth modifier state to %s",
               this, aIndex + 1, ToString(modKeyState).get()));
    }
  }
  size_t longestLength =
      std::max(mInputtingStringAndModifiers.Length(),
               std::max(mShiftedString.Length(), mUnshiftedString.Length()));
  size_t skipUniChars = longestLength - mInputtingStringAndModifiers.Length();
  size_t skipShiftedChars = longestLength - mShiftedString.Length();
  size_t skipUnshiftedChars = longestLength - mUnshiftedString.Length();
  if (aIndex >= longestLength) {
    MOZ_LOG(
        gKeyLog, LogLevel::Info,
        ("%p   NativeKey::WillDispatchKeyboardEvent(), does nothing for %uth "
         "%s event",
         this, aIndex + 1, ToChar(aKeyboardEvent.mMessage)));
    return;
  }

  // Check if aKeyboardEvent is the last event for a key press.
  // So, if it's not an eKeyPress event, it's always the last event.
  // Otherwise, check if the index is the last character of
  // mCommittedCharsAndModifiers.
  bool isLastIndex = aKeyboardEvent.mMessage != eKeyPress ||
                     mCommittedCharsAndModifiers.IsEmpty() ||
                     mCommittedCharsAndModifiers.Length() - 1 == aIndex;

  nsTArray<AlternativeCharCode>& altArray =
      aKeyboardEvent.mAlternativeCharCodes;

  // Set charCode and adjust modifier state for every eKeyPress event.
  // This is not necessary for the other keyboard events because the other
  // keyboard events shouldn't have non-zero charCode value and should have
  // current modifier state.
  if (aKeyboardEvent.mMessage == eKeyPress && skipUniChars <= aIndex) {
    // XXX Modifying modifier state of aKeyboardEvent is illegal, but no way
    //     to set different modifier state per keypress event except this
    //     hack.  Note that ideally, dead key should cause composition events
    //     instead of keypress events, though.
    if (aIndex - skipUniChars < mInputtingStringAndModifiers.Length()) {
      ModifierKeyState modKeyState(mModKeyState);
      // If key in combination with Alt and/or Ctrl produces a different
      // character than without them then do not report these flags
      // because it is separate keyboard layout shift state. If dead-key
      // and base character does not produce a valid composite character
      // then both produced dead-key character and following base
      // character may have different modifier flags, too.
      modKeyState.Unset(MODIFIER_SHIFT | MODIFIER_CONTROL | MODIFIER_ALT |
                        MODIFIER_ALTGRAPH | MODIFIER_CAPSLOCK);
      modKeyState.Set(
          mInputtingStringAndModifiers.ModifiersAt(aIndex - skipUniChars));
      modKeyState.InitInputEvent(aKeyboardEvent);
      MOZ_LOG(gKeyLog, LogLevel::Info,
              ("%p   NativeKey::WillDispatchKeyboardEvent(), "
               "setting %uth modifier state to %s",
               this, aIndex + 1, ToString(modKeyState).get()));
    }
    uint16_t uniChar =
        mInputtingStringAndModifiers.CharAt(aIndex - skipUniChars);

    // The mCharCode was set from mKeyValue. However, for example, when Ctrl key
    // is pressed, its value should indicate an ASCII character for backward
    // compatibility rather than inputting character without the modifiers.
    // Therefore, we need to modify mCharCode value here.
    aKeyboardEvent.SetCharCode(uniChar);
    MOZ_LOG(gKeyLog, LogLevel::Info,
            ("%p   NativeKey::WillDispatchKeyboardEvent(), "
             "setting %uth charCode to %s",
             this, aIndex + 1, GetCharacterCodeName(uniChar).get()));
  }

  // We need to append alterntaive charCode values:
  //   - if the event is eKeyPress, we need to append for the index because
  //     eKeyPress event is dispatched for every character inputted by a
  //     key press.
  //   - if the event is not eKeyPress, we need to append for all characters
  //     inputted by the key press because the other keyboard events (e.g.,
  //     eKeyDown are eKeyUp) are fired only once for a key press.
  size_t count;
  if (aKeyboardEvent.mMessage == eKeyPress) {
    // Basically, append alternative charCode values only for the index.
    count = 1;
    // However, if it's the last eKeyPress event but different shift state
    // can input longer string, the last eKeyPress event should have all
    // remaining alternative charCode values.
    if (isLastIndex) {
      count = longestLength - aIndex;
    }
  } else {
    count = longestLength;
  }
  for (size_t i = 0; i < count; ++i) {
    uint16_t shiftedChar = 0, unshiftedChar = 0;
    if (skipShiftedChars <= aIndex + i) {
      shiftedChar = mShiftedString.CharAt(aIndex + i - skipShiftedChars);
    }
    if (skipUnshiftedChars <= aIndex + i) {
      unshiftedChar = mUnshiftedString.CharAt(aIndex + i - skipUnshiftedChars);
    }

    if (shiftedChar || unshiftedChar) {
      AlternativeCharCode chars(unshiftedChar, shiftedChar);
      altArray.AppendElement(chars);
    }

    if (!isLastIndex) {
      continue;
    }

    if (mUnshiftedLatinChar || mShiftedLatinChar) {
      AlternativeCharCode chars(mUnshiftedLatinChar, mShiftedLatinChar);
      altArray.AppendElement(chars);
    }

    // Typically, following virtual keycodes are used for a key which can
    // input the character.  However, these keycodes are also used for
    // other keys on some keyboard layout.  E.g., in spite of Shift+'1'
    // inputs '+' on Thai keyboard layout, a key which is at '=/+'
    // key on ANSI keyboard layout is VK_OEM_PLUS.  Native applications
    // handle it as '+' key if Ctrl key is pressed.
    char16_t charForOEMKeyCode = 0;
    switch (mVirtualKeyCode) {
      case VK_OEM_PLUS:
        charForOEMKeyCode = '+';
        break;
      case VK_OEM_COMMA:
        charForOEMKeyCode = ',';
        break;
      case VK_OEM_MINUS:
        charForOEMKeyCode = '-';
        break;
      case VK_OEM_PERIOD:
        charForOEMKeyCode = '.';
        break;
    }
    if (charForOEMKeyCode && charForOEMKeyCode != mUnshiftedString.CharAt(0) &&
        charForOEMKeyCode != mShiftedString.CharAt(0) &&
        charForOEMKeyCode != mUnshiftedLatinChar &&
        charForOEMKeyCode != mShiftedLatinChar) {
      AlternativeCharCode OEMChars(charForOEMKeyCode, charForOEMKeyCode);
      altArray.AppendElement(OEMChars);
    }
  }
}

/*****************************************************************************
 * mozilla::widget::KeyboardLayout
 *****************************************************************************/

KeyboardLayout* KeyboardLayout::sInstance = nullptr;
StaticRefPtr<nsIUserIdleServiceInternal> KeyboardLayout::sIdleService;

// static
KeyboardLayout* KeyboardLayout::GetInstance() {
  if (!sInstance) {
    sInstance = new KeyboardLayout();
  }
  return sInstance;
}

// static
void KeyboardLayout::Shutdown() {
  delete sInstance;
  sInstance = nullptr;
  sIdleService = nullptr;
}

// static
void KeyboardLayout::NotifyIdleServiceOfUserActivity() {
  if (!sIdleService) {
    sIdleService = nsCOMPtr<nsIUserIdleServiceInternal>(
                       do_GetService("@mozilla.org/widget/useridleservice;1"))
                       .forget();
    if (NS_WARN_IF(!sIdleService)) {
      return;
    }
  }
  sIdleService->ResetIdleTimeOut(0);
}

KeyboardLayout::KeyboardLayout() {
  // A dead key sequence should be made from up to 5 keys.  Therefore, 4 is
  // enough and makes sense because the item is uint8_t.
  // (Although, even if it's possible to be 6 keys or more in a sequence,
  // this array will be re-allocated).
  mActiveDeadKeys.SetCapacity(4);
  mDeadKeyShiftStates.SetCapacity(4);

  // If we put it off to load active keyboard layout when first needed, we need
  // to load it at instanciation.  That makes us save the cost of a call of
  // GetKeyboardLayout() API.
  if (StaticPrefs::ui_key_layout_load_when_first_needed()) {
    OnLayoutChange(::GetKeyboardLayout(0));
  }
}

KeyboardLayout::~KeyboardLayout() { ReleaseDeadKeyTables(); }

bool KeyboardLayout::IsPrintableCharKey(uint8_t aVirtualKey) {
  return GetKeyIndex(aVirtualKey) >= 0;
}

WORD KeyboardLayout::ComputeScanCodeForVirtualKeyCode(
    uint8_t aVirtualKeyCode) const {
  return static_cast<WORD>(::MapVirtualKeyEx(aVirtualKeyCode, MAPVK_VK_TO_VSC,
                                             KeyboardLayout::GetLayout()));
}

bool KeyboardLayout::IsDeadKey(uint8_t aVirtualKey,
                               const ModifierKeyState& aModKeyState) const {
  int32_t virtualKeyIndex = GetKeyIndex(aVirtualKey);

  // XXX KeyboardLayout class doesn't support unusual keyboard layout which
  //     maps some function keys as dead keys.
  if (virtualKeyIndex < 0) {
    return false;
  }

  return mVirtualKeys[virtualKeyIndex].IsDeadKey(
      VirtualKey::ModifiersToShiftState(aModKeyState.GetModifiers()));
}

bool KeyboardLayout::IsSysKey(uint8_t aVirtualKey,
                              const ModifierKeyState& aModKeyState) const {
  // If Alt key is not pressed, it's never a system key combination.
  // Additionally, if Ctrl key is pressed, it's never a system key combination
  // too.
  // FYI: Windows logo key state won't affect if it's a system key.
  if (!aModKeyState.IsAlt() || aModKeyState.IsControl()) {
    return false;
  }

  int32_t virtualKeyIndex = GetKeyIndex(aVirtualKey);
  if (virtualKeyIndex < 0) {
    return true;
  }

  UniCharsAndModifiers inputCharsAndModifiers =
      GetUniCharsAndModifiers(aVirtualKey, aModKeyState);
  if (inputCharsAndModifiers.IsEmpty()) {
    return true;
  }

  // If the Alt key state isn't consumed, that means that the key with Alt
  // doesn't cause text input.  So, the combination is a system key.
  return !!(inputCharsAndModifiers.ModifiersAt(0) & MODIFIER_ALT);
}

void KeyboardLayout::InitNativeKey(NativeKey& aNativeKey) {
  if (mIsPendingToRestoreKeyboardLayout) {
    LoadLayout(::GetKeyboardLayout(0));
  }

  // If the aNativeKey is initialized with WM_CHAR, the key information
  // should be discarded because mKeyValue should have the string to be
  // inputted.
  if (aNativeKey.mMsg.message == WM_CHAR) {
    char16_t ch = static_cast<char16_t>(aNativeKey.mMsg.wParam);
    // But don't set key value as printable key if the character is a control
    // character such as 0x0D at pressing Enter key.
    if (!NativeKey::IsControlChar(ch)) {
      aNativeKey.mKeyNameIndex = KEY_NAME_INDEX_USE_STRING;
      Modifiers modifiers =
          aNativeKey.GetModifiers() & ~(MODIFIER_ALT | MODIFIER_CONTROL);
      aNativeKey.mCommittedCharsAndModifiers.Append(ch, modifiers);
      return;
    }
  }

  // If the aNativeKey is in a sequence to input a Unicode character with
  // Alt + numpad keys, we should just set the number as the inputting charcter.
  // Note that we should compute the key value from the virtual key code
  // because they may be mapped to alphabets, but they should be treated as
  // Alt + [0-9] even by web apps.
  // However, we shouldn't touch the key value if given virtual key code is
  // not a printable key because it may be synthesized by 3rd party utility
  // or just NumLock is unlocked and user tries to use shortcut key.  In the
  // latter case, we cannot solve the conflict issue with Alt+foo shortcut key
  // and inputting a Unicode scalar value like reported to bug 1606655, though,
  // I have no better idea.  Perhaps, `Alt` shouldn't be used for shortcut key
  // combination on Windows.
  if (aNativeKey.MaybeTypingUnicodeScalarValue() &&
      KeyboardLayout::IsPrintableCharKey(aNativeKey.mVirtualKeyCode)) {
    // If the key code value is mapped to a Numpad key, let's compute the key
    // value with it.  This is same behavior as Chrome.  In strictly speaking,
    // I think that the else block's computation is better because it seems
    // that Windows does not refer virtual key code value, but we should avoid
    // web-compat issues.
    char16_t num = '0';
    if (aNativeKey.mVirtualKeyCode >= VK_NUMPAD0 &&
        aNativeKey.mVirtualKeyCode <= VK_NUMPAD9) {
      num = '0' + aNativeKey.mVirtualKeyCode - VK_NUMPAD0;
    }
    // Otherwise, let's use fake key value for making never match with
    // mnemonic.
    else {
      switch (aNativeKey.mScanCode) {
        case 0x0052:  // Numpad0
          num = '0';
          break;
        case 0x004F:  // Numpad1
          num = '1';
          break;
        case 0x0050:  // Numpad2
          num = '2';
          break;
        case 0x0051:  // Numpad3
          num = '3';
          break;
        case 0x004B:  // Numpad4
          num = '4';
          break;
        case 0x004C:  // Numpad5
          num = '5';
          break;
        case 0x004D:  // Numpad6
          num = '6';
          break;
        case 0x0047:  // Numpad7
          num = '7';
          break;
        case 0x0048:  // Numpad8
          num = '8';
          break;
        case 0x0049:  // Numpad9
          num = '9';
          break;
        default:
          MOZ_ASSERT_UNREACHABLE(
              "IsTypingUnicodeScalarValue() must have returned true for wrong "
              "scancode");
          break;
      }
    }
    aNativeKey.mCommittedCharsAndModifiers.Append(num,
                                                  aNativeKey.GetModifiers());
    aNativeKey.mKeyNameIndex = KEY_NAME_INDEX_USE_STRING;
    return;
  }

  // When it's followed by non-dead char message(s) for printable character(s),
  // aNativeKey should dispatch eKeyPress events for them rather than
  // information from keyboard layout because respecting WM_(SYS)CHAR messages
  // guarantees that we can always input characters which is expected by
  // the user even if the user uses odd keyboard layout.
  // Or, when it was followed by non-dead char message for a printable character
  // but it's gone at removing the message from the queue, let's treat it
  // as a key inputting empty string.
  if (aNativeKey.IsFollowedByPrintableCharOrSysCharMessage() ||
      aNativeKey.mCharMessageHasGone) {
    MOZ_ASSERT(!aNativeKey.IsCharMessage(aNativeKey.mMsg));
    if (aNativeKey.IsFollowedByPrintableCharOrSysCharMessage()) {
      // Initialize mCommittedCharsAndModifiers with following char messages.
      aNativeKey.InitCommittedCharsAndModifiersWithFollowingCharMessages();
      MOZ_ASSERT(!aNativeKey.mCommittedCharsAndModifiers.IsEmpty());

      // Currently, we are doing a ugly hack to keypress events to cause
      // inputting character even if Ctrl or Alt key is pressed, that is, we
      // remove Ctrl and Alt modifier state from keypress event.  However, for
      // example, Ctrl+Space which causes ' ' of WM_CHAR message never causes
      // keypress event whose ctrlKey is true.  For preventing this problem,
      // we should mark as not removable if Ctrl or Alt key does not cause
      // changing inputting character.
      if (IsPrintableCharKey(aNativeKey.mOriginalVirtualKeyCode) &&
          (aNativeKey.IsControl() ^ aNativeKey.IsAlt())) {
        ModifierKeyState state = aNativeKey.ModifierKeyStateRef();
        state.Unset(MODIFIER_ALT | MODIFIER_CONTROL);
        UniCharsAndModifiers charsWithoutModifier =
            GetUniCharsAndModifiers(aNativeKey.GenericVirtualKeyCode(), state);
        aNativeKey.mCanIgnoreModifierStateAtKeyPress =
            !charsWithoutModifier.UniCharsEqual(
                aNativeKey.mCommittedCharsAndModifiers);
      }
    } else {
      aNativeKey.mCommittedCharsAndModifiers.Clear();
    }
    aNativeKey.mKeyNameIndex = KEY_NAME_INDEX_USE_STRING;

    // If it's not in dead key sequence, we don't need to do anymore here.
    if (!IsInDeadKeySequence()) {
      return;
    }

    // If it's in dead key sequence and dead char is inputted as is, we need to
    // set the previous modifier state which is stored when preceding dead key
    // is pressed.
    UniCharsAndModifiers deadChars = GetDeadUniCharsAndModifiers();
    aNativeKey.mCommittedCharsAndModifiers.OverwriteModifiersIfBeginsWith(
        deadChars);
    // Finish the dead key sequence.
    DeactivateDeadKeyState();
    return;
  }

  // If it's a dead key, aNativeKey will be initialized by
  // MaybeInitNativeKeyAsDeadKey().
  if (MaybeInitNativeKeyAsDeadKey(aNativeKey)) {
    return;
  }

  // If the key is not a usual printable key, KeyboardLayout class assume that
  // it's not cause dead char nor printable char.  Therefore, there are nothing
  // to do here fore such keys (e.g., function keys).
  // However, this should keep dead key state even if non-printable key is
  // pressed during a dead key sequence.
  if (!IsPrintableCharKey(aNativeKey.mOriginalVirtualKeyCode)) {
    return;
  }

  MOZ_ASSERT(aNativeKey.mOriginalVirtualKeyCode != VK_PACKET,
             "At handling VK_PACKET, we shouldn't refer keyboard layout");
  MOZ_ASSERT(
      aNativeKey.mKeyNameIndex == KEY_NAME_INDEX_USE_STRING,
      "Printable key's key name index must be KEY_NAME_INDEX_USE_STRING");

  // If it's in dead key handling and the pressed key causes a composite
  // character, aNativeKey will be initialized by
  // MaybeInitNativeKeyWithCompositeChar().
  if (MaybeInitNativeKeyWithCompositeChar(aNativeKey)) {
    return;
  }

  UniCharsAndModifiers baseChars = GetUniCharsAndModifiers(aNativeKey);

  // If the key press isn't related to any dead keys, initialize aNativeKey
  // with the characters which should be caused by the key.
  if (!IsInDeadKeySequence()) {
    aNativeKey.mCommittedCharsAndModifiers = baseChars;
    return;
  }

  // If the key doesn't cause a composite character with preceding dead key,
  // initialize aNativeKey with the dead-key character followed by current
  // key's character.
  UniCharsAndModifiers deadChars = GetDeadUniCharsAndModifiers();
  aNativeKey.mCommittedCharsAndModifiers = deadChars + baseChars;
  if (aNativeKey.IsKeyDownMessage()) {
    DeactivateDeadKeyState();
  }
}

bool KeyboardLayout::MaybeInitNativeKeyAsDeadKey(NativeKey& aNativeKey) {
  // Only when it's not in dead key sequence, we can trust IsDeadKey() result.
  if (!IsInDeadKeySequence() && !IsDeadKey(aNativeKey)) {
    return false;
  }

  // When keydown message is followed by a dead char message, it should be
  // initialized as dead key.
  const bool isDeadKeyDownEvent =
      aNativeKey.IsKeyDownMessage() && aNativeKey.IsFollowedByDeadCharMessage();

  // When keyup message is received, let's check if it's one of preceding
  // dead keys because keydown message order and keyup message order may be
  // different.
  const bool isDeadKeyUpEvent =
      !aNativeKey.IsKeyDownMessage() &&
      mActiveDeadKeys.Contains(aNativeKey.GenericVirtualKeyCode());

  if (isDeadKeyDownEvent || isDeadKeyUpEvent) {
    ActivateDeadKeyState(aNativeKey);
    // Any dead key events don't generate characters.  So, a dead key should
    // cause only keydown event and keyup event whose KeyboardEvent.key
    // values are "Dead".
    aNativeKey.mCommittedCharsAndModifiers.Clear();
    aNativeKey.mKeyNameIndex = KEY_NAME_INDEX_Dead;
    return true;
  }

  // At keydown message handling, we need to forget the first dead key
  // because there is no guarantee coming WM_KEYUP for the second dead
  // key before next WM_KEYDOWN.  E.g., due to auto key repeat or pressing
  // another dead key before releasing current key.  Therefore, we can
  // set only a character for current key for keyup event.
  if (!IsInDeadKeySequence()) {
    aNativeKey.mCommittedCharsAndModifiers =
        GetUniCharsAndModifiers(aNativeKey);
    return true;
  }

  // When non-printable key event comes during a dead key sequence, that must
  // be a modifier key event.  So, such events shouldn't be handled as a part
  // of the dead key sequence.
  if (!IsDeadKey(aNativeKey)) {
    return false;
  }

  // FYI: Following code may run when the user doesn't input text actually
  //      but the key sequence is a dead key sequence.  For example,
  //      ` -> Ctrl+` with Spanish keyboard layout.  Let's keep using this
  //      complicated code for now because this runs really rarely.

  // Dead key followed by another dead key may cause a composed character
  // (e.g., "Russian - Mnemonic" keyboard layout's 's' -> 'c').
  if (MaybeInitNativeKeyWithCompositeChar(aNativeKey)) {
    return true;
  }

  // Otherwise, dead key followed by another dead key causes inputting both
  // character.
  UniCharsAndModifiers prevDeadChars = GetDeadUniCharsAndModifiers();
  UniCharsAndModifiers newChars = GetUniCharsAndModifiers(aNativeKey);
  // But keypress events should be fired for each committed character.
  aNativeKey.mCommittedCharsAndModifiers = prevDeadChars + newChars;
  if (aNativeKey.IsKeyDownMessage()) {
    DeactivateDeadKeyState();
  }
  return true;
}

bool KeyboardLayout::MaybeInitNativeKeyWithCompositeChar(
    NativeKey& aNativeKey) {
  if (!IsInDeadKeySequence()) {
    return false;
  }

  if (NS_WARN_IF(!IsPrintableCharKey(aNativeKey.mOriginalVirtualKeyCode))) {
    return false;
  }

  UniCharsAndModifiers baseChars = GetUniCharsAndModifiers(aNativeKey);
  if (baseChars.IsEmpty() || !baseChars.CharAt(0)) {
    return false;
  }

  char16_t compositeChar = GetCompositeChar(baseChars.CharAt(0));
  if (!compositeChar) {
    return false;
  }

  // Active dead-key and base character does produce exactly one composite
  // character.
  aNativeKey.mCommittedCharsAndModifiers.Append(compositeChar,
                                                baseChars.ModifiersAt(0));
  if (aNativeKey.IsKeyDownMessage()) {
    DeactivateDeadKeyState();
  }
  return true;
}

UniCharsAndModifiers KeyboardLayout::GetUniCharsAndModifiers(
    uint8_t aVirtualKey, VirtualKey::ShiftState aShiftState) const {
  UniCharsAndModifiers result;
  int32_t key = GetKeyIndex(aVirtualKey);
  if (key < 0) {
    return result;
  }
  return mVirtualKeys[key].GetUniChars(aShiftState);
}

UniCharsAndModifiers KeyboardLayout::GetDeadUniCharsAndModifiers() const {
  MOZ_RELEASE_ASSERT(mActiveDeadKeys.Length() == mDeadKeyShiftStates.Length());

  if (NS_WARN_IF(mActiveDeadKeys.IsEmpty())) {
    return UniCharsAndModifiers();
  }

  UniCharsAndModifiers result;
  for (size_t i = 0; i < mActiveDeadKeys.Length(); ++i) {
    result +=
        GetUniCharsAndModifiers(mActiveDeadKeys[i], mDeadKeyShiftStates[i]);
  }
  return result;
}

char16_t KeyboardLayout::GetCompositeChar(char16_t aBaseChar) const {
  if (NS_WARN_IF(mActiveDeadKeys.IsEmpty())) {
    return 0;
  }
  // XXX Currently, we don't support computing a composite character with
  //     two or more dead keys since it needs big table for supporting
  //     long chained dead keys.  However, this should be a minor bug
  //     because this runs only when the latest keydown event does not cause
  //     WM_(SYS)CHAR messages.  So, when user wants to input a character,
  //     this path never runs.
  if (mActiveDeadKeys.Length() > 1) {
    return 0;
  }
  int32_t key = GetKeyIndex(mActiveDeadKeys[0]);
  if (key < 0) {
    return 0;
  }
  return mVirtualKeys[key].GetCompositeChar(mDeadKeyShiftStates[0], aBaseChar);
}

static bool IsValidKeyboardLayoutsChild(const nsAString& aChildName) {
  if (aChildName.Length() != 8) {
    return false;
  }
  for (size_t i = 0; i < aChildName.Length(); i++) {
    if ((aChildName[i] >= '0' && aChildName[i] <= '9') ||
        (aChildName[i] >= 'a' && aChildName[i] <= 'f') ||
        (aChildName[i] >= 'A' && aChildName[i] <= 'F')) {
      continue;
    }
    return false;
  }
  return true;
}

// static
nsCString KeyboardLayout::GetLayoutName(HKL aLayout) {
  constexpr auto kKeyboardLayouts =
      u"SYSTEM\\CurrentControlSet\\Control\\Keyboard Layouts\\"_ns;
  uint16_t language = reinterpret_cast<uintptr_t>(aLayout) & 0xFFFF;
  uint16_t layout = (reinterpret_cast<uintptr_t>(aLayout) >> 16) & 0xFFFF;
  // If the layout is less than 0xA000XXXX (normal keyboard layout for the
  // language) or 0xEYYYXXXX (IMM-IME), we can retrieve its name simply.
  if (layout < 0xA000 || (layout & 0xF000) == 0xE000) {
    nsAutoString key(kKeyboardLayouts);
    key.AppendPrintf("%08" PRIXPTR, layout < 0xA000
                                        ? layout
                                        : reinterpret_cast<uintptr_t>(aLayout));
    wchar_t buf[256];
    if (NS_WARN_IF(!WinRegistry::GetString(
            HKEY_LOCAL_MACHINE, key, u"Layout Text"_ns, buf,
            WinRegistry::kLegacyWinUtilsStringFlags))) {
      return "No name or too long name"_ns;
    }
    return NS_ConvertUTF16toUTF8(buf);
  }

  if (NS_WARN_IF((layout & 0xF000) != 0xF000)) {
    nsCString result;
    result.AppendPrintf("Odd HKL: 0x%08" PRIXPTR,
                        reinterpret_cast<uintptr_t>(aLayout));
    return result;
  }

  // Otherwise, we need to walk the registry under "Keyboard Layouts".
  WinRegistry::Key regKey(HKEY_LOCAL_MACHINE, kKeyboardLayouts,
                          WinRegistry::KeyMode::Read);
  if (NS_WARN_IF(!regKey)) {
    return ""_ns;
  }
  uint32_t childCount = regKey.GetChildCount();
  if (NS_WARN_IF(!childCount)) {
    return ""_ns;
  }
  for (uint32_t i = 0; i < childCount; i++) {
    nsAutoString childName;
    if (NS_WARN_IF(!regKey.GetChildName(i, childName)) ||
        !IsValidKeyboardLayoutsChild(childName)) {
      continue;
    }
    nsresult rv = NS_OK;
    uint32_t childNum = static_cast<uint32_t>(childName.ToInteger64(&rv, 16));
    if (NS_WARN_IF(NS_FAILED(rv))) {
      continue;
    }
    // Ignore normal keyboard layouts for each language.
    if (childNum <= 0xFFFF) {
      continue;
    }
    // If it doesn't start with 'A' nor 'a', language should be matched.
    if ((childNum & 0xFFFF) != language &&
        (childNum & 0xF0000000) != 0xA0000000) {
      continue;
    }
    // Then, the child should have "Layout Id" which is "YYY" of 0xFYYYXXXX.
    nsAutoString key(kKeyboardLayouts);
    key += childName;
    WinRegistry::Key regKey(HKEY_LOCAL_MACHINE, key,
                            WinRegistry::KeyMode::QueryValue);
    if (NS_WARN_IF(!regKey)) {
      continue;
    }
    wchar_t buf[256];
    if (NS_WARN_IF(!regKey.GetValueAsString(
            u"Layout Id"_ns, buf, WinRegistry::kLegacyWinUtilsStringFlags))) {
      continue;
    }
    uint16_t layoutId = wcstol(buf, nullptr, 16);
    if (layoutId != (layout & 0x0FFF)) {
      continue;
    }
    if (NS_WARN_IF(!regKey.GetValueAsString(
            u"Layout Text"_ns, buf, WinRegistry::kLegacyWinUtilsStringFlags))) {
      continue;
    }
    return NS_ConvertUTF16toUTF8(buf);
  }
  return ""_ns;
}

void KeyboardLayout::LoadLayout(HKL aLayout) {
  mIsPendingToRestoreKeyboardLayout = false;

  if (mKeyboardLayout == aLayout) {
    return;
  }

  mKeyboardLayout = aLayout;
  mHasAltGr = false;

  MOZ_LOG(gKeyLog, LogLevel::Info,
          ("KeyboardLayout::LoadLayout(aLayout=0x%p (%s))", aLayout,
           GetLayoutName(aLayout).get()));

  BYTE kbdState[256];
  memset(kbdState, 0, sizeof(kbdState));

  BYTE originalKbdState[256];
  // Bitfield with all shift states that have at least one dead-key.
  uint16_t shiftStatesWithDeadKeys = 0;
  // Bitfield with all shift states that produce any possible dead-key base
  // characters.
  uint16_t shiftStatesWithBaseChars = 0;

  mActiveDeadKeys.Clear();
  mDeadKeyShiftStates.Clear();

  ReleaseDeadKeyTables();

  ::GetKeyboardState(originalKbdState);

  // For each shift state gather all printable characters that are produced
  // for normal case when no any dead-key is active.

  for (VirtualKey::ShiftState shiftState = 0; shiftState < 16; shiftState++) {
    VirtualKey::FillKbdState(kbdState, shiftState);
    bool isAltGr = VirtualKey::IsAltGrIndex(shiftState);
    for (uint32_t virtualKey = 0; virtualKey < 256; virtualKey++) {
      int32_t vki = GetKeyIndex(virtualKey);
      if (vki < 0) {
        continue;
      }
      NS_ASSERTION(uint32_t(vki) < std::size(mVirtualKeys), "invalid index");
      char16_t uniChars[5];
      int32_t ret = ::ToUnicodeEx(virtualKey, 0, kbdState, (LPWSTR)uniChars,
                                  std::size(uniChars), 0, mKeyboardLayout);
      // dead-key
      if (ret < 0) {
        shiftStatesWithDeadKeys |= (1 << shiftState);
        // Repeat dead-key to deactivate it and get its character
        // representation.
        char16_t deadChar[2];
        ret = ::ToUnicodeEx(virtualKey, 0, kbdState, (LPWSTR)deadChar,
                            std::size(deadChar), 0, mKeyboardLayout);
        NS_ASSERTION(ret == 2, "Expecting twice repeated dead-key character");
        mVirtualKeys[vki].SetDeadChar(shiftState, deadChar[0]);

        MOZ_LOG(gKeyLog, LogLevel::Verbose,
                ("  %s (%d): DeadChar(%s, %s) (ret=%d)",
                 kVirtualKeyName[virtualKey], vki,
                 GetShiftStateName(shiftState).get(),
                 GetCharacterCodeNames(deadChar, 1).get(), ret));
      } else {
        if (ret == 1) {
          // dead-key can pair only with exactly one base character.
          shiftStatesWithBaseChars |= (1 << shiftState);
        }
        mVirtualKeys[vki].SetNormalChars(shiftState, uniChars, ret);
        MOZ_LOG(gKeyLog, LogLevel::Verbose,
                ("  %s (%d): NormalChar(%s, %s) (ret=%d)",
                 kVirtualKeyName[virtualKey], vki,
                 GetShiftStateName(shiftState).get(),
                 GetCharacterCodeNames(uniChars, ret).get(), ret));
      }

      // If the key inputs at least one character with AltGr modifier,
      // check if AltGr changes inputting character.  If it does, mark
      // this keyboard layout has AltGr modifier actually.
      if (!mHasAltGr && ret > 0 && isAltGr &&
          mVirtualKeys[vki].IsChangedByAltGr(shiftState)) {
        mHasAltGr = true;
        MOZ_LOG(gKeyLog, LogLevel::Info,
                ("  Found a key (%s) changed by AltGr: %s -> %s (%s) (ret=%d)",
                 kVirtualKeyName[virtualKey],
                 GetCharacterCodeNames(
                     mVirtualKeys[vki].GetNativeUniChars(
                         shiftState - VirtualKey::ShiftStateIndex::eAltGr))
                     .get(),
                 GetCharacterCodeNames(
                     mVirtualKeys[vki].GetNativeUniChars(shiftState))
                     .get(),
                 GetShiftStateName(shiftState).get(), ret));
      }
    }
  }

  // Now process each dead-key to find all its base characters and resulting
  // composite characters.
  for (VirtualKey::ShiftState shiftState = 0; shiftState < 16; shiftState++) {
    if (!(shiftStatesWithDeadKeys & (1 << shiftState))) {
      continue;
    }

    VirtualKey::FillKbdState(kbdState, shiftState);

    for (uint32_t virtualKey = 0; virtualKey < 256; virtualKey++) {
      int32_t vki = GetKeyIndex(virtualKey);
      if (vki >= 0 && mVirtualKeys[vki].IsDeadKey(shiftState)) {
        AutoTArray<DeadKeyEntry, 256> deadKeyArray;
        GetDeadKeyCombinations(virtualKey, kbdState, shiftStatesWithBaseChars,
                               deadKeyArray);
        const auto* dkt = mVirtualKeys[vki].MatchingDeadKeyTable(deadKeyArray);
        if (!dkt) {
          dkt = AddDeadKeyTable(deadKeyArray);
        }
        mVirtualKeys[vki].AttachDeadKeyTable(shiftState, dkt);
      }
    }
  }

  ::SetKeyboardState(originalKbdState);

  if (MOZ_LOG_TEST(gKeyLog, LogLevel::Verbose)) {
    static const UINT kExtendedScanCode[] = {0x0000, 0xE000};
    static const UINT kMapType = MAPVK_VSC_TO_VK_EX;
    MOZ_LOG(gKeyLog, LogLevel::Verbose,
            ("Logging virtual keycode values for scancode (0x%p)...",
             mKeyboardLayout));
    for (uint32_t i = 0; i < std::size(kExtendedScanCode); i++) {
      for (uint32_t j = 1; j <= 0xFF; j++) {
        UINT scanCode = kExtendedScanCode[i] + j;
        UINT virtualKeyCode =
            ::MapVirtualKeyEx(scanCode, kMapType, mKeyboardLayout);
        MOZ_LOG(gKeyLog, LogLevel::Verbose,
                ("0x%04X, %s", scanCode, kVirtualKeyName[virtualKeyCode]));
      }
    }
  }

  MOZ_LOG(gKeyLog, LogLevel::Info,
          ("  AltGr key is %s in %s", mHasAltGr ? "found" : "not found",
           GetLayoutName(aLayout).get()));
}

inline int32_t KeyboardLayout::GetKeyIndex(uint8_t aVirtualKey) {
  // Currently these 68 (NS_NUM_OF_KEYS) virtual keys are assumed
  // to produce visible representation:
  // 0x20 - VK_SPACE          ' '
  // 0x30..0x39               '0'..'9'
  // 0x41..0x5A               'A'..'Z'
  // 0x60..0x69               '0'..'9' on numpad
  // 0x6A - VK_MULTIPLY       '*' on numpad
  // 0x6B - VK_ADD            '+' on numpad
  // 0x6D - VK_SUBTRACT       '-' on numpad
  // 0x6E - VK_DECIMAL        '.' on numpad
  // 0x6F - VK_DIVIDE         '/' on numpad
  // 0x6E - VK_DECIMAL        '.'
  // 0xBA - VK_OEM_1          ';:' for US
  // 0xBB - VK_OEM_PLUS       '+' any country
  // 0xBC - VK_OEM_COMMA      ',' any country
  // 0xBD - VK_OEM_MINUS      '-' any country
  // 0xBE - VK_OEM_PERIOD     '.' any country
  // 0xBF - VK_OEM_2          '/?' for US
  // 0xC0 - VK_OEM_3          '`~' for US
  // 0xC1 - VK_ABNT_C1        '/?' for Brazilian
  // 0xC2 - VK_ABNT_C2        separator key on numpad (Brazilian or JIS for Mac)
  // 0xDB - VK_OEM_4          '[{' for US
  // 0xDC - VK_OEM_5          '\|' for US
  // 0xDD - VK_OEM_6          ']}' for US
  // 0xDE - VK_OEM_7          ''"' for US
  // 0xDF - VK_OEM_8
  // 0xE1 - no name
  // 0xE2 - VK_OEM_102        '\_' for JIS
  // 0xE3 - no name
  // 0xE4 - no name

  static const int8_t xlat[256] = {
      // 0   1   2   3   4   5   6   7   8   9   A   B   C   D   E   F
      //-----------------------------------------------------------------------
      -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  // 00
      -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  // 10
      0,  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  // 20
      1,  2,  3,  4,  5,  6,  7,  8,  9,  10, -1, -1, -1, -1, -1, -1,  // 30
      -1, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25,  // 40
      26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, -1, -1, -1, -1, -1,  // 50
      37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, -1, 49, 50, 51,  // 60
      -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  // 70
      -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  // 80
      -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  // 90
      -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  // A0
      -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 52, 53, 54, 55, 56, 57,  // B0
      58, 59, 60, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  // C0
      -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 61, 62, 63, 64, 65,  // D0
      -1, 66, 67, 68, 69, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  // E0
      -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1   // F0
  };

  return xlat[aVirtualKey];
}

const DeadKeyTable* KeyboardLayout::AddDeadKeyTable(
    const DeadKeyTable& aDeadKeyTable) {
  auto* dkt = new DeadKeyTable(aDeadKeyTable.Clone());
  mDeadKeyTableList.AppendElement(dkt);
  return dkt;
}

void KeyboardLayout::ReleaseDeadKeyTables() { mDeadKeyTableList.Clear(); }

bool KeyboardLayout::EnsureDeadKeyActive(bool aIsActive, uint8_t aDeadKey,
                                         const PBYTE aDeadKeyKbdState) {
  int32_t ret;
  do {
    char16_t dummyChars[5];
    ret =
        ::ToUnicodeEx(aDeadKey, 0, (PBYTE)aDeadKeyKbdState, (LPWSTR)dummyChars,
                      std::size(dummyChars), 0, mKeyboardLayout);
    // returned values:
    // <0 - Dead key state is active. The keyboard driver will wait for next
    //      character.
    //  1 - Previous pressed key was a valid base character that produced
    //      exactly one composite character.
    // >1 - Previous pressed key does not produce any composite characters.
    //      Return dead-key character followed by base character(s).
  } while ((ret < 0) != aIsActive);

  return (ret < 0);
}

void KeyboardLayout::ActivateDeadKeyState(const NativeKey& aNativeKey) {
  // Dead-key state should be activated at keydown.
  if (!aNativeKey.IsKeyDownMessage()) {
    return;
  }

  mActiveDeadKeys.AppendElement(aNativeKey.mOriginalVirtualKeyCode);
  mDeadKeyShiftStates.AppendElement(aNativeKey.GetShiftState());
}

void KeyboardLayout::DeactivateDeadKeyState() {
  if (mActiveDeadKeys.IsEmpty()) {
    return;
  }

  BYTE kbdState[256];
  memset(kbdState, 0, sizeof(kbdState));

  // Assume that the last dead key can finish dead key sequence.
  VirtualKey::FillKbdState(kbdState, mDeadKeyShiftStates.LastElement());
  EnsureDeadKeyActive(false, mActiveDeadKeys.LastElement(), kbdState);
  mActiveDeadKeys.Clear();
  mDeadKeyShiftStates.Clear();
}

void KeyboardLayout::AddDeadKeyEntry(char16_t aBaseChar,
                                     char16_t aCompositeChar,
                                     DeadKeyTable& aDeadKeyTable) {
  auto dke = DeadKeyEntry(aBaseChar, aCompositeChar);
  for (const auto& entry : aDeadKeyTable) {
    if (entry == dke) {
      return;
    }
  }
  aDeadKeyTable.AppendElement(dke);
}

void KeyboardLayout::GetDeadKeyCombinations(uint8_t aDeadKey,
                                            const PBYTE aDeadKeyKbdState,
                                            uint16_t aShiftStatesWithBaseChars,
                                            DeadKeyTable& aDeadKeyTable) {
  bool deadKeyActive = false;
  BYTE kbdState[256];
  memset(kbdState, 0, sizeof(kbdState));

  for (uint32_t shiftState = 0; shiftState < 16; shiftState++) {
    if (!(aShiftStatesWithBaseChars & (1 << shiftState))) {
      continue;
    }

    VirtualKey::FillKbdState(kbdState, shiftState);

    for (uint32_t virtualKey = 0; virtualKey < 256; virtualKey++) {
      int32_t vki = GetKeyIndex(virtualKey);
      // Dead-key can pair only with such key that produces exactly one base
      // character.
      if (vki >= 0 &&
          mVirtualKeys[vki].GetNativeUniChars(shiftState).Length() == 1) {
        // Ensure dead-key is in active state, when it swallows entered
        // character and waits for the next pressed key.
        if (!deadKeyActive) {
          deadKeyActive = EnsureDeadKeyActive(true, aDeadKey, aDeadKeyKbdState);
        }

        // Depending on the character the followed the dead-key, the keyboard
        // driver can produce one composite character, or a dead-key character
        // followed by a second character.
        char16_t compositeChars[5];
        int32_t ret =
            ::ToUnicodeEx(virtualKey, 0, kbdState, (LPWSTR)compositeChars,
                          std::size(compositeChars), 0, mKeyboardLayout);
        switch (ret) {
          case 0:
            // This key combination does not produce any characters. The
            // dead-key is still in active state.
            break;
          case 1: {
            char16_t baseChars[5];
            ret = ::ToUnicodeEx(virtualKey, 0, kbdState, (LPWSTR)baseChars,
                                std::size(baseChars), 0, mKeyboardLayout);
            if (aDeadKeyTable.Length() < aDeadKeyTable.Capacity()) {
              switch (ret) {
                case 1:
                  // Exactly one composite character produced. Now, when
                  // dead-key is not active, repeat the last character one more
                  // time to determine the base character.
                  AddDeadKeyEntry(baseChars[0], compositeChars[0],
                                  aDeadKeyTable);
                  deadKeyActive = false;
                  break;
                case -1: {
                  // If pressing another dead-key produces different character,
                  // we should register the dead-key entry with first character
                  // produced by current key.

                  // First inactivate the dead-key state completely.
                  deadKeyActive =
                      EnsureDeadKeyActive(false, aDeadKey, aDeadKeyKbdState);
                  if (NS_WARN_IF(deadKeyActive)) {
                    MOZ_LOG(gKeyLog, LogLevel::Error,
                            ("  failed to deactivating the dead-key state..."));
                    break;
                  }
                  for (int32_t i = 0; i < 5; ++i) {
                    ret = ::ToUnicodeEx(virtualKey, 0, kbdState,
                                        (LPWSTR)baseChars, std::size(baseChars),
                                        0, mKeyboardLayout);
                    if (ret >= 0) {
                      break;
                    }
                  }
                  if (ret > 0) {
                    AddDeadKeyEntry(baseChars[0], compositeChars[0],
                                    aDeadKeyTable);
                  }
                  // Inactivate dead-key state for current virtual keycode.
                  EnsureDeadKeyActive(false, virtualKey, kbdState);
                  break;
                }
                default:
                  NS_WARNING("File a bug for this dead-key handling!");
                  deadKeyActive = false;
                  break;
              }
            }
            MOZ_LOG(
                gKeyLog, LogLevel::Verbose,
                ("  %s -> %s (%d): DeadKeyEntry(%s, %s) (ret=%d)",
                 kVirtualKeyName[aDeadKey], kVirtualKeyName[virtualKey], vki,
                 GetCharacterCodeNames(compositeChars, 1).get(),
                 ret <= 0
                     ? "''"
                     : GetCharacterCodeNames(baseChars, std::min(ret, 5)).get(),
                 ret));
            break;
          }
          default:
            // 1. Unexpected dead-key. Dead-key chaining is not supported.
            // 2. More than one character generated. This is not a valid
            //    dead-key and base character combination.
            deadKeyActive = false;
            MOZ_LOG(
                gKeyLog, LogLevel::Verbose,
                ("  %s -> %s (%d): Unsupport dead key type(%s) (ret=%d)",
                 kVirtualKeyName[aDeadKey], kVirtualKeyName[virtualKey], vki,
                 ret <= 0
                     ? "''"
                     : GetCharacterCodeNames(compositeChars, std::min(ret, 5))
                           .get(),
                 ret));
            break;
        }
      }
    }
  }

  if (deadKeyActive) {
    deadKeyActive = EnsureDeadKeyActive(false, aDeadKey, aDeadKeyKbdState);
  }

  aDeadKeyTable.Sort();
}

uint32_t KeyboardLayout::ConvertNativeKeyCodeToDOMKeyCode(
    UINT aNativeKeyCode) const {
  // Alphabet or Numeric or Numpad or Function keys
  if ((aNativeKeyCode >= 0x30 && aNativeKeyCode <= 0x39) ||
      (aNativeKeyCode >= 0x41 && aNativeKeyCode <= 0x5A) ||
      (aNativeKeyCode >= 0x60 && aNativeKeyCode <= 0x87)) {
    return static_cast<uint32_t>(aNativeKeyCode);
  }
  switch (aNativeKeyCode) {
    // Following keycodes are same as our DOM keycodes
    case VK_CANCEL:
    case VK_BACK:
    case VK_TAB:
    case VK_CLEAR:
    case VK_RETURN:
    case VK_SHIFT:
    case VK_CONTROL:
    case VK_MENU:  // Alt
    case VK_PAUSE:
    case VK_CAPITAL:  // CAPS LOCK
    case VK_KANA:     // same as VK_HANGUL
    case VK_JUNJA:
    case VK_FINAL:
    case VK_HANJA:  // same as VK_KANJI
    case VK_ESCAPE:
    case VK_CONVERT:
    case VK_NONCONVERT:
    case VK_ACCEPT:
    case VK_MODECHANGE:
    case VK_SPACE:
    case VK_PRIOR:  // PAGE UP
    case VK_NEXT:   // PAGE DOWN
    case VK_END:
    case VK_HOME:
    case VK_LEFT:
    case VK_UP:
    case VK_RIGHT:
    case VK_DOWN:
    case VK_SELECT:
    case VK_PRINT:
    case VK_EXECUTE:
    case VK_SNAPSHOT:
    case VK_INSERT:
    case VK_DELETE:
    case VK_APPS:  // Context Menu
    case VK_SLEEP:
    case VK_NUMLOCK:
    case VK_SCROLL:  // SCROLL LOCK
    case VK_ATTN:    // Attension key of IBM midrange computers, e.g., AS/400
    case VK_CRSEL:   // Cursor Selection
    case VK_EXSEL:   // Extend Selection
    case VK_EREOF:   // Erase EOF key of IBM 3270 keyboard layout
    case VK_PLAY:
    case VK_ZOOM:
    case VK_PA1:  // PA1 key of IBM 3270 keyboard layout
      return uint32_t(aNativeKeyCode);

    case VK_HELP:
      return NS_VK_HELP;

    // Windows key should be mapped to a Win keycode
    // They should be able to be distinguished by DOM3 KeyboardEvent.location
    case VK_LWIN:
    case VK_RWIN:
      return NS_VK_WIN;

    case VK_VOLUME_MUTE:
      return NS_VK_VOLUME_MUTE;
    case VK_VOLUME_DOWN:
      return NS_VK_VOLUME_DOWN;
    case VK_VOLUME_UP:
      return NS_VK_VOLUME_UP;

    case VK_LSHIFT:
    case VK_RSHIFT:
      return NS_VK_SHIFT;

    case VK_LCONTROL:
    case VK_RCONTROL:
      return NS_VK_CONTROL;

    // Note that even if the key is AltGr, we should return NS_VK_ALT for
    // compatibility with both older Gecko and the other browsers.
    case VK_LMENU:
    case VK_RMENU:
      return NS_VK_ALT;

    // Following keycodes are not defined in our DOM keycodes.
    case VK_BROWSER_BACK:
    case VK_BROWSER_FORWARD:
    case VK_BROWSER_REFRESH:
    case VK_BROWSER_STOP:
    case VK_BROWSER_SEARCH:
    case VK_BROWSER_FAVORITES:
    case VK_BROWSER_HOME:
    case VK_MEDIA_NEXT_TRACK:
    case VK_MEDIA_PREV_TRACK:
    case VK_MEDIA_STOP:
    case VK_MEDIA_PLAY_PAUSE:
    case VK_LAUNCH_MAIL:
    case VK_LAUNCH_MEDIA_SELECT:
    case VK_LAUNCH_APP1:
    case VK_LAUNCH_APP2:
      return 0;

    // Following OEM specific virtual keycodes should pass through DOM keyCode
    // for compatibility with the other browsers on Windows.

    // Following OEM specific virtual keycodes are defined for Fujitsu/OASYS.
    case VK_OEM_FJ_JISHO:
    case VK_OEM_FJ_MASSHOU:
    case VK_OEM_FJ_TOUROKU:
    case VK_OEM_FJ_LOYA:
    case VK_OEM_FJ_ROYA:
    // Not sure what means "ICO".
    case VK_ICO_HELP:
    case VK_ICO_00:
    case VK_ICO_CLEAR:
    // Following OEM specific virtual keycodes are defined for Nokia/Ericsson.
    case VK_OEM_RESET:
    case VK_OEM_JUMP:
    case VK_OEM_PA1:
    case VK_OEM_PA2:
    case VK_OEM_PA3:
    case VK_OEM_WSCTRL:
    case VK_OEM_CUSEL:
    case VK_OEM_ATTN:
    case VK_OEM_FINISH:
    case VK_OEM_COPY:
    case VK_OEM_AUTO:
    case VK_OEM_ENLW:
    case VK_OEM_BACKTAB:
    // VK_OEM_CLEAR is defined as not OEM specific, but let's pass though
    // DOM keyCode like other OEM specific virtual keycodes.
    case VK_OEM_CLEAR:
      return uint32_t(aNativeKeyCode);

    // 0xE1 is an OEM specific virtual keycode. However, the value is already
    // used in our DOM keyCode for AltGr on Linux. So, this virtual keycode
    // cannot pass through DOM keyCode.
    case 0xE1:
      return 0;

    // Following keycodes are OEM keys which are keycodes for non-alphabet and
    // non-numeric keys, we should compute each keycode of them from unshifted
    // character which is inputted by each key.  But if the unshifted character
    // is not an ASCII character but shifted character is an ASCII character,
    // we should refer it.
    case VK_OEM_1:
    case VK_OEM_PLUS:
    case VK_OEM_COMMA:
    case VK_OEM_MINUS:
    case VK_OEM_PERIOD:
    case VK_OEM_2:
    case VK_OEM_3:
    case VK_OEM_4:
    case VK_OEM_5:
    case VK_OEM_6:
    case VK_OEM_7:
    case VK_OEM_8:
    case VK_OEM_102:
    case VK_ABNT_C1: {
      NS_ASSERTION(IsPrintableCharKey(aNativeKeyCode),
                   "The key must be printable");
      ModifierKeyState modKeyState(0);
      UniCharsAndModifiers uniChars =
          GetUniCharsAndModifiers(aNativeKeyCode, modKeyState);
      if (uniChars.Length() != 1 || uniChars.CharAt(0) < ' ' ||
          uniChars.CharAt(0) > 0x7F) {
        modKeyState.Set(MODIFIER_SHIFT);
        uniChars = GetUniCharsAndModifiers(aNativeKeyCode, modKeyState);
        if (uniChars.Length() != 1 || uniChars.CharAt(0) < ' ' ||
            uniChars.CharAt(0) > 0x7F) {
          // In this case, we've returned 0 in this case for long time because
          // we decided that we should avoid setting same keyCode value to 2 or
          // more keys since active keyboard layout may have a key to input the
          // punctuation with different key.  However, setting keyCode to 0
          // makes some web applications which are aware of neither
          // KeyboardEvent.key nor KeyboardEvent.code not work with Firefox
          // when user selects non-ASCII capable keyboard layout such as
          // Russian and Thai layout.  So, let's decide keyCode value with
          // major keyboard layout's key which causes the OEM keycode.
          // Actually, this maps same keyCode value to 2 keys on Russian
          // keyboard layout.  "Period" key causes VK_OEM_PERIOD but inputs
          // Yu of Cyrillic and "Slash" key causes VK_OEM_2 (same as US
          // keyboard layout) but inputs "." (period of ASCII).  Therefore,
          // we return DOM_VK_PERIOD which is same as VK_OEM_PERIOD for
          // "Period" key.  On the other hand, we use same keyCode value for
          // "Slash" key too because it inputs ".".
          CodeNameIndex code;
          switch (aNativeKeyCode) {
            case VK_OEM_1:
              code = CODE_NAME_INDEX_Semicolon;
              break;
            case VK_OEM_PLUS:
              code = CODE_NAME_INDEX_Equal;
              break;
            case VK_OEM_COMMA:
              code = CODE_NAME_INDEX_Comma;
              break;
            case VK_OEM_MINUS:
              code = CODE_NAME_INDEX_Minus;
              break;
            case VK_OEM_PERIOD:
              code = CODE_NAME_INDEX_Period;
              break;
            case VK_OEM_2:
              code = CODE_NAME_INDEX_Slash;
              break;
            case VK_OEM_3:
              code = CODE_NAME_INDEX_Backquote;
              break;
            case VK_OEM_4:
              code = CODE_NAME_INDEX_BracketLeft;
              break;
            case VK_OEM_5:
              code = CODE_NAME_INDEX_Backslash;
              break;
            case VK_OEM_6:
              code = CODE_NAME_INDEX_BracketRight;
              break;
            case VK_OEM_7:
              code = CODE_NAME_INDEX_Quote;
              break;
            case VK_OEM_8:
              // Use keyCode value for "Backquote" key on UK keyboard layout.
              code = CODE_NAME_INDEX_Backquote;
              break;
            case VK_OEM_102:
              // Use keyCode value for "IntlBackslash" key.
              code = CODE_NAME_INDEX_IntlBackslash;
              break;
            case VK_ABNT_C1:  // "/" of ABNT.
              // Use keyCode value for "IntlBackslash" key on ABNT keyboard
              // layout.
              code = CODE_NAME_INDEX_IntlBackslash;
              break;
            default:
              MOZ_ASSERT_UNREACHABLE("Handle all OEM keycode values");
              return 0;
          }
          return WidgetKeyboardEvent::GetFallbackKeyCodeOfPunctuationKey(code);
        }
      }
      return WidgetUtils::ComputeKeyCodeFromChar(uniChars.CharAt(0));
    }

    // IE sets 0xC2 to the DOM keyCode for VK_ABNT_C2.  However, we're already
    // using NS_VK_SEPARATOR for the separator key on Mac and Linux.  Therefore,
    // We should keep consistency between Gecko on all platforms rather than
    // with other browsers since a lot of keyCode values are already different
    // between browsers.
    case VK_ABNT_C2:
      return NS_VK_SEPARATOR;

    // VK_PROCESSKEY means IME already consumed the key event.
    case VK_PROCESSKEY:
      return NS_VK_PROCESSKEY;
    // VK_PACKET is generated by SendInput() API, we don't need to
    // care this message as key event.
    case VK_PACKET:
      return 0;
    // If a key is not mapped to a virtual keycode, 0xFF is used.
    case 0xFF:
      NS_WARNING("The key is failed to be converted to a virtual keycode");
      return 0;
  }
#ifdef DEBUG
  nsPrintfCString warning(
      "Unknown virtual keycode (0x%08X), please check the "
      "latest MSDN document, there may be some new "
      "keycodes we've never known.",
      aNativeKeyCode);
  NS_WARNING(warning.get());
#endif
  return 0;
}

KeyNameIndex KeyboardLayout::ConvertNativeKeyCodeToKeyNameIndex(
    uint8_t aVirtualKey) const {
  if (IsPrintableCharKey(aVirtualKey) || aVirtualKey == VK_PACKET) {
    return KEY_NAME_INDEX_USE_STRING;
  }

  // If the keyboard layout has AltGr and AltRight key is pressed,
  // return AltGraph.
  if (aVirtualKey == VK_RMENU && HasAltGr()) {
    return KEY_NAME_INDEX_AltGraph;
  }

  switch (aVirtualKey) {
#undef NS_NATIVE_KEY_TO_DOM_KEY_NAME_INDEX
#define NS_NATIVE_KEY_TO_DOM_KEY_NAME_INDEX(aNativeKey, aKeyNameIndex) \
  case aNativeKey:                                                     \
    return aKeyNameIndex;

#include "NativeKeyToDOMKeyName.h"

#undef NS_NATIVE_KEY_TO_DOM_KEY_NAME_INDEX

    default:
      break;
  }

  HKL layout = KeyboardLayout::GetLayout();
  WORD langID = LOWORD(static_cast<HKL>(layout));
  WORD primaryLangID = PRIMARYLANGID(langID);

  if (primaryLangID == LANG_JAPANESE) {
    switch (aVirtualKey) {
#undef NS_JAPANESE_NATIVE_KEY_TO_DOM_KEY_NAME_INDEX
#define NS_JAPANESE_NATIVE_KEY_TO_DOM_KEY_NAME_INDEX(aNativeKey,    \
                                                     aKeyNameIndex) \
  case aNativeKey:                                                  \
    return aKeyNameIndex;

#include "NativeKeyToDOMKeyName.h"

#undef NS_JAPANESE_NATIVE_KEY_TO_DOM_KEY_NAME_INDEX

      default:
        break;
    }
  } else if (primaryLangID == LANG_KOREAN) {
    switch (aVirtualKey) {
#undef NS_KOREAN_NATIVE_KEY_TO_DOM_KEY_NAME_INDEX
#define NS_KOREAN_NATIVE_KEY_TO_DOM_KEY_NAME_INDEX(aNativeKey, aKeyNameIndex) \
  case aNativeKey:                                                            \
    return aKeyNameIndex;

#include "NativeKeyToDOMKeyName.h"

#undef NS_KOREAN_NATIVE_KEY_TO_DOM_KEY_NAME_INDEX

      default:
        return KEY_NAME_INDEX_Unidentified;
    }
  }

  switch (aVirtualKey) {
#undef NS_OTHER_NATIVE_KEY_TO_DOM_KEY_NAME_INDEX
#define NS_OTHER_NATIVE_KEY_TO_DOM_KEY_NAME_INDEX(aNativeKey, aKeyNameIndex) \
  case aNativeKey:                                                           \
    return aKeyNameIndex;

#include "NativeKeyToDOMKeyName.h"

#undef NS_OTHER_NATIVE_KEY_TO_DOM_KEY_NAME_INDEX

    default:
      return KEY_NAME_INDEX_Unidentified;
  }
}

// static
CodeNameIndex KeyboardLayout::ConvertScanCodeToCodeNameIndex(UINT aScanCode) {
  switch (aScanCode) {
#define NS_NATIVE_KEY_TO_DOM_CODE_NAME_INDEX(aNativeKey, aCodeNameIndex) \
  case aNativeKey:                                                       \
    return aCodeNameIndex;

#include "NativeKeyToDOMCodeName.h"

#undef NS_NATIVE_KEY_TO_DOM_CODE_NAME_INDEX

    default:
      return CODE_NAME_INDEX_UNKNOWN;
  }
}

nsresult KeyboardLayout::SynthesizeNativeKeyEvent(
    nsWindow* aWidget, int32_t aNativeKeyboardLayout, int32_t aNativeKeyCode,
    uint32_t aModifierFlags, const nsAString& aCharacters,
    const nsAString& aUnmodifiedCharacters) {
  UINT keyboardLayoutListCount = ::GetKeyboardLayoutList(0, nullptr);
  NS_ASSERTION(keyboardLayoutListCount > 0,
               "One keyboard layout must be installed at least");
  HKL keyboardLayoutListBuff[50];
  HKL* keyboardLayoutList = keyboardLayoutListCount < 50
                                ? keyboardLayoutListBuff
                                : new HKL[keyboardLayoutListCount];
  keyboardLayoutListCount =
      ::GetKeyboardLayoutList(keyboardLayoutListCount, keyboardLayoutList);
  NS_ASSERTION(keyboardLayoutListCount > 0,
               "Failed to get all keyboard layouts installed on the system");

  nsPrintfCString layoutName("%08x", aNativeKeyboardLayout);
  HKL loadedLayout = LoadKeyboardLayoutA(layoutName.get(), KLF_NOTELLSHELL);
  if (loadedLayout == nullptr) {
    if (keyboardLayoutListBuff != keyboardLayoutList) {
      delete[] keyboardLayoutList;
    }
    return NS_ERROR_NOT_AVAILABLE;
  }

  // Setup clean key state and load desired layout
  BYTE originalKbdState[256];
  ::GetKeyboardState(originalKbdState);
  BYTE kbdState[256];
  memset(kbdState, 0, sizeof(kbdState));
  // This changes the state of the keyboard for the current thread only,
  // and we'll restore it soon, so this should be OK.
  ::SetKeyboardState(kbdState);

  OverrideLayout(loadedLayout);

  bool isAltGrKeyPress = false;
  if (aModifierFlags & nsIWidget::ALTGRAPH) {
    if (!HasAltGr()) {
      return NS_ERROR_INVALID_ARG;
    }
    // AltGr emulates ControlLeft key press and AltRight key press.
    // So, we should remove those flags from aModifierFlags before
    // calling WinUtils::SetupKeyModifiersSequence() to create correct
    // key sequence.
    // FYI: We don't support both ControlLeft and AltRight (AltGr) are
    //      pressed at the same time unless synthesizing key is
    //      VK_LCONTROL.
    aModifierFlags &= ~(nsIWidget::CTRL_L | nsIWidget::ALT_R);
  }

  uint8_t argumentKeySpecific = 0;
  switch (aNativeKeyCode & 0xFF) {
    case VK_SHIFT:
      aModifierFlags &= ~(nsIWidget::SHIFT_L | nsIWidget::SHIFT_R);
      argumentKeySpecific = VK_LSHIFT;
      break;
    case VK_LSHIFT:
      aModifierFlags &= ~nsIWidget::SHIFT_L;
      argumentKeySpecific = aNativeKeyCode & 0xFF;
      aNativeKeyCode = (aNativeKeyCode & 0xFFFF0000) | VK_SHIFT;
      break;
    case VK_RSHIFT:
      aModifierFlags &= ~nsIWidget::SHIFT_R;
      argumentKeySpecific = aNativeKeyCode & 0xFF;
      aNativeKeyCode = (aNativeKeyCode & 0xFFFF0000) | VK_SHIFT;
      break;
    case VK_CONTROL:
      aModifierFlags &= ~(nsIWidget::CTRL_L | nsIWidget::CTRL_R);
      argumentKeySpecific = VK_LCONTROL;
      break;
    case VK_LCONTROL:
      aModifierFlags &= ~nsIWidget::CTRL_L;
      argumentKeySpecific = aNativeKeyCode & 0xFF;
      aNativeKeyCode = (aNativeKeyCode & 0xFFFF0000) | VK_CONTROL;
      break;
    case VK_RCONTROL:
      aModifierFlags &= ~nsIWidget::CTRL_R;
      argumentKeySpecific = aNativeKeyCode & 0xFF;
      aNativeKeyCode = (aNativeKeyCode & 0xFFFF0000) | VK_CONTROL;
      break;
    case VK_MENU:
      aModifierFlags &= ~(nsIWidget::ALT_L | nsIWidget::ALT_R);
      argumentKeySpecific = VK_LMENU;
      break;
    case VK_LMENU:
      aModifierFlags &= ~nsIWidget::ALT_L;
      argumentKeySpecific = aNativeKeyCode & 0xFF;
      aNativeKeyCode = (aNativeKeyCode & 0xFFFF0000) | VK_MENU;
      break;
    case VK_RMENU:
      aModifierFlags &= ~(nsIWidget::ALT_R | nsIWidget::ALTGRAPH);
      argumentKeySpecific = aNativeKeyCode & 0xFF;
      aNativeKeyCode = (aNativeKeyCode & 0xFFFF0000) | VK_MENU;
      // If AltRight key is AltGr in the keyboard layout, let's use
      // SetupKeyModifiersSequence() to emulate the native behavior
      // since the same event order between keydown and keyup makes
      // the following code complicated.
      if (HasAltGr()) {
        isAltGrKeyPress = true;
        aModifierFlags &= ~nsIWidget::CTRL_L;
        aModifierFlags |= nsIWidget::ALTGRAPH;
      }
      break;
    case VK_CAPITAL:
      aModifierFlags &= ~nsIWidget::CAPS_LOCK;
      argumentKeySpecific = VK_CAPITAL;
      break;
    case VK_NUMLOCK:
      aModifierFlags &= ~nsIWidget::NUM_LOCK;
      argumentKeySpecific = VK_NUMLOCK;
      break;
  }

  AutoTArray<KeyPair, 10> keySequence;
  WinUtils::SetupKeyModifiersSequence(&keySequence, aModifierFlags, WM_KEYDOWN);
  if (!isAltGrKeyPress) {
    keySequence.AppendElement(KeyPair(aNativeKeyCode, argumentKeySpecific));
  }

  // Simulate the pressing of each modifier key and then the real key
  // FYI: Each NativeKey instance here doesn't need to override keyboard layout
  //      since this method overrides and restores the keyboard layout.
  for (uint32_t i = 0; i < keySequence.Length(); ++i) {
    uint8_t key = keySequence[i].mGeneral;
    uint8_t keySpecific = keySequence[i].mSpecific;
    uint16_t scanCode = keySequence[i].mScanCode;
    kbdState[key] = 0x81;  // key is down and toggled on if appropriate
    if (keySpecific) {
      kbdState[keySpecific] = 0x81;
    }
    ::SetKeyboardState(kbdState);
    ModifierKeyState modKeyState;
    // If scan code isn't specified explicitly, let's compute it with current
    // keyboard layout.
    if (!scanCode) {
      scanCode =
          ComputeScanCodeForVirtualKeyCode(keySpecific ? keySpecific : key);
    }
    LPARAM lParam = static_cast<LPARAM>(scanCode << 16);
    // If the scan code is for an extended key, set extended key flag.
    if ((scanCode & 0xFF00) == 0xE000) {
      lParam |= 0x1000000;
    }
    // When AltGr key is pressed, both ControlLeft and AltRight cause
    // WM_KEYDOWN messages.
    bool makeSysKeyMsg =
        !(aModifierFlags & nsIWidget::ALTGRAPH) && IsSysKey(key, modKeyState);
    MSG keyDownMsg =
        WinUtils::InitMSG(makeSysKeyMsg ? WM_SYSKEYDOWN : WM_KEYDOWN, key,
                          lParam, aWidget->GetWindowHandle());
    if (i == keySequence.Length() - 1) {
      bool makeDeadCharMsg =
          (IsDeadKey(key, modKeyState) && aCharacters.IsEmpty());
      nsAutoString chars(aCharacters);
      if (makeDeadCharMsg) {
        UniCharsAndModifiers deadChars =
            GetUniCharsAndModifiers(key, modKeyState);
        chars = deadChars.ToString();
        NS_ASSERTION(chars.Length() == 1,
                     "Dead char must be only one character");
      }
      if (chars.IsEmpty()) {
        NativeKey nativeKey(aWidget, keyDownMsg, modKeyState);
        nativeKey.HandleKeyDownMessage();
      } else {
        AutoTArray<NativeKey::FakeCharMsg, 10> fakeCharMsgs;
        for (uint32_t j = 0; j < chars.Length(); j++) {
          NativeKey::FakeCharMsg* fakeCharMsg = fakeCharMsgs.AppendElement();
          fakeCharMsg->mCharCode = chars.CharAt(j);
          fakeCharMsg->mScanCode = scanCode;
          fakeCharMsg->mIsSysKey = makeSysKeyMsg;
          fakeCharMsg->mIsDeadKey = makeDeadCharMsg;
        }
        NativeKey nativeKey(aWidget, keyDownMsg, modKeyState, 0, &fakeCharMsgs);
        bool dispatched;
        nativeKey.HandleKeyDownMessage(&dispatched);
        // If some char messages are not consumed, let's emulate the widget
        // receiving the message directly.
        for (uint32_t j = 1; j < fakeCharMsgs.Length(); j++) {
          if (fakeCharMsgs[j].mConsumed) {
            continue;
          }
          MSG charMsg = fakeCharMsgs[j].GetCharMsg(aWidget->GetWindowHandle());
          NativeKey nativeKey(aWidget, charMsg, modKeyState);
          nativeKey.HandleCharMessage(charMsg);
        }
      }
    } else {
      NativeKey nativeKey(aWidget, keyDownMsg, modKeyState);
      nativeKey.HandleKeyDownMessage();
    }
  }

  keySequence.Clear();
  if (!isAltGrKeyPress) {
    keySequence.AppendElement(KeyPair(aNativeKeyCode, argumentKeySpecific));
  }
  WinUtils::SetupKeyModifiersSequence(&keySequence, aModifierFlags, WM_KEYUP);
  for (uint32_t i = 0; i < keySequence.Length(); ++i) {
    uint8_t key = keySequence[i].mGeneral;
    uint8_t keySpecific = keySequence[i].mSpecific;
    uint16_t scanCode = keySequence[i].mScanCode;
    kbdState[key] = 0;  // key is up and toggled off if appropriate
    if (keySpecific) {
      kbdState[keySpecific] = 0;
    }
    ::SetKeyboardState(kbdState);
    ModifierKeyState modKeyState;
    // If scan code isn't specified explicitly, let's compute it with current
    // keyboard layout.
    if (!scanCode) {
      scanCode =
          ComputeScanCodeForVirtualKeyCode(keySpecific ? keySpecific : key);
    }
    LPARAM lParam = static_cast<LPARAM>(scanCode << 16);
    // If the scan code is for an extended key, set extended key flag.
    if ((scanCode & 0xFF00) == 0xE000) {
      lParam |= 0x1000000;
    }
    // Don't use WM_SYSKEYUP for Alt keyup.
    // NOTE: When AltGr was pressed, ControlLeft causes WM_SYSKEYUP normally.
    bool makeSysKeyMsg = IsSysKey(key, modKeyState) && key != VK_MENU;
    MSG keyUpMsg = WinUtils::InitMSG(makeSysKeyMsg ? WM_SYSKEYUP : WM_KEYUP,
                                     key, lParam, aWidget->GetWindowHandle());
    NativeKey nativeKey(aWidget, keyUpMsg, modKeyState);
    nativeKey.HandleKeyUpMessage();
  }

  // Restore old key state and layout
  ::SetKeyboardState(originalKbdState);
  RestoreLayout();

  // Don't unload the layout if it's installed actually.
  for (uint32_t i = 0; i < keyboardLayoutListCount; i++) {
    if (keyboardLayoutList[i] == loadedLayout) {
      loadedLayout = 0;
      break;
    }
  }
  if (keyboardLayoutListBuff != keyboardLayoutList) {
    delete[] keyboardLayoutList;
  }
  if (loadedLayout) {
    ::UnloadKeyboardLayout(loadedLayout);
  }
  return NS_OK;
}

/*****************************************************************************
 * mozilla::widget::RedirectedKeyDownMessage
 *****************************************************************************/

MSG RedirectedKeyDownMessageManager::sRedirectedKeyDownMsg;
bool RedirectedKeyDownMessageManager::sDefaultPreventedOfRedirectedMsg = false;

// static
bool RedirectedKeyDownMessageManager::IsRedirectedMessage(const MSG& aMsg) {
  return (aMsg.message == WM_KEYDOWN || aMsg.message == WM_SYSKEYDOWN) &&
         (sRedirectedKeyDownMsg.message == aMsg.message &&
          WinUtils::GetScanCode(sRedirectedKeyDownMsg.lParam) ==
              WinUtils::GetScanCode(aMsg.lParam));
}

// static
void RedirectedKeyDownMessageManager::RemoveNextCharMessage(HWND aWnd) {
  MSG msg;
  if (WinUtils::PeekMessage(&msg, aWnd, WM_KEYFIRST, WM_KEYLAST,
                            PM_NOREMOVE | PM_NOYIELD) &&
      (msg.message == WM_CHAR || msg.message == WM_SYSCHAR)) {
    WinUtils::PeekMessage(&msg, aWnd, msg.message, msg.message,
                          PM_REMOVE | PM_NOYIELD);
  }
}

}  // namespace widget
}  // namespace mozilla
