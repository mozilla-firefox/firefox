/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "GeckoEditableSupport.h"

#include <android/api-level.h>
#include <android/input.h>
#include <android/log.h>

#include "AndroidBridgeUtilities.h"
#include "AndroidRect.h"
#include "KeyEvent.h"
#include "PuppetWidget.h"
#include "mozilla/IMEStateManager.h"
#include "mozilla/Logging.h"
#include "mozilla/MiscEvents.h"
#include "mozilla/Preferences.h"
#include "mozilla/StaticPrefs_intl.h"
#include "mozilla/TextComposition.h"
#include "mozilla/TextEventDispatcherListener.h"
#include "mozilla/TextEvents.h"
#include "mozilla/ToString.h"
#include "mozilla/dom/BrowserChild.h"
#include "mozilla/dom/ContentChild.h"
#include "mozilla/java/GeckoEditableChildWrappers.h"
#include "mozilla/java/GeckoServiceChildProcessWrappers.h"
#include "mozilla/jni/NativesInlines.h"
#include "mozilla/widget/GeckoViewSupport.h"
#include "nsComponentManagerUtils.h"
#include "nsIContent.h"
#include "nsITransferable.h"
#include "nsStringStream.h"
#include "nsWindow.h"

#ifdef NIGHTLY_BUILD
static mozilla::LazyLogModule sGeckoEditableSupportLog("IMEHandler");
#  define ALOGIME(...) \
    MOZ_LOG(sGeckoEditableSupportLog, LogLevel::Debug, (__VA_ARGS__))
#else
#  define ALOGIME(args...) \
    do {                   \
    } while (0)
#endif

namespace mozilla {
namespace widget {

static uint32_t ConvertAndroidKeyCodeToDOMKeyCode(int32_t androidKeyCode) {
  // Special-case alphanumeric keycodes because they are most common.
  if (androidKeyCode >= AKEYCODE_A && androidKeyCode <= AKEYCODE_Z) {
    return androidKeyCode - AKEYCODE_A + NS_VK_A;
  }

  if (androidKeyCode >= AKEYCODE_0 && androidKeyCode <= AKEYCODE_9) {
    return androidKeyCode - AKEYCODE_0 + NS_VK_0;
  }

  switch (androidKeyCode) {
    // KEYCODE_UNKNOWN (0) ... KEYCODE_HOME (3)
    case AKEYCODE_BACK:
      return NS_VK_ESCAPE;
    // KEYCODE_CALL (5) ... KEYCODE_POUND (18)
    case AKEYCODE_DPAD_UP:
      return NS_VK_UP;
    case AKEYCODE_DPAD_DOWN:
      return NS_VK_DOWN;
    case AKEYCODE_DPAD_LEFT:
      return NS_VK_LEFT;
    case AKEYCODE_DPAD_RIGHT:
      return NS_VK_RIGHT;
    case AKEYCODE_DPAD_CENTER:
      return NS_VK_RETURN;
    case AKEYCODE_VOLUME_UP:
      return NS_VK_VOLUME_UP;
    case AKEYCODE_VOLUME_DOWN:
      return NS_VK_VOLUME_DOWN;
    // KEYCODE_VOLUME_POWER (26) ... KEYCODE_Z (54)
    case AKEYCODE_COMMA:
      return NS_VK_COMMA;
    case AKEYCODE_PERIOD:
      return NS_VK_PERIOD;
    case AKEYCODE_ALT_LEFT:
      return NS_VK_ALT;
    case AKEYCODE_ALT_RIGHT:
      return NS_VK_ALT;
    case AKEYCODE_SHIFT_LEFT:
      return NS_VK_SHIFT;
    case AKEYCODE_SHIFT_RIGHT:
      return NS_VK_SHIFT;
    case AKEYCODE_TAB:
      return NS_VK_TAB;
    case AKEYCODE_SPACE:
      return NS_VK_SPACE;
    // KEYCODE_SYM (63) ... KEYCODE_ENVELOPE (65)
    case AKEYCODE_ENTER:
      return NS_VK_RETURN;
    case AKEYCODE_DEL:
      return NS_VK_BACK;  // Backspace
    case AKEYCODE_GRAVE:
      return NS_VK_BACK_QUOTE;
    // KEYCODE_MINUS (69)
    case AKEYCODE_EQUALS:
      return NS_VK_EQUALS;
    case AKEYCODE_LEFT_BRACKET:
      return NS_VK_OPEN_BRACKET;
    case AKEYCODE_RIGHT_BRACKET:
      return NS_VK_CLOSE_BRACKET;
    case AKEYCODE_BACKSLASH:
      return NS_VK_BACK_SLASH;
    case AKEYCODE_SEMICOLON:
      return NS_VK_SEMICOLON;
    // KEYCODE_APOSTROPHE (75)
    case AKEYCODE_SLASH:
      return NS_VK_SLASH;
    // KEYCODE_AT (77) ... KEYCODE_MEDIA_FAST_FORWARD (90)
    case AKEYCODE_MUTE:
      return NS_VK_VOLUME_MUTE;
    case AKEYCODE_PAGE_UP:
      return NS_VK_PAGE_UP;
    case AKEYCODE_PAGE_DOWN:
      return NS_VK_PAGE_DOWN;
    // KEYCODE_PICTSYMBOLS (94) ... KEYCODE_BUTTON_MODE (110)
    case AKEYCODE_ESCAPE:
      return NS_VK_ESCAPE;
    case AKEYCODE_FORWARD_DEL:
      return NS_VK_DELETE;
    case AKEYCODE_CTRL_LEFT:
      return NS_VK_CONTROL;
    case AKEYCODE_CTRL_RIGHT:
      return NS_VK_CONTROL;
    case AKEYCODE_CAPS_LOCK:
      return NS_VK_CAPS_LOCK;
    case AKEYCODE_SCROLL_LOCK:
      return NS_VK_SCROLL_LOCK;
    // KEYCODE_META_LEFT (117) ... KEYCODE_FUNCTION (119)
    case AKEYCODE_SYSRQ:
      return NS_VK_PRINTSCREEN;
    case AKEYCODE_BREAK:
      return NS_VK_PAUSE;
    case AKEYCODE_MOVE_HOME:
      return NS_VK_HOME;
    case AKEYCODE_MOVE_END:
      return NS_VK_END;
    case AKEYCODE_INSERT:
      return NS_VK_INSERT;
    // KEYCODE_FORWARD (125) ... KEYCODE_MEDIA_RECORD (130)
    case AKEYCODE_F1:
      return NS_VK_F1;
    case AKEYCODE_F2:
      return NS_VK_F2;
    case AKEYCODE_F3:
      return NS_VK_F3;
    case AKEYCODE_F4:
      return NS_VK_F4;
    case AKEYCODE_F5:
      return NS_VK_F5;
    case AKEYCODE_F6:
      return NS_VK_F6;
    case AKEYCODE_F7:
      return NS_VK_F7;
    case AKEYCODE_F8:
      return NS_VK_F8;
    case AKEYCODE_F9:
      return NS_VK_F9;
    case AKEYCODE_F10:
      return NS_VK_F10;
    case AKEYCODE_F11:
      return NS_VK_F11;
    case AKEYCODE_F12:
      return NS_VK_F12;
    case AKEYCODE_NUM_LOCK:
      return NS_VK_NUM_LOCK;
    case AKEYCODE_NUMPAD_0:
      return NS_VK_NUMPAD0;
    case AKEYCODE_NUMPAD_1:
      return NS_VK_NUMPAD1;
    case AKEYCODE_NUMPAD_2:
      return NS_VK_NUMPAD2;
    case AKEYCODE_NUMPAD_3:
      return NS_VK_NUMPAD3;
    case AKEYCODE_NUMPAD_4:
      return NS_VK_NUMPAD4;
    case AKEYCODE_NUMPAD_5:
      return NS_VK_NUMPAD5;
    case AKEYCODE_NUMPAD_6:
      return NS_VK_NUMPAD6;
    case AKEYCODE_NUMPAD_7:
      return NS_VK_NUMPAD7;
    case AKEYCODE_NUMPAD_8:
      return NS_VK_NUMPAD8;
    case AKEYCODE_NUMPAD_9:
      return NS_VK_NUMPAD9;
    case AKEYCODE_NUMPAD_DIVIDE:
      return NS_VK_DIVIDE;
    case AKEYCODE_NUMPAD_MULTIPLY:
      return NS_VK_MULTIPLY;
    case AKEYCODE_NUMPAD_SUBTRACT:
      return NS_VK_SUBTRACT;
    case AKEYCODE_NUMPAD_ADD:
      return NS_VK_ADD;
    case AKEYCODE_NUMPAD_DOT:
      return NS_VK_DECIMAL;
    case AKEYCODE_NUMPAD_COMMA:
      return NS_VK_SEPARATOR;
    case AKEYCODE_NUMPAD_ENTER:
      return NS_VK_RETURN;
    case AKEYCODE_NUMPAD_EQUALS:
      return NS_VK_EQUALS;
    // KEYCODE_NUMPAD_LEFT_PAREN (162) ... KEYCODE_CALCULATOR (210)

    // Needs to confirm the behavior.  If the key switches the open state
    // of Japanese IME (or switches input character between Hiragana and
    // Roman numeric characters), then, it might be better to use
    // NS_VK_KANJI which is used for Alt+Zenkaku/Hankaku key on Windows.
    case AKEYCODE_ZENKAKU_HANKAKU:
      return 0;
    case AKEYCODE_EISU:
      return NS_VK_EISU;
    case AKEYCODE_MUHENKAN:
      return NS_VK_NONCONVERT;
    case AKEYCODE_HENKAN:
      return NS_VK_CONVERT;
    case AKEYCODE_KATAKANA_HIRAGANA:
      return 0;
    case AKEYCODE_YEN:
      return NS_VK_BACK_SLASH;  // Same as other platforms.
    case AKEYCODE_RO:
      return NS_VK_BACK_SLASH;  // Same as other platforms.
    case AKEYCODE_KANA:
      return NS_VK_KANA;
    case AKEYCODE_ASSIST:
      return NS_VK_HELP;

    // the A key is the action key for gamepad devices.
    case AKEYCODE_BUTTON_A:
      return NS_VK_RETURN;

    default:
      ALOG(
          "ConvertAndroidKeyCodeToDOMKeyCode: "
          "No DOM keycode for Android keycode %d",
          int(androidKeyCode));
      return 0;
  }
}

static KeyNameIndex ConvertAndroidKeyCodeToKeyNameIndex(
    int32_t keyCode, int32_t action, int32_t domPrintableKeyValue) {
  // Special-case alphanumeric keycodes because they are most common.
  if (keyCode >= AKEYCODE_A && keyCode <= AKEYCODE_Z) {
    return KEY_NAME_INDEX_USE_STRING;
  }

  if (keyCode >= AKEYCODE_0 && keyCode <= AKEYCODE_9) {
    return KEY_NAME_INDEX_USE_STRING;
  }

  switch (keyCode) {
#define NS_NATIVE_KEY_TO_DOM_KEY_NAME_INDEX(aNativeKey, aKeyNameIndex) \
  case aNativeKey:                                                     \
    return aKeyNameIndex;

#include "NativeKeyToDOMKeyName.inc"

#undef NS_NATIVE_KEY_TO_DOM_KEY_NAME_INDEX

    // KEYCODE_0 (7) ... KEYCODE_9 (16)
    case AKEYCODE_STAR:   // '*' key
    case AKEYCODE_POUND:  // '#' key

      // KEYCODE_A (29) ... KEYCODE_Z (54)

    case AKEYCODE_COMMA:   // ',' key
    case AKEYCODE_PERIOD:  // '.' key
    case AKEYCODE_SPACE:
    case AKEYCODE_GRAVE:          // '`' key
    case AKEYCODE_MINUS:          // '-' key
    case AKEYCODE_EQUALS:         // '=' key
    case AKEYCODE_LEFT_BRACKET:   // '[' key
    case AKEYCODE_RIGHT_BRACKET:  // ']' key
    case AKEYCODE_BACKSLASH:      // '\' key
    case AKEYCODE_SEMICOLON:      // ';' key
    case AKEYCODE_APOSTROPHE:     // ''' key
    case AKEYCODE_SLASH:          // '/' key
    case AKEYCODE_AT:             // '@' key
    case AKEYCODE_PLUS:           // '+' key

    case AKEYCODE_NUMPAD_0:
    case AKEYCODE_NUMPAD_1:
    case AKEYCODE_NUMPAD_2:
    case AKEYCODE_NUMPAD_3:
    case AKEYCODE_NUMPAD_4:
    case AKEYCODE_NUMPAD_5:
    case AKEYCODE_NUMPAD_6:
    case AKEYCODE_NUMPAD_7:
    case AKEYCODE_NUMPAD_8:
    case AKEYCODE_NUMPAD_9:
    case AKEYCODE_NUMPAD_DIVIDE:
    case AKEYCODE_NUMPAD_MULTIPLY:
    case AKEYCODE_NUMPAD_SUBTRACT:
    case AKEYCODE_NUMPAD_ADD:
    case AKEYCODE_NUMPAD_DOT:
    case AKEYCODE_NUMPAD_COMMA:
    case AKEYCODE_NUMPAD_EQUALS:
    case AKEYCODE_NUMPAD_LEFT_PAREN:
    case AKEYCODE_NUMPAD_RIGHT_PAREN:

    case AKEYCODE_YEN:  // yen sign key
    case AKEYCODE_RO:   // Japanese Ro key
      return KEY_NAME_INDEX_USE_STRING;

    case AKEYCODE_NUM:  // XXX Not sure
    case AKEYCODE_PICTSYMBOLS:

    case AKEYCODE_BUTTON_A:
    case AKEYCODE_BUTTON_B:
    case AKEYCODE_BUTTON_C:
    case AKEYCODE_BUTTON_X:
    case AKEYCODE_BUTTON_Y:
    case AKEYCODE_BUTTON_Z:
    case AKEYCODE_BUTTON_L1:
    case AKEYCODE_BUTTON_R1:
    case AKEYCODE_BUTTON_L2:
    case AKEYCODE_BUTTON_R2:
    case AKEYCODE_BUTTON_THUMBL:
    case AKEYCODE_BUTTON_THUMBR:
    case AKEYCODE_BUTTON_START:
    case AKEYCODE_BUTTON_SELECT:
    case AKEYCODE_BUTTON_MODE:

    case AKEYCODE_MEDIA_CLOSE:

    case AKEYCODE_BUTTON_1:
    case AKEYCODE_BUTTON_2:
    case AKEYCODE_BUTTON_3:
    case AKEYCODE_BUTTON_4:
    case AKEYCODE_BUTTON_5:
    case AKEYCODE_BUTTON_6:
    case AKEYCODE_BUTTON_7:
    case AKEYCODE_BUTTON_8:
    case AKEYCODE_BUTTON_9:
    case AKEYCODE_BUTTON_10:
    case AKEYCODE_BUTTON_11:
    case AKEYCODE_BUTTON_12:
    case AKEYCODE_BUTTON_13:
    case AKEYCODE_BUTTON_14:
    case AKEYCODE_BUTTON_15:
    case AKEYCODE_BUTTON_16:
      return KEY_NAME_INDEX_Unidentified;

    case AKEYCODE_UNKNOWN:
      MOZ_ASSERT(action != AKEY_EVENT_ACTION_MULTIPLE,
                 "Don't call this when action is AKEY_EVENT_ACTION_MULTIPLE!");
      // It's actually an unknown key if the action isn't ACTION_MULTIPLE.
      // However, it might cause text input.  So, let's check the value.
      return domPrintableKeyValue ? KEY_NAME_INDEX_USE_STRING
                                  : KEY_NAME_INDEX_Unidentified;

    default:
      ALOG(
          "ConvertAndroidKeyCodeToKeyNameIndex: "
          "No DOM key name index for Android keycode %d",
          keyCode);
      return KEY_NAME_INDEX_Unidentified;
  }
}

static CodeNameIndex ConvertAndroidScanCodeToCodeNameIndex(int32_t scanCode) {
  switch (scanCode) {
#define NS_NATIVE_KEY_TO_DOM_CODE_NAME_INDEX(aNativeKey, aCodeNameIndex) \
  case aNativeKey:                                                       \
    return aCodeNameIndex;

#include "NativeKeyToDOMCodeName.inc"

#undef NS_NATIVE_KEY_TO_DOM_CODE_NAME_INDEX

    default:
      return CODE_NAME_INDEX_UNKNOWN;
  }
}

static void InitKeyEvent(WidgetKeyboardEvent& aEvent, int32_t aAction,
                         int32_t aKeyCode, int32_t aScanCode,
                         int32_t aMetaState, int64_t aTime,
                         int32_t aDomPrintableKeyValue, int32_t aRepeatCount,
                         int32_t aFlags) {
  const uint32_t domKeyCode = ConvertAndroidKeyCodeToDOMKeyCode(aKeyCode);

  aEvent.mModifiers = nsWindow::GetModifiers(aMetaState);
  aEvent.mKeyCode = domKeyCode;

  aEvent.mIsRepeat =
      (aEvent.mMessage == eKeyDown || aEvent.mMessage == eKeyPress) &&
      ((aFlags & java::sdk::KeyEvent::FLAG_LONG_PRESS) || aRepeatCount);

  aEvent.mKeyNameIndex = ConvertAndroidKeyCodeToKeyNameIndex(
      aKeyCode, aAction, aDomPrintableKeyValue);
  aEvent.mCodeNameIndex = ConvertAndroidScanCodeToCodeNameIndex(aScanCode);

  if (aEvent.mKeyNameIndex == KEY_NAME_INDEX_USE_STRING &&
      aDomPrintableKeyValue) {
    aEvent.mKeyValue = char16_t(aDomPrintableKeyValue);
  }

  aEvent.mLocation =
      WidgetKeyboardEvent::ComputeLocationFromCodeValue(aEvent.mCodeNameIndex);
  aEvent.mTimeStamp = nsWindow::GetEventTimeStamp(aTime);
}

static nscolor ConvertAndroidColor(uint32_t aArgb) {
  return NS_RGBA((aArgb & 0x00ff0000) >> 16, (aArgb & 0x0000ff00) >> 8,
                 (aArgb & 0x000000ff), (aArgb & 0xff000000) >> 24);
}

static jni::ObjectArray::LocalRef ConvertRectArrayToJavaRectFArray(
    const nsTArray<LayoutDeviceIntRect>& aRects) {
  const size_t length = aRects.Length();
  auto rects = jni::ObjectArray::New<java::sdk::RectF>(length);

  for (size_t i = 0; i < length; i++) {
    const LayoutDeviceIntRect& tmp = aRects[i];

    auto rect = java::sdk::RectF::New(tmp.x, tmp.y, tmp.XMost(), tmp.YMost());
    rects->SetElement(i, rect);
  }
  return rects;
}

uint32_t GeckoEditableSupport::sUniqueKeyEventId = 0;

NS_IMPL_ISUPPORTS(GeckoEditableSupport, TextEventDispatcherListener,
                  nsISupportsWeakReference)

// This is the blocker helper class whether disposing GeckoEditableChild now.
// During JNI call from GeckoEditableChild, we shouldn't dispose it.
class MOZ_RAII AutoGeckoEditableBlocker final {
 public:
  explicit AutoGeckoEditableBlocker(GeckoEditableSupport* aGeckoEditableSupport)
      : mGeckoEditable(aGeckoEditableSupport) {
    mGeckoEditable->AddBlocker();
  }
  ~AutoGeckoEditableBlocker() { mGeckoEditable->ReleaseBlocker(); }

 private:
  RefPtr<GeckoEditableSupport> mGeckoEditable;
};

RefPtr<TextComposition> GeckoEditableSupport::GetComposition() const {
  nsCOMPtr<nsIWidget> widget = GetWidget();
  return widget ? IMEStateManager::GetTextCompositionFor(widget) : nullptr;
}

bool GeckoEditableSupport::RemoveComposition(RemoveCompositionFlag aFlag) {
  if (!mDispatcher || !mDispatcher->IsComposing()) {
    return false;
  }

  nsEventStatus status = nsEventStatus_eIgnore;
  NS_ENSURE_SUCCESS(BeginInputTransaction(mDispatcher), false);
  mDispatcher->CommitComposition(
      status, aFlag == CANCEL_IME_COMPOSITION ? &EmptyString() : nullptr);
  return true;
}

void GeckoEditableSupport::OnKeyEvent(
    int32_t aAction, int32_t aKeyCode, int32_t aScanCode, int32_t aMetaState,
    int32_t aKeyPressMetaState, int64_t aTime, int32_t aDomPrintableKeyValue,
    int32_t aRepeatCount, int32_t aFlags, bool aIsSynthesizedImeKey,
    bool aWaitingReply, jni::Object::Param aOriginalEvent) {
  AutoGeckoEditableBlocker blocker(this);

  nsCOMPtr<nsIWidget> widget = GetWidget();
  RefPtr<TextEventDispatcher> dispatcher =
      mDispatcher ? mDispatcher.get()
      : widget    ? widget->GetTextEventDispatcher()
                  : nullptr;
  NS_ENSURE_TRUE_VOID(dispatcher && widget);

  if (!aIsSynthesizedImeKey) {
    if (nsWindow* window = GetNsWindow()) {
      window->UserActivity();
    }
  } else if (aIsSynthesizedImeKey && mIMEMaskEventsCount > 0) {
    // Don't synthesize editor keys when not focused.
    return;
  }

  EventMessage msg;
  if (aAction == java::sdk::KeyEvent::ACTION_DOWN) {
    msg = eKeyDown;
  } else if (aAction == java::sdk::KeyEvent::ACTION_UP) {
    msg = eKeyUp;
  } else if (aAction == java::sdk::KeyEvent::ACTION_MULTIPLE) {
    // Keys with multiple action are handled in Java,
    // and we should never see one here
    MOZ_CRASH("Cannot handle key with multiple action");
  } else {
    NS_WARNING("Unknown key action event");
    return;
  }

  nsEventStatus status = nsEventStatus_eIgnore;
  WidgetKeyboardEvent event(true, msg, widget);
  InitKeyEvent(event, aAction, aKeyCode, aScanCode, aMetaState, aTime,
               aDomPrintableKeyValue, aRepeatCount, aFlags);

  if (nsIWidget::UsePuppetWidgets()) {
    // Don't use native key bindings.
    event.PreventNativeKeyBindings();
  }

  if (aIsSynthesizedImeKey) {
    // Keys synthesized by Java IME code are saved in the mIMEKeyEvents
    // array until the next IME_REPLACE_TEXT event, at which point
    // these keys are dispatched in sequence.
    mIMEKeyEvents.AppendElement(UniquePtr<WidgetEvent>(event.Duplicate()));
  } else {
    if (aWaitingReply) {
      event.MarkAsWaitingReplyFromRemoteProcess();
      event.mUniqueId = ++sUniqueKeyEventId;
    }
    NS_ENSURE_SUCCESS_VOID(BeginInputTransaction(dispatcher));
    dispatcher->DispatchKeyboardEvent(msg, event, status);
    if (widget->Destroyed() || status == nsEventStatus_eConsumeNoDefault) {
      // Skip default processing.
      return;
    }

    // If parent process, DispatchKeyboardEvent doesn't return status whether
    // processing default. If not handled, OnDefaultKeyEvent() will be called
    // when marked with waiting reply.
    if (aWaitingReply) {
      if (nsIWidget::UsePuppetWidgets()) {
        mEditable->OnDefaultKeyEvent(aOriginalEvent);
      } else {
        jni::Object::GlobalRef originalKeyEvent(aOriginalEvent);
        mWaitingReplyKeyEvents.AppendElement(
            WaitingReplyKeyEvent{sUniqueKeyEventId, aOriginalEvent});
      }
    }
  }

  // Only send keypress after keydown.
  if (msg != eKeyDown) {
    return;
  }

  WidgetKeyboardEvent pressEvent(true, eKeyPress, widget);
  InitKeyEvent(pressEvent, aAction, aKeyCode, aScanCode, aKeyPressMetaState,
               aTime, aDomPrintableKeyValue, aRepeatCount, aFlags);

  if (nsIWidget::UsePuppetWidgets()) {
    // Don't use native key bindings.
    pressEvent.PreventNativeKeyBindings();
  }

  if (aIsSynthesizedImeKey) {
    mIMEKeyEvents.AppendElement(UniquePtr<WidgetEvent>(pressEvent.Duplicate()));
  } else {
    dispatcher->MaybeDispatchKeypressEvents(pressEvent, status);
  }
}

/*
 * Send dummy key events for pages that are unaware of input events,
 * to provide web compatibility for pages that depend on key events.
 */
void GeckoEditableSupport::SendIMEDummyKeyEvent(nsIWidget* aWidget,
                                                EventMessage msg) {
  AutoGeckoEditableBlocker blocker(this);

  nsEventStatus status = nsEventStatus_eIgnore;
  MOZ_ASSERT(mDispatcher);

  WidgetKeyboardEvent event(true, msg, aWidget);
  // TODO: If we can know scan code of the key event which caused replacing
  //       composition string, we should set mCodeNameIndex here.  Then,
  //       we should rename this method because it becomes not a "dummy"
  //       keyboard event.
  event.mKeyCode = NS_VK_PROCESSKEY;
  event.mKeyNameIndex = KEY_NAME_INDEX_Process;
  // KeyboardEvents marked as "processed by IME" shouldn't cause any edit
  // actions.  So, we should set their native key binding to none before
  // dispatch to avoid crash on PuppetWidget and avoid running redundant
  // path to look for native key bindings.
  if (nsIWidget::UsePuppetWidgets()) {
    event.PreventNativeKeyBindings();
  }
  NS_ENSURE_SUCCESS_VOID(BeginInputTransaction(mDispatcher));
  mDispatcher->DispatchKeyboardEvent(msg, event, status);
}

void GeckoEditableSupport::AddIMETextChange(
    const IMENotification::TextChangeDataBase& aChange) {
  mIMEPendingTextChange.MergeWith(aChange);

  // We may not be in the middle of flushing,
  // in which case this flag is meaningless.
  mIMETextChangedDuringFlush = true;
}

void GeckoEditableSupport::PostFlushIMEChanges() {
  if (mIMEPendingTextChange.IsValid() || mIMESelectionChanged) {
    // Already posted
    return;
  }

  RefPtr<GeckoEditableSupport> self(this);

  nsAppShell::PostEvent([this, self] {
    nsCOMPtr<nsIWidget> widget = GetWidget();
    if (widget && !widget->Destroyed()) {
      FlushIMEChanges();
    }
  });
}

void GeckoEditableSupport::FlushIMEChanges(FlushChangesFlag aFlags) {
  // Only send change notifications if we are *not* masking events,
  // i.e. if we have a focused editor,
  NS_ENSURE_TRUE_VOID(!mIMEMaskEventsCount);

  if (mIMEDelaySynchronizeReply && mIMEActiveCompositionCount > 0) {
    // We are still expecting more composition events to be handled. Once
    // that happens, FlushIMEChanges will be called again.
    return;
  }

  nsCOMPtr<nsIWidget> widget = GetWidget();
  NS_ENSURE_TRUE_VOID(widget);

  struct TextRecord {
    TextRecord() : start(-1), oldEnd(-1), newEnd(-1) {}

    bool IsValid() const { return start >= 0; }

    nsString text;
    int32_t start;
    int32_t oldEnd;
    int32_t newEnd;
  };
  TextRecord textTransaction;

  bool causedOnlyByComposition = mIMEPendingTextChange.IsValid() &&
                                 mIMEPendingTextChange.mCausedOnlyByComposition;
  mIMETextChangedDuringFlush = false;

  auto shouldAbort = [=, this](bool aForce) -> bool {
    if (!aForce && !mIMETextChangedDuringFlush) {
      return false;
    }
    // A query event could have triggered more text changes to come in, as
    // indicated by our flag. If that happens, try flushing IME changes
    // again.
    if (aFlags == FLUSH_FLAG_NONE) {
      FlushIMEChanges(FLUSH_FLAG_RETRY);
    } else {
      // Don't retry if already retrying, to avoid infinite loops.
      __android_log_print(ANDROID_LOG_WARN, "GeckoEditableSupport",
                          "Already retrying IME flush");
    }
    return true;
  };

  if (mIMEPendingTextChange.IsValid() &&
      (mIMEPendingTextChange.mStartOffset !=
           mIMEPendingTextChange.mRemovedEndOffset ||
       mIMEPendingTextChange.mStartOffset !=
           mIMEPendingTextChange.mAddedEndOffset)) {
    WidgetQueryContentEvent queryTextContentEvent(true, eQueryTextContent,
                                                  widget);

    if (mIMEPendingTextChange.mAddedEndOffset !=
        mIMEPendingTextChange.mStartOffset) {
      queryTextContentEvent.InitForQueryTextContent(
          mIMEPendingTextChange.mStartOffset,
          mIMEPendingTextChange.mAddedEndOffset -
              mIMEPendingTextChange.mStartOffset);
      widget->DispatchEvent(&queryTextContentEvent);

      if (shouldAbort(NS_WARN_IF(queryTextContentEvent.Failed()))) {
        return;
      }

      textTransaction.text = queryTextContentEvent.mReply->DataRef();
    }

    textTransaction.start =
        static_cast<int32_t>(mIMEPendingTextChange.mStartOffset);
    textTransaction.oldEnd =
        static_cast<int32_t>(mIMEPendingTextChange.mRemovedEndOffset);
    textTransaction.newEnd =
        static_cast<int32_t>(mIMEPendingTextChange.mAddedEndOffset);
  }

  int32_t selStart = -1;
  int32_t selEnd = -1;

  if (mIMESelectionChanged) {
    if (mCachedSelection.IsValid()) {
      selStart = mCachedSelection.mStartOffset;
      selEnd = mCachedSelection.mEndOffset;
    } else {
      // XXX Unfortunately we don't know current selection via selection
      //     change notification.
      //     eQuerySelectedText might be newer data than text change data.
      //     It means that GeckoEditableChild.onSelectionChange may throw
      //     IllegalArgumentException since we don't merge with newer text
      //     change.
      WidgetQueryContentEvent querySelectedTextEvent(true, eQuerySelectedText,
                                                     widget);
      widget->DispatchEvent(&querySelectedTextEvent);

      if (shouldAbort(
              NS_WARN_IF(querySelectedTextEvent.DidNotFindSelection()))) {
        return;
      }

      selStart =
          static_cast<int32_t>(querySelectedTextEvent.mReply->AnchorOffset());
      selEnd =
          static_cast<int32_t>(querySelectedTextEvent.mReply->FocusOffset());
    }

    if (aFlags == FLUSH_FLAG_RECOVER && textTransaction.IsValid()) {
      // Sometimes we get out-of-bounds selection during recovery.
      // Limit the offsets so we don't crash.
      const int32_t end = textTransaction.start + textTransaction.text.Length();
      selStart = std::min(selStart, end);
      selEnd = std::min(selEnd, end);
    }
  }

  JNIEnv* const env = jni::GetGeckoThreadEnv();
  auto flushOnException = [=, this]() -> bool {
    if (!env->ExceptionCheck()) {
      return false;
    }
    if (aFlags != FLUSH_FLAG_RECOVER) {
      // First time seeing an exception; try flushing text.
      env->ExceptionClear();
      __android_log_print(ANDROID_LOG_WARN, "GeckoEditableSupport",
                          "Recovering from IME exception");
      FlushIMEText(FLUSH_FLAG_RECOVER);
    } else {
      // Give up because we've already tried.
#ifdef RELEASE_OR_BETA
      env->ExceptionClear();
#else
      MOZ_CATCH_JNI_EXCEPTION(env);
#endif
    }
    return true;
  };

  // Commit the text change and selection change transaction.
  mIMEPendingTextChange.Clear();

  if (textTransaction.IsValid()) {
    mEditable->OnTextChange(textTransaction.text, textTransaction.start,
                            textTransaction.oldEnd, textTransaction.newEnd,
                            causedOnlyByComposition);
    if (flushOnException()) {
      return;
    }
  }

  while (mIMEDelaySynchronizeReply && mIMEActiveSynchronizeCount) {
    mIMEActiveSynchronizeCount--;
    mEditable->NotifyIME(EditableListener::NOTIFY_IME_REPLY_EVENT);
  }
  mIMEDelaySynchronizeReply = false;
  mIMEActiveSynchronizeCount = 0;
  mIMEActiveCompositionCount = 0;

  if (mIMESelectionChanged) {
    mIMESelectionChanged = false;
    if (mDispatcher) {
      // mCausedOnlyByComposition may be true on committing text.
      // So even if true, there is no composition.
      causedOnlyByComposition &= mDispatcher->IsComposing();
    }
    mEditable->OnSelectionChange(selStart, selEnd, causedOnlyByComposition);
    flushOnException();
  }
}

void GeckoEditableSupport::FlushIMEText(FlushChangesFlag aFlags) {
  NS_WARNING_ASSERTION(
      !mIMEDelaySynchronizeReply || !mIMEActiveCompositionCount,
      "Cannot synchronize Java text with Gecko text");

  // Notify Java of the newly focused content
  mIMEPendingTextChange.Clear();
  mIMESelectionChanged = true;

  // Use 'INT32_MAX / 2' here because subsequent text changes might combine
  // with this text change, and overflow might occur if we just use
  // INT32_MAX.
  IMENotification notification(NOTIFY_IME_OF_TEXT_CHANGE);
  notification.mTextChangeData.mStartOffset = 0;
  notification.mTextChangeData.mRemovedEndOffset = INT32_MAX / 2;
  notification.mTextChangeData.mAddedEndOffset = INT32_MAX / 2;
  NotifyIME(mDispatcher, notification);

  FlushIMEChanges(aFlags);
}

void GeckoEditableSupport::UpdateCompositionRects() {
  nsCOMPtr<nsIWidget> widget = GetWidget();
  RefPtr<TextComposition> composition(GetComposition());
  NS_ENSURE_TRUE_VOID(mDispatcher && widget);

  jni::ObjectArray::LocalRef rects;
  if (composition) {
    uint32_t offset = composition->NativeOffsetOfStartComposition();
    WidgetQueryContentEvent queryTextRectsEvent(true, eQueryTextRectArray,
                                                widget);
    queryTextRectsEvent.InitForQueryTextRectArray(
        offset, composition->String().Length());
    widget->DispatchEvent(&queryTextRectsEvent);
    rects = ConvertRectArrayToJavaRectFArray(
        queryTextRectsEvent.Succeeded()
            ? queryTextRectsEvent.mReply->mRectArray
            : CopyableTArray<mozilla::LayoutDeviceIntRect>());
  } else {
    rects = ConvertRectArrayToJavaRectFArray(
        CopyableTArray<mozilla::LayoutDeviceIntRect>());
  }

  WidgetQueryContentEvent queryCaretRectEvent(true, eQueryCaretRect, widget);
  WidgetQueryContentEvent::Options options;
  options.mRelativeToInsertionPoint = true;
  queryCaretRectEvent.InitForQueryCaretRect(0, options);

  widget->DispatchEvent(&queryCaretRectEvent);
  auto caretRect =
      queryCaretRectEvent.Succeeded()
          ? java::sdk::RectF::New(queryCaretRectEvent.mReply->mRect.x,
                                  queryCaretRectEvent.mReply->mRect.y,
                                  queryCaretRectEvent.mReply->mRect.XMost(),
                                  queryCaretRectEvent.mReply->mRect.YMost())
          : java::sdk::RectF::New();

  mEditable->UpdateCompositionRects(rects, caretRect);
}

void GeckoEditableSupport::OnImeSynchronize() {
  AutoGeckoEditableBlocker blocker(this);

  if (mIMEDelaySynchronizeReply) {
    // If we are waiting for other events to reply,
    // queue this reply as well.
    mIMEActiveSynchronizeCount++;
    return;
  }
  if (!mIMEMaskEventsCount) {
    FlushIMEChanges();
  }
  mEditable->NotifyIME(EditableListener::NOTIFY_IME_REPLY_EVENT);
}

void GeckoEditableSupport::OnImeReplaceText(int32_t aStart, int32_t aEnd,
                                            jni::String::Param aText) {
  AutoGeckoEditableBlocker blocker(this);

  if (DoReplaceText(aStart, aEnd, aText)) {
    mIMEDelaySynchronizeReply = true;
  }

  OnImeSynchronize();
}

bool GeckoEditableSupport::DoReplaceText(int32_t aStart, int32_t aEnd,
                                         jni::String::Param aText) {
  ALOGIME(
      "IME: IME_REPLACE_TEXT: aStart=%d, aEnd=%d, aText=\"%s\", "
      "mIMERanges=%zu, mIMEKeyEvents=%zu",
      aStart, aEnd, NS_ConvertUTF16toUTF8(aText->ToString()).get(),
      mIMERanges->Length(), mIMEKeyEvents.Length());

  // Return true if processed and we should reply to the OnImeReplaceText
  // event later. Return false if _not_ processed and we should reply to the
  // OnImeReplaceText event now.

  if (mIMEMaskEventsCount > 0) {
    // Not focused; still reply to events, but don't do anything else.
    return false;
  }

  if (nsWindow* window = GetNsWindow()) {
    window->UserActivity();
  }

  /*
      Replace text in Gecko thread from aStart to aEnd with the string text.
  */
  nsCOMPtr<nsIWidget> widget = GetWidget();
  NS_ENSURE_TRUE(mDispatcher && widget, false);
  const OwningNonNull<TextEventDispatcher> dispatcher(*mDispatcher);
  NS_ENSURE_SUCCESS(BeginInputTransaction(dispatcher), false);

  RefPtr<TextComposition> composition(GetComposition());
  MOZ_ASSERT(!composition || !composition->EditorIsHandlingLatestChange());

  nsString string(aText->ToString());
  const bool composing = !mIMERanges->IsEmpty();
  nsEventStatus status = nsEventStatus_eIgnore;
  bool textChanged = composing;
  // Whether deleting content before setting or committing composition text.
  bool performDeletion = false;
  // Dispatch composition start to set current composition.
  bool needDispatchCompositionStart = false;

  if (!mIMEKeyEvents.IsEmpty() || !composition || !dispatcher->IsComposing() ||
      uint32_t(aStart) != composition->NativeOffsetOfStartComposition() ||
      uint32_t(aEnd) != composition->NativeOffsetOfStartComposition() +
                            composition->String().Length()) {
    // Only start a new composition if we have key events,
    // if we don't have an existing composition, or
    // the replaced text does not match our composition.
    textChanged |= RemoveComposition();

#ifdef NIGHTLY_BUILD
    {
      WidgetQueryContentEvent querySelectedTextEvent(true, eQuerySelectedText,
                                                     widget);
      widget->DispatchEvent(&querySelectedTextEvent);
      if (querySelectedTextEvent.Succeeded()) {
        ALOGIME(
            "IME: Current selection: %s",
            ToString(querySelectedTextEvent.mReply->mOffsetAndData).c_str());
      }
    }
#endif

    // If aStart or aEnd is negative value, we use current selection instead
    // of updating the selection.
    if (aStart >= 0 && aEnd >= 0) {
      // Use text selection to set target position(s) for
      // insert, or replace, of text.
      MOZ_ASSERT(dispatcher->GetWidget() == widget);
      dispatcher->DispatchSetSelectionEvent(
          uint32_t(aStart), uint32_t(aEnd - aStart),
          ExpandToClusterBoundary::No, RangeDirection::Normal,
          nsISelectionListener::IME_REASON);
    }

    if (!mIMEKeyEvents.IsEmpty()) {
      bool ignoreNextKeyPress = false;
      for (uint32_t i = 0; i < mIMEKeyEvents.Length(); i++) {
        const auto event = mIMEKeyEvents[i]->AsKeyboardEvent();
        // widget for duplicated events is initially nullptr.
        event->mWidget = widget;

        status = nsEventStatus_eIgnore;
        if (event->mMessage != eKeyPress) {
          dispatcher->DispatchKeyboardEvent(event->mMessage, *event, status);
          // Skip default processing. It means that next key press shouldn't
          // be dispatched.
          ignoreNextKeyPress = event->mMessage == eKeyDown &&
                               status == nsEventStatus_eConsumeNoDefault;
        } else {
          if (ignoreNextKeyPress) {
            // Don't dispatch key press since previous key down is consumed.
            ignoreNextKeyPress = false;
            continue;
          }
          dispatcher->MaybeDispatchKeypressEvents(*event, status);
          if (status == nsEventStatus_eConsumeNoDefault) {
            textChanged = true;
          }
        }
        if (!mDispatcher || widget->Destroyed()) {
          // Don't wait for any text change event.
          textChanged = false;
          break;
        }
      }
      mIMEKeyEvents.Clear();
      return textChanged;
    }

    if (aStart != aEnd) {
      if (composing) {
        // Actually Gecko doesn't start composition, so it is unnecessary to
        // delete content before setting composition string.
        needDispatchCompositionStart = true;
      } else {
        // Perform a deletion first.
        performDeletion = true;
      }
    }
  } else if (composition->String().Equals(string)) {
    // If the new text is the same as the existing composition text,
    // the NS_COMPOSITION_CHANGE event does not generate a text
    // change notification. However, the Java side still expects
    // one, so we manually generate a notification.
    //
    // Also, since this is IME change, we have to set mCausedOnlyByComposition.
    IMENotification::TextChangeData dummyChange(aStart, aEnd, aEnd, true,
                                                false);
    PostFlushIMEChanges();
    mIMESelectionChanged = true;
    AddIMETextChange(dummyChange);
    textChanged = true;
  }

  SendIMEDummyKeyEvent(widget, eKeyDown);
  if (!mDispatcher || widget->Destroyed()) {
    return false;
  }

  if (!StaticPrefs::intl_ime_use_composition_events_for_insert_text() &&
      !composing && !dispatcher->IsComposing() && aStart == aEnd &&
      !string.IsEmpty()) {
    // We don't start composition yet and inserting text has no composition.
    // So we can simply insert text without composition.
    ALOGIME("IME: Don't use composition event to insert text");
    (void)dispatcher->DispatchInsertTextCommandEvent(string);
    if (!mDispatcher || widget->Destroyed()) {
      return false;
    }
    SendIMEDummyKeyEvent(widget, eKeyUp);
    return true;
  }

  if (needDispatchCompositionStart) {
    // StartComposition sets composition string from selected string.
    nsEventStatus status = nsEventStatus_eIgnore;
    dispatcher->StartComposition(status);
    if (!mDispatcher || widget->Destroyed()) {
      return false;
    }
  } else if (performDeletion) {
    (void)dispatcher->DispatchContentCommandEvent(eContentCommandDelete);
    if (!mDispatcher || widget->Destroyed()) {
      return false;
    }
    textChanged = true;
  }

  if (composing) {
    dispatcher->SetPendingComposition(string, mIMERanges);
    dispatcher->FlushPendingComposition(status);
    mIMEActiveCompositionCount++;
    // Ensure IME ranges are empty.
    mIMERanges->Clear();
  } else if (!string.IsEmpty() || dispatcher->IsComposing()) {
    dispatcher->CommitComposition(status, &string);
    mIMEActiveCompositionCount++;
    textChanged = true;
  }
  if (!mDispatcher || widget->Destroyed()) {
    return false;
  }

  SendIMEDummyKeyEvent(widget, eKeyUp);
  // Widget may be destroyed after dispatching the above event.

  return textChanged;
}

void GeckoEditableSupport::OnImeAddCompositionRange(
    int32_t aStart, int32_t aEnd, int32_t aRangeType, int32_t aRangeStyle,
    int32_t aRangeLineStyle, bool aRangeBoldLine, int32_t aRangeForeColor,
    int32_t aRangeBackColor, int32_t aRangeLineColor) {
  AutoGeckoEditableBlocker blocker(this);

  if (mIMEMaskEventsCount > 0) {
    // Not focused.
    return;
  }

  TextRange range;
  range.mStartOffset = aStart;
  range.mEndOffset = aEnd;
  range.mRangeType = ToTextRangeType(aRangeType);
  range.mRangeStyle.mDefinedStyles = aRangeStyle;
  range.mRangeStyle.mLineStyle = TextRangeStyle::ToLineStyle(aRangeLineStyle);
  range.mRangeStyle.mIsBoldLine = aRangeBoldLine;
  range.mRangeStyle.mForegroundColor =
      ConvertAndroidColor(uint32_t(aRangeForeColor));
  range.mRangeStyle.mBackgroundColor =
      ConvertAndroidColor(uint32_t(aRangeBackColor));
  range.mRangeStyle.mUnderlineColor =
      ConvertAndroidColor(uint32_t(aRangeLineColor));
  mIMERanges->AppendElement(range);
}

void GeckoEditableSupport::OnImeUpdateComposition(int32_t aStart, int32_t aEnd,
                                                  int32_t aFlags) {
  AutoGeckoEditableBlocker blocker(this);

  if (DoUpdateComposition(aStart, aEnd, aFlags)) {
    mIMEDelaySynchronizeReply = true;
  }
}

bool GeckoEditableSupport::DoUpdateComposition(int32_t aStart, int32_t aEnd,
                                               int32_t aFlags) {
  if (mIMEMaskEventsCount > 0) {
    // Not focused.
    return false;
  }

  nsCOMPtr<nsIWidget> widget = GetWidget();
  NS_ENSURE_TRUE(mDispatcher && widget, false);

  const bool keepCurrent =
      !!(aFlags & java::GeckoEditableChild::FLAG_KEEP_CURRENT_COMPOSITION);

  // A composition with no ranges means we want to set the selection.
  if (mIMERanges->IsEmpty()) {
    if (keepCurrent && mDispatcher->IsComposing()) {
      // Don't set selection if we want to keep current composition.
      return false;
    }

    MOZ_ASSERT(aStart >= 0 && aEnd >= 0);
    const bool compositionChanged = RemoveComposition();

    MOZ_ASSERT(mDispatcher->GetWidget() == widget);
    const uint32_t offset = std::min(aStart, aEnd);
    mDispatcher->DispatchSetSelectionEvent(
        offset, std::max(aStart, aEnd) - offset, ExpandToClusterBoundary::No,
        aStart > aEnd ? RangeDirection::Reversed : RangeDirection::Normal);
    return compositionChanged;
  }

  /**
   * Update the composition from aStart to aEnd using information from added
   * ranges. This is only used for visual indication and does not affect the
   * text content.  Only the offsets are specified and not the text content
   * to eliminate the possibility of this event altering the text content
   * unintentionally.
   */
  nsString string;
  RefPtr<TextComposition> composition(GetComposition());
  MOZ_ASSERT(!composition || !composition->EditorIsHandlingLatestChange());

  nsEventStatus status = nsEventStatus_eIgnore;
  if (!composition || !mDispatcher->IsComposing() ||
      uint32_t(aStart) != composition->NativeOffsetOfStartComposition() ||
      uint32_t(aEnd) != composition->NativeOffsetOfStartComposition() +
                            composition->String().Length()) {
    if (keepCurrent) {
      // Don't start a new composition if we want to keep the current one.
      mIMERanges->Clear();
      return false;
    }

    // Only start new composition if we don't have an existing one,
    // or if the existing composition doesn't match the new one.
    RemoveComposition();

    MOZ_ASSERT(mDispatcher->GetWidget() == widget);
    mDispatcher->DispatchSetSelectionEvent(
        uint32_t(aStart), uint32_t(aEnd - aStart), ExpandToClusterBoundary::No,
        RangeDirection::Normal, nsISelectionListener::IME_REASON);

    {
      // XXX Hasn't already received selection change notification?
      // https://searchfox.org/firefox-main/rev/6762e79efb6111d51bb58951c1ebdbac886526e7/layout/base/PresShell.cpp#9295-9301
      // Then, we don't need to make ContentEventHandler recompute the new
      // selection range.
      WidgetQueryContentEvent querySelectedTextEvent(true, eQuerySelectedText,
                                                     widget);
      status = widget->DispatchEvent(&querySelectedTextEvent);
      MOZ_ASSERT(querySelectedTextEvent.Succeeded());
      if (querySelectedTextEvent.FoundSelection()) {
        string = querySelectedTextEvent.mReply->DataRef();
      }
    }
  } else {
    // If the new composition matches the existing composition,
    // reuse the old composition.
    string = composition->String();
  }

  ALOGIME("IME: IME_SET_TEXT: text=\"%s\", length=%zu, range=%zu",
          NS_ConvertUTF16toUTF8(string).get(), string.Length(),
          mIMERanges->Length());

  if (NS_WARN_IF(NS_FAILED(BeginInputTransaction(mDispatcher)))) {
    mIMERanges->Clear();
    return false;
  }
  mDispatcher->SetPendingComposition(string, mIMERanges);
  mDispatcher->FlushPendingComposition(status);
  mIMEActiveCompositionCount++;
  mIMERanges->Clear();
  return true;
}

void GeckoEditableSupport::OnImeRequestCursorUpdates(int aRequestMode) {
  AutoGeckoEditableBlocker blocker(this);

  if (aRequestMode == EditableClient::ONE_SHOT) {
    UpdateCompositionRects();
    return;
  }

  mIMEMonitorCursor = (aRequestMode == EditableClient::START_MONITOR);
}

class MOZ_STACK_CLASS AutoSelectionRestore final {
 public:
  explicit AutoSelectionRestore(nsIWidget* widget,
                                TextEventDispatcher* dispatcher)
      : mWidget(widget), mDispatcher(dispatcher) {
    MOZ_ASSERT(widget);
    if (!dispatcher || !dispatcher->IsComposing()) {
      mOffset = UINT32_MAX;
      mLength = UINT32_MAX;
      return;
    }
    WidgetQueryContentEvent querySelectedTextEvent(true, eQuerySelectedText,
                                                   widget);
    widget->DispatchEvent(&querySelectedTextEvent);
    if (querySelectedTextEvent.DidNotFindSelection()) {
      mOffset = UINT32_MAX;
      mLength = UINT32_MAX;
      return;
    }

    mOffset = querySelectedTextEvent.mReply->StartOffset();
    mLength = querySelectedTextEvent.mReply->DataLength();
  }

  ~AutoSelectionRestore() {
    if (mWidget->Destroyed() || mOffset == UINT32_MAX) {
      return;
    }

    // FIXME: If the range boundaries are around inline element boundaries, the
    // restored range may be different from the original one because inline
    // element boundaries do not advance offset in the flattened text. I wonder
    // if we can add eSaveSelectionRange event and eRestoreSelectionRange events
    // to make IMEContentObserver or something handle it.
    MOZ_ASSERT(mDispatcher->GetWidget() == mWidget);
    mDispatcher->DispatchSetSelectionEvent(
        mOffset, mLength, ExpandToClusterBoundary::No, RangeDirection::Normal,
        nsISelectionListener::IME_REASON);
  }

 private:
  nsCOMPtr<nsIWidget> mWidget;
  RefPtr<TextEventDispatcher> mDispatcher;
  uint32_t mOffset;
  uint32_t mLength;
};

void GeckoEditableSupport::OnImeRequestCommit() {
  AutoGeckoEditableBlocker blocker(this);

  if (mIMEMaskEventsCount > 0) {
    // Not focused.
    return;
  }

  nsCOMPtr<nsIWidget> widget = GetWidget();
  if (NS_WARN_IF(!widget)) {
    return;
  }

  AutoSelectionRestore restore(widget, mDispatcher);

  RemoveComposition(COMMIT_IME_COMPOSITION);
}

void GeckoEditableSupport::AsyncNotifyIME(int32_t aNotification) {
  RefPtr<GeckoEditableSupport> self(this);

  nsAppShell::PostEvent([this, self, aNotification] {
    if (!mIMEMaskEventsCount) {
      mEditable->NotifyIME(aNotification);
    }
  });
}

nsresult GeckoEditableSupport::NotifyIME(
    TextEventDispatcher* aTextEventDispatcher,
    const IMENotification& aNotification) {
  MOZ_ASSERT(mEditable);

  switch (aNotification.mMessage) {
    case REQUEST_TO_COMMIT_COMPOSITION: {
      ALOGIME("IME: REQUEST_TO_COMMIT_COMPOSITION");

      RemoveComposition(COMMIT_IME_COMPOSITION);
      AsyncNotifyIME(EditableListener::NOTIFY_IME_TO_COMMIT_COMPOSITION);
      break;
    }

    case REQUEST_TO_CANCEL_COMPOSITION: {
      ALOGIME("IME: REQUEST_TO_CANCEL_COMPOSITION");

      RemoveComposition(CANCEL_IME_COMPOSITION);
      AsyncNotifyIME(EditableListener::NOTIFY_IME_TO_CANCEL_COMPOSITION);
      break;
    }

    case NOTIFY_IME_OF_FOCUS: {
      ALOGIME("IME: NOTIFY_IME_OF_FOCUS");

      mIMEFocusCount++;

      RefPtr<GeckoEditableSupport> self(this);
      RefPtr<TextEventDispatcher> dispatcher = aTextEventDispatcher;

      // Post an event because we have to flush the text before sending a
      // focus event, and we may not be able to flush text during the
      // NotifyIME call.
      nsAppShell::PostEvent([this, self, dispatcher] {
        nsCOMPtr<nsIWidget> widget = dispatcher->GetWidget();

        --mIMEMaskEventsCount;
        if (!mIMEFocusCount || !widget || widget->Destroyed()) {
          ALOGIME("IME: NOTIFY_IME_OF_FOCUS: No focus");
          return;
        }

        mEditable->NotifyIME(EditableListener::NOTIFY_IME_OF_TOKEN);

        if (mIsRemote) {
          if (!mEditableAttached) {
            // Re-attach on focus; see OnRemovedFrom().
            jni::NativeWeakPtrHolder<GeckoEditableSupport>::AttachExisting(
                mEditable, do_AddRef(this));
            mEditableAttached = true;
          }

          if (!mEditable->HasEditableParent()) {
            if (dom::BrowserChild* browserChild =
                    widget->GetOwningBrowserChild()) {
              const uint64_t tabId = browserChild->GetTabId();

              EnsureEditableParent(tabId);
            }
          }

          // Because GeckoEditableSupport in content process doesn't
          // manage the active input context, we need to retrieve the
          // input context from the widget, for use by
          // OnImeReplaceText.
          mInputContext = widget->GetInputContext();
        }
        mDispatcher = dispatcher;
        mIMEKeyEvents.Clear();

        mCachedSelection.Reset();

        mIMEDelaySynchronizeReply = false;
        mIMEActiveCompositionCount = 0;
        FlushIMEText();

        // IME will call requestCursorUpdates after getting context.
        // So reset cursor update mode before getting context.
        mIMEMonitorCursor = false;

        mEditable->NotifyIME(EditableListener::NOTIFY_IME_OF_FOCUS);
      });
      break;
    }

    case NOTIFY_IME_OF_BLUR: {
      ALOGIME("IME: NOTIFY_IME_OF_BLUR");

      mIMEFocusCount--;
      MOZ_ASSERT(mIMEFocusCount >= 0);

      RefPtr<GeckoEditableSupport> self(this);
      nsAppShell::PostEvent([this, self] {
        if (!mIMEFocusCount) {
          mIMEDelaySynchronizeReply = false;
          mIMEActiveSynchronizeCount = 0;
          mIMEActiveCompositionCount = 0;
          mInputContext.ShutDown();
          mEditable->NotifyIME(EditableListener::NOTIFY_IME_OF_BLUR);
          OnRemovedFrom(mDispatcher);
        }
      });

      // Mask events because we lost focus. Unmask on the next focus.
      mIMEMaskEventsCount++;

      mWaitingReplyKeyEvents.Clear();
      break;
    }

    case NOTIFY_IME_OF_SELECTION_CHANGE: {
      ALOGIME("IME: NOTIFY_IME_OF_SELECTION_CHANGE: SelectionChangeData=%s",
              ToString(aNotification.mSelectionChangeData).c_str());

      if (aNotification.mSelectionChangeData.HasRange()) {
        mCachedSelection.mStartOffset = static_cast<int32_t>(
            aNotification.mSelectionChangeData.AnchorOffset());
        mCachedSelection.mEndOffset = static_cast<int32_t>(
            aNotification.mSelectionChangeData.FocusOffset());
      } else {
        mCachedSelection.Reset();
      }

      PostFlushIMEChanges();
      mIMESelectionChanged = true;
      break;
    }

    case NOTIFY_IME_OF_TEXT_CHANGE: {
      ALOGIME("IME: NOTIFY_IME_OF_TEXT_CHANGE: TextChangeData=%s",
              ToString(aNotification.mTextChangeData).c_str());

      /* Make sure Java's selection is up-to-date */
      PostFlushIMEChanges();
      mIMESelectionChanged = true;
      AddIMETextChange(aNotification.mTextChangeData);
      break;
    }

    case NOTIFY_IME_OF_COMPOSITION_EVENT_HANDLED: {
      ALOGIME("IME: NOTIFY_IME_OF_COMPOSITION_EVENT_HANDLED");

      // NOTIFY_IME_OF_COMPOSITION_EVENT_HANDLED isn't sent per IME call.
      // Receiving this event means that Gecko has already handled all IME
      // composing events in queue.
      //
      if (mIsRemote) {
        OnNotifyIMEOfCompositionEventHandled();
      } else {
        // Also, when receiving this event, mIMEDelaySynchronizeReply won't
        // update yet on non-e10s case since IME event is posted before updating
        // it. So we have to delay handling of this event.
        RefPtr<GeckoEditableSupport> self(this);
        nsAppShell::PostEvent(
            [this, self] { OnNotifyIMEOfCompositionEventHandled(); });
      }
      break;
    }

    default:
      break;
  }
  return NS_OK;
}

void GeckoEditableSupport::OnNotifyIMEOfCompositionEventHandled() {
  // NOTIFY_IME_OF_COMPOSITION_EVENT_HANDLED may be merged with multiple events,
  // so reset count.
  mIMEActiveCompositionCount = 0;
  if (mIMEDelaySynchronizeReply) {
    FlushIMEChanges();
  }

  // Hardware keyboard support requires each string rect.
  if (mIMEMonitorCursor) {
    UpdateCompositionRects();
  }
}

void GeckoEditableSupport::OnRemovedFrom(
    TextEventDispatcher* aTextEventDispatcher) {
  mDispatcher = nullptr;

  if (mIsRemote && mEditable->HasEditableParent()) {
    // When we're remote, detach every time.
    OnWeakNonIntrusiveDetach(NS_NewRunnableFunction(
        "GeckoEditableSupport::OnRemovedFrom",
        [editable = java::GeckoEditableChild::GlobalRef(mEditable)] {
          DisposeNative(editable);
        }));
  }
}

void GeckoEditableSupport::WillDispatchKeyboardEvent(
    TextEventDispatcher* aTextEventDispatcher,
    WidgetKeyboardEvent& aKeyboardEvent, uint32_t aIndexOfKeypress,
    void* aData) {}

NS_IMETHODIMP_(IMENotificationRequests)
GeckoEditableSupport::GetIMENotificationRequests() {
  return {IMENotificationRequest::TextChange};
}

static bool ShouldKeyboardDismiss(const nsAString& aInputType,
                                  const nsAString& aInputMode) {
  // Some input type uses the prompt to input value. So it is unnecessary to
  // show software keyboard.
  return aInputMode.EqualsLiteral("none") || aInputType.EqualsLiteral("date") ||
         aInputType.EqualsLiteral("time") ||
         aInputType.EqualsLiteral("month") ||
         aInputType.EqualsLiteral("week") ||
         aInputType.EqualsLiteral("datetime-local");
}

void GeckoEditableSupport::SetInputContext(const InputContext& aContext,
                                           const InputContextAction& aAction) {
  // SetInputContext is called from chrome process only
  MOZ_ASSERT(XRE_IsParentProcess());
  MOZ_ASSERT(mEditable);

  ALOGIME(
      "IME: SetInputContext: aContext=%s, "
      "aAction={mCause=%s, mFocusChange=%s}",
      ToString(aContext).c_str(), ToString(aAction.mCause).c_str(),
      ToString(aAction.mFocusChange).c_str());

  const bool changingEnabledState =
      aContext.IsInputAttributeChanged(mInputContext);

  mInputContext = aContext;

  if (mInputContext.mIMEState.mEnabled != IMEEnabled::Disabled &&
      !ShouldKeyboardDismiss(mInputContext.mHTMLInputType,
                             mInputContext.mHTMLInputMode) &&
      aAction.UserMightRequestOpenVKB() && !changingEnabledState) {
    // Don't reset keyboard when we should simply open the vkb
    mEditable->NotifyIME(EditableListener::NOTIFY_IME_OPEN_VKB);
    return;
  }

  // Post an event to keep calls in order relative to NotifyIME.
  nsAppShell::PostEvent([this, self = RefPtr<GeckoEditableSupport>(this),
                         context = mInputContext, action = aAction] {
    nsCOMPtr<nsIWidget> widget = GetWidget();

    if (!widget || widget->Destroyed()) {
      return;
    }
    NotifyIMEContext(context, action);
  });
}

void GeckoEditableSupport::NotifyIMEContext(const InputContext& aContext,
                                            const InputContextAction& aAction) {
  const bool inPrivateBrowsing = aContext.mInPrivateBrowsing;
  // isUserAction is used whether opening virtual keyboard. But long press
  // shouldn't open it.
  const bool isUserAction =
      aAction.mCause != InputContextAction::CAUSE_LONGPRESS &&
      !(aAction.mCause == InputContextAction::CAUSE_UNKNOWN_CHROME &&
        aContext.mIMEState.mEnabled == IMEEnabled::Enabled) &&
      (aAction.IsHandlingUserInput() || aContext.mHasHandledUserInput);
  const int32_t flags =
      (inPrivateBrowsing ? EditableListener::IME_FLAG_PRIVATE_BROWSING : 0) |
      (isUserAction ? EditableListener::IME_FLAG_USER_ACTION : 0) |
      (aAction.mFocusChange == InputContextAction::FOCUS_NOT_CHANGED
           ? EditableListener::IME_FOCUS_NOT_CHANGED
           : 0);

  mEditable->NotifyIMEContext(static_cast<int32_t>(aContext.mIMEState.mEnabled),
                              aContext.mHTMLInputType, aContext.mHTMLInputMode,
                              aContext.mActionHint, aContext.mAutocapitalize,
                              aContext.mAutocorrect, flags);
}

InputContext GeckoEditableSupport::GetInputContext() {
  // GetInputContext is called from chrome process only
  MOZ_ASSERT(XRE_IsParentProcess());
  InputContext context = mInputContext;
  context.mIMEState.mOpen = IMEState::OPEN_STATE_NOT_SUPPORTED;
  return context;
}

void GeckoEditableSupport::TransferParent(jni::Object::Param aEditableParent) {
  ALOGIME("IME: TransferParent, mIMEFocusCount=%d", mIMEFocusCount);

  AutoGeckoEditableBlocker blocker(this);

  mEditable->SetParent(aEditableParent);

  // If we are already focused, make sure the new parent has our token
  // and focus information, so it can accept additional calls from us.
  if (mIMEFocusCount > 0) {
    mEditable->NotifyIME(EditableListener::NOTIFY_IME_OF_TOKEN);
    if (mIsRemote) {
      // GeckoEditableSupport::SetInputContext is called on chrome process
      // only, so mInputContext may be still invalid since it is set after
      // we have gotton focus.
      RefPtr<GeckoEditableSupport> self(this);
      nsAppShell::PostEvent([self = std::move(self)] {
        NS_WARNING_ASSERTION(
            self->mDispatcher,
            "Text dispatcher is still null. Why don't we get focus yet?");
        self->NotifyIMEContext(self->mInputContext, InputContextAction());
      });
    } else {
      NotifyIMEContext(mInputContext, InputContextAction());
    }
    mEditable->NotifyIME(EditableListener::NOTIFY_IME_OF_FOCUS);
    // We have focus, so don't destroy editable child.
    return;
  }

  if (mIsRemote && !mDispatcher) {
    // Detach now if we were only attached temporarily.
    OnRemovedFrom(/* dispatcher */ nullptr);
  }
}

void GeckoEditableSupport::EnsureEditableParent(uint64_t aTabId) {
  MOZ_ASSERT(mEditableAttached);
  MOZ_ASSERT(mEditable);

  if (mEditable->HasEditableParent()) {
    return;
  }
  java::GeckoServiceChildProcess::GetEditableParent(GetJavaEditable(), aTabId);
}

void GeckoEditableSupport::SetOnBrowserChild(dom::BrowserChild* aBrowserChild) {
  MOZ_ASSERT(!XRE_IsParentProcess());
  NS_ENSURE_TRUE_VOID(aBrowserChild);

  const dom::ContentChild* const contentChild =
      dom::ContentChild::GetSingleton();
  RefPtr<widget::PuppetWidget> widget(aBrowserChild->WebWidget());
  NS_ENSURE_TRUE_VOID(contentChild && widget);

  // Get the tab ID in order to get the correct
  // IGeckoEditableParent object, which GeckoEditableChild uses to
  // communicate with the parent process.
  const uint64_t contentId = contentChild->GetID();
  const uint64_t tabId = aBrowserChild->GetTabId();
  NS_ENSURE_TRUE_VOID(contentId && tabId);

  RefPtr<widget::TextEventDispatcherListener> listener =
      widget->GetNativeTextEventDispatcherListener();

  if (!listener ||
      listener.get() ==
          static_cast<widget::TextEventDispatcherListener*>(widget)) {
    // We need to set a new listener.
    const auto editableChild = java::GeckoEditableChild::New(
        /* parent */ nullptr, /* default */ false);

    // Temporarily attach so we can receive the initial editable parent.
    auto editableSupport =
        jni::NativeWeakPtrHolder<GeckoEditableSupport>::Attach(editableChild,
                                                               editableChild);
    auto accEditableSupport(editableSupport.Access());
    MOZ_RELEASE_ASSERT(accEditableSupport);

    // Tell PuppetWidget to use our listener for IME operations.
    widget->SetNativeTextEventDispatcherListener(
        accEditableSupport.AsRefPtr().get());

    accEditableSupport->mEditableAttached = true;

    // Connect the new child to a parent that corresponds to the BrowserChild.
    java::GeckoServiceChildProcess::GetEditableParent(editableChild, tabId);
    return;
  }

  // We need to update the existing listener to use the new parent.

  // We expect the existing TextEventDispatcherListener to be a
  // GeckoEditableSupport object, so we perform a sanity check to make
  // sure, by comparing their respective vtable pointers.
  const auto dummy =
      MakeRefPtr<widget::GeckoEditableSupport>(/* child */ nullptr);
  NS_ENSURE_TRUE_VOID(*reinterpret_cast<const uintptr_t*>(listener.get()) ==
                      *reinterpret_cast<const uintptr_t*>(dummy.get()));

  const auto support =
      static_cast<widget::GeckoEditableSupport*>(listener.get());
  if (!support->mEditableAttached) {
    // Temporarily attach so we can receive the initial editable parent.
    jni::NativeWeakPtrHolder<GeckoEditableSupport>::AttachExisting(
        support->GetJavaEditable(), do_AddRef(support));
    support->mEditableAttached = true;
  }

  // Transfer to a new parent that corresponds to the BrowserChild.
  support->EnsureEditableParent(tabId);
}

nsIWidget* GeckoEditableSupport::GetWidget() const {
  MOZ_ASSERT(NS_IsMainThread());
  return mDispatcher ? mDispatcher->GetWidget() : GetNsWindow();
}

nsWindow* GeckoEditableSupport::GetNsWindow() const {
  MOZ_ASSERT(NS_IsMainThread());

  auto acc(mWindow.Access());
  if (!acc) {
    return nullptr;
  }

  return acc->GetNsWindow();
}

void GeckoEditableSupport::OnImeInsertImage(jni::ByteArray::Param aData,
                                            jni::String::Param aMimeType) {
  AutoGeckoEditableBlocker blocker(this);

  nsCString mimeType(aMimeType->ToCString());
  nsCOMPtr<nsIInputStream> byteStream;

  nsresult rv = NS_NewByteInputStream(
      getter_AddRefs(byteStream),
      mozilla::Span(
          reinterpret_cast<const char*>(aData->GetElements().Elements()),
          aData->Length()),
      NS_ASSIGNMENT_COPY);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return;
  }

  nsCOMPtr<nsITransferable> trans =
      do_CreateInstance("@mozilla.org/widget/transferable;1");
  if (NS_WARN_IF(!trans)) {
    return;
  }
  trans->Init(nullptr);
  rv = trans->SetTransferData(mimeType.get(), byteStream);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return;
  }

  nsCOMPtr<nsIWidget> widget = GetWidget();
  if (NS_WARN_IF(!widget) || NS_WARN_IF(widget->Destroyed())) {
    return;
  }

  if (const RefPtr<TextEventDispatcher> dispatcher =
          widget->GetTextEventDispatcher()) {
    (void)dispatcher->DispatchPasteTransferableCommandEvent(trans);
  }
}

void GeckoEditableSupport::PostHandleKeyEvent(WidgetKeyboardEvent* aEvent) {
  const auto foundIt =
      std::find_if(mWaitingReplyKeyEvents.begin(), mWaitingReplyKeyEvents.end(),
                   [aEvent](const auto& item) {
                     return item.mUniqueId == aEvent->mUniqueId;
                   });
  if (foundIt != mWaitingReplyKeyEvents.end()) {
    mEditable->OnDefaultKeyEvent(*foundIt->mOriginalEvent);
    // Old events might be discarded by preventDefault() call. No one will use
    // it.
    mWaitingReplyKeyEvents.RemoveElementsAt(0, foundIt.GetIndex() + 1);
  }
}

}  // namespace widget
}  // namespace mozilla
