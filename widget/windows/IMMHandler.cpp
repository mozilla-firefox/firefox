/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "IMMHandler.h"

#include <algorithm>

#include "KeyboardLayout.h"
#include "WinIMEHandler.h"
#include "WinUtils.h"
#include "mozilla/Logging.h"
#include "mozilla/MiscEvents.h"
#include "mozilla/TextEvents.h"
#include "mozilla/ToString.h"
#include "nsWindow.h"
#include "nsWindowDefs.h"

#ifndef IME_PROP_ACCEPT_WIDE_VKEY
#  define IME_PROP_ACCEPT_WIDE_VKEY 0x20
#endif

//-------------------------------------------------------------------------
//
// from
// http://download.microsoft.com/download/6/0/9/60908e9e-d2c1-47db-98f6-216af76a235f/msime.h
// The document for this has been removed from MSDN...
//
//-------------------------------------------------------------------------

#define RWM_MOUSE TEXT("MSIMEMouseOperation")

#define IMEMOUSE_NONE 0x00  // no mouse button was pushed
#define IMEMOUSE_LDOWN 0x01
#define IMEMOUSE_RDOWN 0x02
#define IMEMOUSE_MDOWN 0x04
#define IMEMOUSE_WUP 0x10    // wheel up
#define IMEMOUSE_WDOWN 0x20  // wheel down

// For collecting other people's log, tell `MOZ_LOG=IMEHandler:4,sync`
// rather than `MOZ_LOG=IMEHandler:5,sync` since using `5` may create too
// big file.
// Therefore you shouldn't use `LogLevel::Verbose` for logging usual behavior.
extern mozilla::LazyLogModule gIMELog;

static void HandleSeparator(nsACString& aDesc) {
  if (!aDesc.IsEmpty()) {
    aDesc.AppendLiteral(" | ");
  }
}

class GetIMEGeneralPropertyName : public nsAutoCString {
 public:
  explicit GetIMEGeneralPropertyName(DWORD aFlags) {
    if (!aFlags) {
      AppendLiteral("no flags");
      return;
    }
    if (aFlags & IME_PROP_AT_CARET) {
      AppendLiteral("IME_PROP_AT_CARET");
    }
    if (aFlags & IME_PROP_SPECIAL_UI) {
      HandleSeparator(*this);
      AppendLiteral("IME_PROP_SPECIAL_UI");
    }
    if (aFlags & IME_PROP_CANDLIST_START_FROM_1) {
      HandleSeparator(*this);
      AppendLiteral("IME_PROP_CANDLIST_START_FROM_1");
    }
    if (aFlags & IME_PROP_UNICODE) {
      HandleSeparator(*this);
      AppendLiteral("IME_PROP_UNICODE");
    }
    if (aFlags & IME_PROP_COMPLETE_ON_UNSELECT) {
      HandleSeparator(*this);
      AppendLiteral("IME_PROP_COMPLETE_ON_UNSELECT");
    }
    if (aFlags & IME_PROP_ACCEPT_WIDE_VKEY) {
      HandleSeparator(*this);
      AppendLiteral("IME_PROP_ACCEPT_WIDE_VKEY");
    }
  }
  virtual ~GetIMEGeneralPropertyName() = default;
};

class GetIMEUIPropertyName : public nsAutoCString {
 public:
  explicit GetIMEUIPropertyName(DWORD aFlags) {
    if (!aFlags) {
      AppendLiteral("no flags");
      return;
    }
    if (aFlags & UI_CAP_2700) {
      AppendLiteral("UI_CAP_2700");
    }
    if (aFlags & UI_CAP_ROT90) {
      HandleSeparator(*this);
      AppendLiteral("UI_CAP_ROT90");
    }
    if (aFlags & UI_CAP_ROTANY) {
      HandleSeparator(*this);
      AppendLiteral("UI_CAP_ROTANY");
    }
  }
  virtual ~GetIMEUIPropertyName() = default;
};

class GetReconvertStringLog : public nsAutoCString {
 public:
  explicit GetReconvertStringLog(RECONVERTSTRING* aReconv) {
    AssignLiteral("{ dwSize=");
    AppendInt(static_cast<uint32_t>(aReconv->dwSize));
    AppendLiteral(", dwVersion=");
    AppendInt(static_cast<uint32_t>(aReconv->dwVersion));
    AppendLiteral(", dwStrLen=");
    AppendInt(static_cast<uint32_t>(aReconv->dwStrLen));
    AppendLiteral(", dwStrOffset=");
    AppendInt(static_cast<uint32_t>(aReconv->dwStrOffset));
    AppendLiteral(", dwCompStrLen=");
    AppendInt(static_cast<uint32_t>(aReconv->dwCompStrLen));
    AppendLiteral(", dwCompStrOffset=");
    AppendInt(static_cast<uint32_t>(aReconv->dwCompStrOffset));
    AppendLiteral(", dwTargetStrLen=");
    AppendInt(static_cast<uint32_t>(aReconv->dwTargetStrLen));
    AppendLiteral(", dwTargetStrOffset=");
    AppendInt(static_cast<uint32_t>(aReconv->dwTargetStrOffset));
    AppendLiteral(", result str=\"");
    if (aReconv->dwStrLen) {
      char16_t* strStart = reinterpret_cast<char16_t*>(
          reinterpret_cast<char*>(aReconv) + aReconv->dwStrOffset);
      nsDependentString str(strStart, aReconv->dwStrLen);
      Append(NS_ConvertUTF16toUTF8(str));
    }
    AppendLiteral("\" }");
  }
  virtual ~GetReconvertStringLog() = default;
};

namespace mozilla {
namespace widget {

static IMMHandler* gIMMHandler = nullptr;

/******************************************************************************
 * IMEContext
 ******************************************************************************/

IMEContext::IMEContext(HWND aWnd) : mWnd(aWnd), mIMC(::ImmGetContext(aWnd)) {}

IMEContext::IMEContext(nsWindow* aWindowBase)
    : mWnd(aWindowBase->GetWindowHandle()),
      mIMC(::ImmGetContext(aWindowBase->GetWindowHandle())) {}

void IMEContext::Init(HWND aWnd) {
  Clear();
  mWnd = aWnd;
  mIMC = ::ImmGetContext(mWnd);
}

void IMEContext::Init(nsWindow* aWindowBase) {
  Init(aWindowBase->GetWindowHandle());
}

void IMEContext::Clear() {
  if (mWnd && mIMC) {
    ::ImmReleaseContext(mWnd, mIMC);
  }
  mWnd = nullptr;
  mIMC = nullptr;
}

/******************************************************************************
 * IMMHandler
 ******************************************************************************/

static UINT sWM_MSIME_MOUSE = 0;  // mouse message for MSIME 98/2000

constinit WritingMode IMMHandler::sWritingModeOfCompositionFont;
constinit nsString IMMHandler::sIMEName;
UINT IMMHandler::sCodePage = 0;
DWORD IMMHandler::sIMEProperty = 0;
DWORD IMMHandler::sIMEUIProperty = 0;
bool IMMHandler::sAssumeVerticalWritingModeNotSupported = false;
bool IMMHandler::sHasFocus = false;

#define IMPL_IS_IME_ACTIVE(aReadableName, aActualName) \
  bool IMMHandler::Is##aReadableName##Active() {       \
    return sIMEName.Equals(aActualName);               \
  }

IMPL_IS_IME_ACTIVE(ATOK2006, u"ATOK 2006")
IMPL_IS_IME_ACTIVE(ATOK2007, u"ATOK 2007")
IMPL_IS_IME_ACTIVE(ATOK2008, u"ATOK 2008")
IMPL_IS_IME_ACTIVE(ATOK2009, u"ATOK 2009")
IMPL_IS_IME_ACTIVE(ATOK2010, u"ATOK 2010")
// NOTE: Even on Windows for en-US, the name of Google Japanese Input is
//       written in Japanese.
IMPL_IS_IME_ACTIVE(GoogleJapaneseInput,
                   u"Google \x65E5\x672C\x8A9E\x5165\x529B "
                   u"IMM32 \x30E2\x30B8\x30E5\x30FC\x30EB")
IMPL_IS_IME_ACTIVE(Japanist2003, u"Japanist 2003")

#undef IMPL_IS_IME_ACTIVE

// static
bool IMMHandler::IsActiveIMEInBlockList() {
  if (sIMEName.IsEmpty()) {
    return false;
  }
#ifdef _WIN64
  // ATOK started to be TIP of TSF since 2011.  Older than it, i.e., ATOK 2010
  // and earlier have a lot of problems even for daily use.  Perhaps, the
  // reason is Win 8 has a lot of changes around IMM-IME support and TSF,
  // and ATOK 2010 is released earlier than Win 8.
  // ATOK 2006 crashes while converting a word with candidate window.
  // ATOK 2007 doesn't paint and resize suggest window and candidate window
  // correctly (showing white window or too big window).
  // ATOK 2008 and ATOK 2009 crash when user just opens their open state.
  // ATOK 2010 isn't installable newly on Win 7 or later, but we have a lot of
  // crash reports.
  if ((IsATOK2006Active() || IsATOK2007Active() || IsATOK2008Active() ||
       IsATOK2009Active() || IsATOK2010Active())) {
    return true;
  }
#endif  // #ifdef _WIN64
  return false;
}

// static
void IMMHandler::EnsureHandlerInstance() {
  if (!gIMMHandler) {
    gIMMHandler = new IMMHandler();
  }
}

// static
void IMMHandler::Initialize() {
  if (!sWM_MSIME_MOUSE) {
    sWM_MSIME_MOUSE = ::RegisterWindowMessage(RWM_MOUSE);
  }
  sAssumeVerticalWritingModeNotSupported = Preferences::GetBool(
      "intl.imm.vertical_writing.always_assume_not_supported", false);
  InitKeyboardLayout(nullptr, ::GetKeyboardLayout(0));
}

// static
void IMMHandler::Terminate() {
  if (!gIMMHandler) return;
  delete gIMMHandler;
  gIMMHandler = nullptr;
}

// static
bool IMMHandler::IsComposingOnOurEditor() {
  return gIMMHandler && gIMMHandler->mIsComposing;
}

// static
bool IMMHandler::IsComposingWindow(nsWindow* aWindow) {
  return gIMMHandler && gIMMHandler->mComposingWindow == aWindow;
}

// static
bool IMMHandler::IsTopLevelWindowOfComposition(nsWindow* aWindow) {
  if (!gIMMHandler || !gIMMHandler->mComposingWindow) {
    return false;
  }
  HWND wnd = gIMMHandler->mComposingWindow->GetWindowHandle();
  return WinUtils::GetTopLevelHWND(wnd, true) == aWindow->GetWindowHandle();
}

// static
bool IMMHandler::ShouldDrawCompositionStringOurselves() {
  // If current IME has special UI or its composition window should not
  // positioned to caret position, we should now draw composition string
  // ourselves.
  return !(sIMEProperty & IME_PROP_SPECIAL_UI) &&
         (sIMEProperty & IME_PROP_AT_CARET);
}

// static
bool IMMHandler::IsVerticalWritingSupported() {
  // Even if IME claims that they support vertical writing mode but it may not
  // support vertical writing mode for its candidate window.
  if (sAssumeVerticalWritingModeNotSupported) {
    return false;
  }
  // Google Japanese Input doesn't support vertical writing mode.  We should
  // return false if it's active IME.
  if (IsGoogleJapaneseInputActive()) {
    return false;
  }
  return !!(sIMEUIProperty & (UI_CAP_2700 | UI_CAP_ROT90 | UI_CAP_ROTANY));
}

// static
void IMMHandler::InitKeyboardLayout(nsWindow* aWindow, HKL aKeyboardLayout) {
  UINT IMENameLength = ::ImmGetDescriptionW(aKeyboardLayout, nullptr, 0);
  if (IMENameLength) {
    // Add room for the terminating null character
    sIMEName.SetLength(++IMENameLength);
    IMENameLength =
        ::ImmGetDescriptionW(aKeyboardLayout, sIMEName.get(), IMENameLength);
    // Adjust the length to ignore the terminating null character
    sIMEName.SetLength(IMENameLength);
  } else {
    sIMEName.Truncate();
  }

  WORD langID = LOWORD(aKeyboardLayout);
  ::GetLocaleInfoW(MAKELCID(langID, SORT_DEFAULT),
                   LOCALE_IDEFAULTANSICODEPAGE | LOCALE_RETURN_NUMBER,
                   (PWSTR)&sCodePage, sizeof(sCodePage) / sizeof(WCHAR));
  sIMEProperty = ::ImmGetProperty(aKeyboardLayout, IGP_PROPERTY);
  sIMEUIProperty = ::ImmGetProperty(aKeyboardLayout, IGP_UI);

  // If active IME is a TIP of TSF, we cannot retrieve the name with IMM32 API.
  // For hacking some bugs of some TIP, we should set an IME name from the
  // pref.
  if (sCodePage == 932 && sIMEName.IsEmpty()) {
    Preferences::GetString("intl.imm.japanese.assume_active_tip_name_as",
                           sIMEName);
  }

  // Whether the IME supports vertical writing mode might be changed or
  // some IMEs may need specific font for their UI.  Therefore, we should
  // update composition font forcibly here.
  if (aWindow) {
    MaybeAdjustCompositionFont(aWindow, sWritingModeOfCompositionFont, true);
  }

  MOZ_LOG(gIMELog, LogLevel::Info,
          ("IMMHandler::InitKeyboardLayout, aKeyboardLayout=%p (\"%s\"), "
           "sCodePage=%u, sIMEProperty=%s, sIMEUIProperty=%s",
           aKeyboardLayout, NS_ConvertUTF16toUTF8(sIMEName).get(), sCodePage,
           GetIMEGeneralPropertyName(sIMEProperty).get(),
           GetIMEUIPropertyName(sIMEUIProperty).get()));
}

// static
UINT IMMHandler::GetKeyboardCodePage() { return sCodePage; }

// static
IMENotificationRequests IMMHandler::GetIMENotificationRequests() {
  return {IMENotificationRequest::PositionChange,
          IMENotificationRequest::MouseEventOnChar};
}

// used for checking the lParam of WM_IME_COMPOSITION
#define IS_COMPOSING_LPARAM(lParam) \
  ((lParam) & (GCS_COMPSTR | GCS_COMPATTR | GCS_COMPCLAUSE | GCS_CURSORPOS))
#define IS_COMMITTING_LPARAM(lParam) ((lParam) & GCS_RESULTSTR)
// Some IMEs (e.g., the standard IME for Korean) don't have caret position,
// then, we should not set caret position to compositionchange event.
#define NO_IME_CARET -1

IMMHandler::IMMHandler()
    : mCursorPosition(NO_IME_CARET), mCompositionStart(0), mIsComposing(false) {
  MOZ_LOG(gIMELog, LogLevel::Debug, ("IMMHandler::IMMHandler is created"));
}

IMMHandler::~IMMHandler() {
  if (mIsComposing) {
    MOZ_LOG(
        gIMELog, LogLevel::Error,
        ("  IMMHandler::~IMMHandler, ERROR, the instance is still composing"));
  }
  MOZ_LOG(gIMELog, LogLevel::Debug, ("IMMHandler::IMMHandler is destroyed"));
}

nsresult IMMHandler::EnsureClauseArray(int32_t aCount) {
  NS_ENSURE_ARG_MIN(aCount, 0);
  mClauseArray.SetCapacity(aCount + 32);
  return NS_OK;
}

nsresult IMMHandler::EnsureAttributeArray(int32_t aCount) {
  NS_ENSURE_ARG_MIN(aCount, 0);
  mAttributeArray.SetCapacity(aCount + 64);
  return NS_OK;
}

// static
void IMMHandler::CommitComposition(nsWindow* aWindow, bool aForce) {
  MOZ_LOG(gIMELog, LogLevel::Info,
          ("IMMHandler::CommitComposition, aForce=%s, aWindow=%p, hWnd=%p, "
           "mComposingWindow=%p%s",
           TrueOrFalse(aForce), aWindow, aWindow->GetWindowHandle(),
           gIMMHandler ? gIMMHandler->mComposingWindow.get() : nullptr,
           gIMMHandler && gIMMHandler->mComposingWindow
               ? IsComposingOnOurEditor() ? " (composing on editor)"
                                          : " (composing on plug-in)"
               : ""));
  if (!aForce && !IsComposingWindow(aWindow)) {
    return;
  }

  IMEContext context(aWindow);
  bool associated = context.AssociateDefaultContext();
  MOZ_LOG(gIMELog, LogLevel::Info,
          ("  IMMHandler::CommitComposition, associated=%s",
           TrueOrFalse(associated)));

  if (context.IsValid()) {
    ::ImmNotifyIME(context.get(), NI_COMPOSITIONSTR, CPS_COMPLETE, 0);
    ::ImmNotifyIME(context.get(), NI_COMPOSITIONSTR, CPS_CANCEL, 0);
  }

  if (associated) {
    context.Disassociate();
  }
}

// static
void IMMHandler::CancelComposition(nsWindow* aWindow, bool aForce) {
  MOZ_LOG(gIMELog, LogLevel::Info,
          ("IMMHandler::CancelComposition, aForce=%s, aWindow=%p, hWnd=%p, "
           "mComposingWindow=%p%s",
           TrueOrFalse(aForce), aWindow, aWindow->GetWindowHandle(),
           gIMMHandler ? gIMMHandler->mComposingWindow.get() : nullptr,
           gIMMHandler && gIMMHandler->mComposingWindow
               ? IsComposingOnOurEditor() ? " (composing on editor)"
                                          : " (composing on plug-in)"
               : ""));
  if (!aForce && !IsComposingWindow(aWindow)) {
    return;
  }

  IMEContext context(aWindow);
  bool associated = context.AssociateDefaultContext();
  MOZ_LOG(gIMELog, LogLevel::Info,
          ("  IMMHandler::CancelComposition, associated=%s",
           TrueOrFalse(associated)));

  if (context.IsValid()) {
    ::ImmNotifyIME(context.get(), NI_COMPOSITIONSTR, CPS_CANCEL, 0);
  }

  if (associated) {
    context.Disassociate();
  }
}

// static
void IMMHandler::OnDestroyWindow(nsWindow* aWindow) {
  if (!gIMMHandler || gIMMHandler->mComposingWindow != aWindow) {
    return;
  }
  MOZ_LOG(gIMELog, LogLevel::Warning,
          ("IMMHandler::OnDestroyWindow(aWindow=%p), sHasFocus=%s", aWindow,
           TrueOrFalse(sHasFocus)));
  RefPtr window(aWindow);
  OnFocusChange(false, window);
  if (gIMMHandler && gIMMHandler->mComposingWindow == window) {
    gIMMHandler->ClearComposingData();
  }
}

// static
void IMMHandler::OnFocusChange(bool aFocus, nsWindow* aWindow) {
  MOZ_LOG(gIMELog, LogLevel::Info,
          ("IMMHandler::OnFocusChange(aFocus=%s, aWindow=%p), sHasFocus=%s, "
           "IsComposingWindow(aWindow)=%s, aWindow->Destroyed()=%s",
           TrueOrFalse(aFocus), aWindow, TrueOrFalse(sHasFocus),
           TrueOrFalse(IsComposingWindow(aWindow)),
           TrueOrFalse(aWindow->Destroyed())));

  if (!aFocus) {
    IMEHandler::MaybeDestroyNativeCaret();
    if (IsComposingWindow(aWindow) && aWindow->Destroyed()) {
      CancelComposition(aWindow);
    }
  }
  if (gIMMHandler) {
    gIMMHandler->mContentSelection.reset();
  }
  sHasFocus = aFocus;
}

// static
void IMMHandler::OnUpdateComposition(nsWindow* aWindow) {
  if (!gIMMHandler) {
    return;
  }

  IMEContext context(aWindow);
  gIMMHandler->SetIMERelatedWindowsPos(aWindow, context);
}

// static
void IMMHandler::OnSelectionChange(nsWindow* aWindow,
                                   const IMENotification& aIMENotification,
                                   bool aIsIMMActive) {
  if (!aIMENotification.mSelectionChangeData.mCausedByComposition &&
      aIsIMMActive) {
    MaybeAdjustCompositionFont(
        aWindow, aIMENotification.mSelectionChangeData.GetWritingMode());
  }
  // MaybeAdjustCompositionFont() may create gIMMHandler.  So, check it
  // after a call of MaybeAdjustCompositionFont().
  if (gIMMHandler) {
    gIMMHandler->mContentSelection =
        Some(ContentSelection(aIMENotification.mSelectionChangeData));
  }
}

// static
void IMMHandler::MaybeAdjustCompositionFont(nsWindow* aWindow,
                                            const WritingMode& aWritingMode,
                                            bool aForceUpdate) {
  switch (sCodePage) {
    case 932:  // Japanese Shift-JIS
    case 936:  // Simlified Chinese GBK
    case 949:  // Korean
    case 950:  // Traditional Chinese Big5
      EnsureHandlerInstance();
      break;
    default:
      // If there is no instance of nsIMM32Hander, we shouldn't waste footprint.
      if (!gIMMHandler) {
        return;
      }
  }

  // Like Navi-Bar of ATOK, some IMEs may require proper composition font even
  // before sending WM_IME_STARTCOMPOSITION.
  IMEContext context(aWindow);
  gIMMHandler->AdjustCompositionFont(aWindow, context, aWritingMode,
                                     aForceUpdate);
}

// static
bool IMMHandler::ProcessInputLangChangeMessage(nsWindow* aWindow, WPARAM wParam,
                                               LPARAM lParam,
                                               MSGResult& aResult) {
  aResult.mResult = 0;
  aResult.mConsumed = false;
  // We don't need to create the instance of the handler here.
  if (gIMMHandler) {
    gIMMHandler->OnInputLangChange(aWindow, wParam, lParam, aResult);
  }
  InitKeyboardLayout(aWindow, reinterpret_cast<HKL>(lParam));
  // We can release the instance here, because the instance may be never
  // used. E.g., the new keyboard layout may not use IME, or it may use TSF.
  Terminate();
  // Don't return as "processed", the messages should be processed on nsWindow
  // too.
  return false;
}

// static
bool IMMHandler::ProcessMessage(nsWindow* aWindow, UINT msg, WPARAM& wParam,
                                LPARAM& lParam, MSGResult& aResult) {
  // XXX We store the composing window in mComposingWindow.  If IME messages are
  // sent to different window, we should commit the old transaction.  And also
  // if the new window handle is not focused, probably, we should not start
  // the composition, however, such case should not be, it's just bad scenario.

  aResult.mResult = 0;
  switch (msg) {
    case WM_INPUTLANGCHANGE:
      return ProcessInputLangChangeMessage(aWindow, wParam, lParam, aResult);
    case WM_IME_STARTCOMPOSITION:
      EnsureHandlerInstance();
      return gIMMHandler->OnIMEStartComposition(aWindow, aResult);
    case WM_IME_COMPOSITION:
      EnsureHandlerInstance();
      return gIMMHandler->OnIMEComposition(aWindow, wParam, lParam, aResult);
    case WM_IME_ENDCOMPOSITION:
      EnsureHandlerInstance();
      return gIMMHandler->OnIMEEndComposition(aWindow, aResult);
    case WM_IME_CHAR:
      return OnIMEChar(aWindow, wParam, lParam, aResult);
    case WM_IME_NOTIFY:
      return OnIMENotify(aWindow, wParam, lParam, aResult);
    case WM_IME_REQUEST:
      EnsureHandlerInstance();
      return gIMMHandler->OnIMERequest(aWindow, wParam, lParam, aResult);
    case WM_IME_SELECT:
      return OnIMESelect(aWindow, wParam, lParam, aResult);
    case WM_IME_SETCONTEXT:
      return OnIMESetContext(aWindow, wParam, lParam, aResult);
    case WM_KEYDOWN:
      return OnKeyDownEvent(aWindow, wParam, lParam, aResult);
    case WM_CHAR:
      if (!gIMMHandler) {
        return false;
      }
      return gIMMHandler->OnChar(aWindow, wParam, lParam, aResult);
    default:
      return false;
  };
}

/****************************************************************************
 * message handlers
 ****************************************************************************/

void IMMHandler::OnInputLangChange(nsWindow* aWindow, WPARAM wParam,
                                   LPARAM lParam, MSGResult& aResult) {
  MOZ_LOG(gIMELog, LogLevel::Info,
          ("IMMHandler::OnInputLangChange, hWnd=%p, wParam=%08zx, "
           "lParam=%08" PRIxLPTR,
           aWindow->GetWindowHandle(), wParam, lParam));

  aWindow->NotifyIME(REQUEST_TO_COMMIT_COMPOSITION);
  NS_ASSERTION(!mIsComposing, "ResetInputState failed");

  if (mIsComposing) {
    HandleEndComposition(aWindow);
  }

  aResult.mConsumed = false;
}

bool IMMHandler::OnIMEStartComposition(nsWindow* aWindow, MSGResult& aResult) {
  MOZ_LOG(gIMELog, LogLevel::Info,
          ("IMMHandler::OnIMEStartComposition, hWnd=%p, mIsComposing=%s",
           aWindow->GetWindowHandle(), TrueOrFalse(mIsComposing)));
  aResult.mConsumed = ShouldDrawCompositionStringOurselves();
  if (mIsComposing) {
    NS_WARNING("Composition has been already started");
    return true;
  }

  IMEContext context(aWindow);
  HandleStartComposition(aWindow, context);
  return true;
}

bool IMMHandler::OnIMEComposition(nsWindow* aWindow, WPARAM wParam,
                                  LPARAM lParam, MSGResult& aResult) {
  MOZ_LOG(
      gIMELog, LogLevel::Info,
      ("IMMHandler::OnIMEComposition, hWnd=%p, lParam=%08" PRIxLPTR
       ", mIsComposing=%s, "
       "GCS_RESULTSTR=%s, GCS_COMPSTR=%s, GCS_COMPATTR=%s, GCS_COMPCLAUSE=%s, "
       "GCS_CURSORPOS=%s,",
       aWindow->GetWindowHandle(), lParam, TrueOrFalse(mIsComposing),
       TrueOrFalse(lParam & GCS_RESULTSTR), TrueOrFalse(lParam & GCS_COMPSTR),
       TrueOrFalse(lParam & GCS_COMPATTR), TrueOrFalse(lParam & GCS_COMPCLAUSE),
       TrueOrFalse(lParam & GCS_CURSORPOS)));

  IMEContext context(aWindow);
  aResult.mConsumed = HandleComposition(aWindow, context, lParam);
  return true;
}

bool IMMHandler::OnIMEEndComposition(nsWindow* aWindow, MSGResult& aResult) {
  MOZ_LOG(gIMELog, LogLevel::Info,
          ("IMMHandler::OnIMEEndComposition, hWnd=%p, mIsComposing=%s",
           aWindow->GetWindowHandle(), TrueOrFalse(mIsComposing)));

  aResult.mConsumed = ShouldDrawCompositionStringOurselves();
  if (!mIsComposing) {
    return true;
  }

  // Korean IME posts WM_IME_ENDCOMPOSITION first when we hit space during
  // composition. Then, we should ignore the message and commit the composition
  // string at following WM_IME_COMPOSITION.
  MSG compositionMsg;
  if (WinUtils::PeekMessage(&compositionMsg, aWindow->GetWindowHandle(),
                            WM_IME_STARTCOMPOSITION, WM_IME_COMPOSITION,
                            PM_NOREMOVE) &&
      compositionMsg.message == WM_IME_COMPOSITION &&
      IS_COMMITTING_LPARAM(compositionMsg.lParam)) {
    MOZ_LOG(gIMELog, LogLevel::Info,
            ("  IMMHandler::OnIMEEndComposition, WM_IME_ENDCOMPOSITION is "
             "followed by WM_IME_COMPOSITION, ignoring the message..."));
    return true;
  }

  // Otherwise, e.g., ChangJie doesn't post WM_IME_COMPOSITION before
  // WM_IME_ENDCOMPOSITION when composition string becomes empty.
  // Then, we should dispatch a compositionupdate event, a compositionchange
  // event and a compositionend event.
  // XXX Shouldn't we dispatch the compositionchange event with actual or
  //     latest composition string?
  MOZ_LOG(gIMELog, LogLevel::Info,
          ("  IMMHandler::OnIMEEndComposition, mCompositionString=\"%s\"%s",
           NS_ConvertUTF16toUTF8(mCompositionString).get(),
           mCompositionString.IsEmpty() ? "" : ", but canceling it..."));

  HandleEndComposition(aWindow, &EmptyString());

  return true;
}

// static
bool IMMHandler::OnIMEChar(nsWindow* aWindow, WPARAM wParam, LPARAM lParam,
                           MSGResult& aResult) {
  MOZ_LOG(gIMELog, LogLevel::Info,
          ("IMMHandler::OnIMEChar, hWnd=%p, char=%08zx",
           aWindow->GetWindowHandle(), wParam));

  // We don't need to fire any compositionchange events from here. This method
  // will be called when the composition string of the current IME is not drawn
  // by us and some characters are committed. In that case, the committed
  // string was processed in nsWindow::OnIMEComposition already.

  // We need to consume the message so that Windows don't send two WM_CHAR msgs
  aResult.mConsumed = true;
  return true;
}

// static
bool IMMHandler::OnIMECompositionFull(nsWindow* aWindow, MSGResult& aResult) {
  MOZ_LOG(gIMELog, LogLevel::Info,
          ("IMMHandler::OnIMECompositionFull, hWnd=%p",
           aWindow->GetWindowHandle()));

  // not implement yet
  aResult.mConsumed = false;
  return true;
}

// static
bool IMMHandler::OnIMENotify(nsWindow* aWindow, WPARAM wParam, LPARAM lParam,
                             MSGResult& aResult) {
  switch (wParam) {
    case IMN_CHANGECANDIDATE:
      MOZ_LOG(gIMELog, LogLevel::Info,
              ("IMMHandler::OnIMENotify, hWnd=%p, IMN_CHANGECANDIDATE, "
               "lParam=%08" PRIxLPTR,
               aWindow->GetWindowHandle(), lParam));
      break;
    case IMN_CLOSECANDIDATE:
      MOZ_LOG(gIMELog, LogLevel::Info,
              ("IMMHandler::OnIMENotify, hWnd=%p, IMN_CLOSECANDIDATE, "
               "lParam=%08" PRIxLPTR,
               aWindow->GetWindowHandle(), lParam));
      break;
    case IMN_CLOSESTATUSWINDOW:
      MOZ_LOG(gIMELog, LogLevel::Info,
              ("IMMHandler::OnIMENotify, hWnd=%p, IMN_CLOSESTATUSWINDOW",
               aWindow->GetWindowHandle()));
      break;
    case IMN_GUIDELINE:
      MOZ_LOG(gIMELog, LogLevel::Info,
              ("IMMHandler::OnIMENotify, hWnd=%p, IMN_GUIDELINE",
               aWindow->GetWindowHandle()));
      break;
    case IMN_OPENCANDIDATE:
      MOZ_LOG(gIMELog, LogLevel::Info,
              ("IMMHandler::OnIMENotify, hWnd=%p, IMN_OPENCANDIDATE, "
               "lParam=%08" PRIxLPTR,
               aWindow->GetWindowHandle(), lParam));
      break;
    case IMN_OPENSTATUSWINDOW:
      MOZ_LOG(gIMELog, LogLevel::Info,
              ("IMMHandler::OnIMENotify, hWnd=%p, IMN_OPENSTATUSWINDOW",
               aWindow->GetWindowHandle()));
      break;
    case IMN_SETCANDIDATEPOS:
      MOZ_LOG(gIMELog, LogLevel::Info,
              ("IMMHandler::OnIMENotify, hWnd=%p, IMN_SETCANDIDATEPOS, "
               "lParam=%08" PRIxLPTR,
               aWindow->GetWindowHandle(), lParam));
      break;
    case IMN_SETCOMPOSITIONFONT:
      MOZ_LOG(gIMELog, LogLevel::Info,
              ("IMMHandler::OnIMENotify, hWnd=%p, IMN_SETCOMPOSITIONFONT",
               aWindow->GetWindowHandle()));
      break;
    case IMN_SETCOMPOSITIONWINDOW:
      MOZ_LOG(gIMELog, LogLevel::Info,
              ("IMMHandler::OnIMENotify, hWnd=%p, IMN_SETCOMPOSITIONWINDOW",
               aWindow->GetWindowHandle()));
      break;
    case IMN_SETCONVERSIONMODE:
      MOZ_LOG(gIMELog, LogLevel::Info,
              ("IMMHandler::OnIMENotify, hWnd=%p, IMN_SETCONVERSIONMODE",
               aWindow->GetWindowHandle()));
      break;
    case IMN_SETOPENSTATUS:
      MOZ_LOG(gIMELog, LogLevel::Info,
              ("IMMHandler::OnIMENotify, hWnd=%p, IMN_SETOPENSTATUS",
               aWindow->GetWindowHandle()));
      break;
    case IMN_SETSENTENCEMODE:
      MOZ_LOG(gIMELog, LogLevel::Info,
              ("IMMHandler::OnIMENotify, hWnd=%p, IMN_SETSENTENCEMODE",
               aWindow->GetWindowHandle()));
      break;
    case IMN_SETSTATUSWINDOWPOS:
      MOZ_LOG(gIMELog, LogLevel::Info,
              ("IMMHandler::OnIMENotify, hWnd=%p, IMN_SETSTATUSWINDOWPOS",
               aWindow->GetWindowHandle()));
      break;
    case IMN_PRIVATE:
      MOZ_LOG(gIMELog, LogLevel::Info,
              ("IMMHandler::OnIMENotify, hWnd=%p, IMN_PRIVATE",
               aWindow->GetWindowHandle()));
      break;
  }

  // not implement yet
  aResult.mConsumed = false;
  return true;
}

bool IMMHandler::OnIMERequest(nsWindow* aWindow, WPARAM wParam, LPARAM lParam,
                              MSGResult& aResult) {
  switch (wParam) {
    case IMR_RECONVERTSTRING:
      MOZ_LOG(gIMELog, LogLevel::Info,
              ("IMMHandler::OnIMERequest, hWnd=%p, IMR_RECONVERTSTRING",
               aWindow->GetWindowHandle()));
      aResult.mConsumed = HandleReconvert(aWindow, lParam, &aResult.mResult);
      return true;
    case IMR_QUERYCHARPOSITION:
      MOZ_LOG(gIMELog, LogLevel::Info,
              ("IMMHandler::OnIMERequest, hWnd=%p, IMR_QUERYCHARPOSITION",
               aWindow->GetWindowHandle()));
      aResult.mConsumed =
          HandleQueryCharPosition(aWindow, lParam, &aResult.mResult);
      return true;
    case IMR_DOCUMENTFEED:
      MOZ_LOG(gIMELog, LogLevel::Info,
              ("IMMHandler::OnIMERequest, hWnd=%p, IMR_DOCUMENTFEED",
               aWindow->GetWindowHandle()));
      aResult.mConsumed = HandleDocumentFeed(aWindow, lParam, &aResult.mResult);
      return true;
    default:
      MOZ_LOG(gIMELog, LogLevel::Info,
              ("IMMHandler::OnIMERequest, hWnd=%p, wParam=%08zx",
               aWindow->GetWindowHandle(), wParam));
      aResult.mConsumed = false;
      return true;
  }
}

// static
bool IMMHandler::OnIMESelect(nsWindow* aWindow, WPARAM wParam, LPARAM lParam,
                             MSGResult& aResult) {
  MOZ_LOG(
      gIMELog, LogLevel::Info,
      ("IMMHandler::OnIMESelect, hWnd=%p, wParam=%08zx, lParam=%08" PRIxLPTR,
       aWindow->GetWindowHandle(), wParam, lParam));

  // not implement yet
  aResult.mConsumed = false;
  return true;
}

// static
bool IMMHandler::OnIMESetContext(nsWindow* aWindow, WPARAM wParam,
                                 LPARAM lParam, MSGResult& aResult) {
  MOZ_LOG(gIMELog, LogLevel::Info,
          ("IMMHandler::OnIMESetContext, hWnd=%p, %s, lParam=%08" PRIxLPTR,
           aWindow->GetWindowHandle(), wParam ? "Active" : "Deactive", lParam));

  aResult.mConsumed = false;

  // NOTE: If the aWindow is top level window of the composing window because
  // when a window on deactive window gets focus, WM_IME_SETCONTEXT (wParam is
  // TRUE) is sent to the top level window first.  After that,
  // WM_IME_SETCONTEXT (wParam is FALSE) is sent to the top level window.
  // Finally, WM_IME_SETCONTEXT (wParam is TRUE) is sent to the focused window.
  // The top level window never becomes composing window, so, we can ignore
  // the WM_IME_SETCONTEXT on the top level window.
  if (IsTopLevelWindowOfComposition(aWindow)) {
    MOZ_LOG(gIMELog, LogLevel::Info,
            ("  IMMHandler::OnIMESetContext, hWnd=%p is top level window",
             aWindow->GetWindowHandle()));
    return true;
  }

  // When IME context is activating on another window,
  // we should commit the old composition on the old window.
  bool cancelComposition = false;
  if (wParam && gIMMHandler) {
    cancelComposition = gIMMHandler->CommitCompositionOnPreviousWindow(aWindow);
  }

  if (wParam && (lParam & ISC_SHOWUICOMPOSITIONWINDOW) &&
      ShouldDrawCompositionStringOurselves()) {
    MOZ_LOG(gIMELog, LogLevel::Info,
            ("  IMMHandler::OnIMESetContext, ISC_SHOWUICOMPOSITIONWINDOW is "
             "removed"));
    lParam &= ~ISC_SHOWUICOMPOSITIONWINDOW;
  }

  // We should sent WM_IME_SETCONTEXT to the DefWndProc here because the
  // ancestor windows shouldn't receive this message.  If they receive the
  // message, we cannot know whether which window is the target of the message.
  aResult.mResult = ::DefWindowProc(aWindow->GetWindowHandle(),
                                    WM_IME_SETCONTEXT, wParam, lParam);

  // Cancel composition on the new window if we committed our composition on
  // another window.
  if (cancelComposition) {
    CancelComposition(aWindow, true);
  }

  aResult.mConsumed = true;
  return true;
}

bool IMMHandler::OnChar(nsWindow* aWindow, WPARAM wParam, LPARAM lParam,
                        MSGResult& aResult) {
  // The return value must be same as aResult.mConsumed because only when we
  // consume the message, the caller shouldn't do anything anymore but
  // otherwise, the caller should handle the message.
  aResult.mConsumed = false;
  if (IsIMECharRecordsEmpty()) {
    return aResult.mConsumed;
  }
  WPARAM recWParam;
  LPARAM recLParam;
  DequeueIMECharRecords(recWParam, recLParam);
  MOZ_LOG(
      gIMELog, LogLevel::Info,
      ("IMMHandler::OnChar, aWindow=%p, wParam=%08zx, lParam=%08" PRIxLPTR ", "
       "recorded: wParam=%08zx, lParam=%08" PRIxLPTR,
       aWindow->GetWindowHandle(), wParam, lParam, recWParam, recLParam));
  // If an unexpected char message comes, we should reset the records,
  // of course, this shouldn't happen.
  if (recWParam != wParam || recLParam != lParam) {
    ResetIMECharRecords();
    return aResult.mConsumed;
  }
  // Eat the char message which is caused by WM_IME_CHAR because we should
  // have processed the IME messages, so, this message could be come from
  // a windowless plug-in.
  aResult.mConsumed = true;
  return aResult.mConsumed;
}

/****************************************************************************
 * others
 ****************************************************************************/

TextEventDispatcher* IMMHandler::GetTextEventDispatcherFor(nsWindow* aWindow) {
  return aWindow == mComposingWindow && mDispatcher
             ? mDispatcher.get()
             : aWindow->GetTextEventDispatcher();
}

void IMMHandler::HandleStartComposition(nsWindow* aWindow,
                                        const IMEContext& aContext) {
  MOZ_ASSERT(!mIsComposing,
             "HandleStartComposition is called but mIsComposing is TRUE");

  const Maybe<ContentSelection>& contentSelection =
      GetContentSelectionWithQueryIfNothing(aWindow);
  if (contentSelection.isNothing()) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("  IMMHandler::HandleStartComposition, FAILED, due to "
             "Selection::GetContentSelectionWithQueryIfNothing() failure"));
    return;
  }
  if (!contentSelection->HasRange()) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("  IMMHandler::HandleStartComposition, FAILED, due to "
             "there is no selection"));
    return;
  }

  AdjustCompositionFont(aWindow, aContext, contentSelection->WritingModeRef());

  mCompositionStart = contentSelection->OffsetAndDataRef().StartOffset();
  mCursorPosition = NO_IME_CARET;

  RefPtr<TextEventDispatcher> dispatcher = GetTextEventDispatcherFor(aWindow);
  nsresult rv = dispatcher->BeginNativeInputTransaction();
  if (NS_WARN_IF(NS_FAILED(rv))) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("  IMMHandler::HandleStartComposition, FAILED due to "
             "TextEventDispatcher::BeginNativeInputTransaction() failure"));
    return;
  }

  // The composition may end during a call of
  // TextEventDispatcher::StartComposition() so that let's set the active
  // composition data before calling it.
  mIsComposing = true;
  mComposingWindow = aWindow;
  mDispatcher = dispatcher;

  WidgetEventTime eventTime = aWindow->CurrentMessageWidgetEventTime();
  nsEventStatus status;
  rv = dispatcher->StartComposition(status, &eventTime);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("  IMMHandler::HandleStartComposition, FAILED, due to "
             "TextEventDispatcher::StartComposition() failure"));
    return;
  }

  MOZ_LOG(gIMELog, LogLevel::Info,
          ("IMMHandler::HandleStartComposition, START composition, "
           "mCompositionStart=%u",
           mCompositionStart));
}

bool IMMHandler::HandleComposition(nsWindow* aWindow,
                                   const IMEContext& aContext, LPARAM lParam) {
  // for bug #60050
  // MS-IME 95/97/98/2000 may send WM_IME_COMPOSITION with non-conversion
  // mode before it send WM_IME_STARTCOMPOSITION.
  // However, ATOK sends a WM_IME_COMPOSITION before WM_IME_STARTCOMPOSITION,
  // and if we access ATOK via some APIs, ATOK will sometimes fail to
  // initialize its state.  If WM_IME_STARTCOMPOSITION is already in the
  // message queue, we should ignore the strange WM_IME_COMPOSITION message and
  // skip to the next.  So, we should look for next composition message
  // (WM_IME_STARTCOMPOSITION or WM_IME_ENDCOMPOSITION or WM_IME_COMPOSITION),
  // and if it's WM_IME_STARTCOMPOSITION, and one more next composition message
  // is WM_IME_COMPOSITION, current IME is ATOK, probably.  Otherwise, we
  // should start composition forcibly.
  if (!mIsComposing) {
    MSG msg1, msg2;
    HWND wnd = aWindow->GetWindowHandle();
    if (WinUtils::PeekMessage(&msg1, wnd, WM_IME_STARTCOMPOSITION,
                              WM_IME_COMPOSITION, PM_NOREMOVE) &&
        msg1.message == WM_IME_STARTCOMPOSITION &&
        WinUtils::PeekMessage(&msg2, wnd, WM_IME_ENDCOMPOSITION,
                              WM_IME_COMPOSITION, PM_NOREMOVE) &&
        msg2.message == WM_IME_COMPOSITION) {
      MOZ_LOG(gIMELog, LogLevel::Info,
              ("IMMHandler::HandleComposition, Ignores due to find a "
               "WM_IME_STARTCOMPOSITION"));
      return ShouldDrawCompositionStringOurselves();
    }
  }

  bool startCompositionMessageHasBeenSent = mIsComposing;

  //
  // This catches a fixed result
  //
  if (IS_COMMITTING_LPARAM(lParam)) {
    if (!mIsComposing) {
      HandleStartComposition(aWindow, aContext);
    }

    GetCompositionString(aContext, GCS_RESULTSTR, mCompositionString);

    MOZ_LOG(gIMELog, LogLevel::Info,
            ("IMMHandler::HandleComposition, GCS_RESULTSTR"));

    HandleEndComposition(aWindow, &mCompositionString);

    if (!IS_COMPOSING_LPARAM(lParam)) {
      return ShouldDrawCompositionStringOurselves();
    }
  }

  //
  // This provides us with a composition string
  //
  if (!mIsComposing) {
    HandleStartComposition(aWindow, aContext);
  }

  //--------------------------------------------------------
  // 1. Get GCS_COMPSTR
  //--------------------------------------------------------
  MOZ_LOG(gIMELog, LogLevel::Info,
          ("IMMHandler::HandleComposition, GCS_COMPSTR"));

  nsAutoString previousCompositionString(mCompositionString);
  GetCompositionString(aContext, GCS_COMPSTR, mCompositionString);

  if (!IS_COMPOSING_LPARAM(lParam)) {
    MOZ_LOG(
        gIMELog, LogLevel::Info,
        ("  IMMHandler::HandleComposition, lParam doesn't indicate composing, "
         "mCompositionString=\"%s\", previousCompositionString=\"%s\"",
         NS_ConvertUTF16toUTF8(mCompositionString).get(),
         NS_ConvertUTF16toUTF8(previousCompositionString).get()));

    // If composition string isn't changed, we can trust the lParam.
    // So, we need to do nothing.
    if (previousCompositionString == mCompositionString) {
      return ShouldDrawCompositionStringOurselves();
    }

    // IME may send WM_IME_COMPOSITION without composing lParam values
    // when composition string becomes empty (e.g., using Backspace key).
    // If composition string is empty, we should dispatch a compositionchange
    // event with empty string and clear the clause information.
    if (mCompositionString.IsEmpty()) {
      mClauseArray.Clear();
      mAttributeArray.Clear();
      mCursorPosition = 0;
      DispatchCompositionChangeEvent(aWindow, aContext);
      return ShouldDrawCompositionStringOurselves();
    }

    // Otherwise, we cannot trust the lParam value.  We might need to
    // dispatch compositionchange event with the latest composition string
    // information.
  }

  // See https://bugzilla.mozilla.org/show_bug.cgi?id=296339
  if (mCompositionString.IsEmpty() && !startCompositionMessageHasBeenSent) {
    // In this case, maybe, the sender is MSPinYin. That sends *only*
    // WM_IME_COMPOSITION with GCS_COMP* and GCS_RESULT* when
    // user inputted the Chinese full stop. So, that doesn't send
    // WM_IME_STARTCOMPOSITION and WM_IME_ENDCOMPOSITION.
    // If WM_IME_STARTCOMPOSITION was not sent and the composition
    // string is null (it indicates the composition transaction ended),
    // WM_IME_ENDCOMPOSITION may not be sent. If so, we cannot run
    // HandleEndComposition() in other place.
    MOZ_LOG(gIMELog, LogLevel::Info,
            ("    IMMHandler::HandleComposition, Aborting GCS_COMPSTR"));
    HandleEndComposition(aWindow);
    return IS_COMMITTING_LPARAM(lParam);
  }

  //--------------------------------------------------------
  // 2. Get GCS_COMPCLAUSE
  //--------------------------------------------------------
  long clauseArrayLength =
      ::ImmGetCompositionStringW(aContext.get(), GCS_COMPCLAUSE, nullptr, 0);
  clauseArrayLength /= sizeof(uint32_t);

  if (clauseArrayLength > 0) {
    nsresult rv = EnsureClauseArray(clauseArrayLength);
    NS_ENSURE_SUCCESS(rv, false);

    // Intelligent ABC IME (Simplified Chinese IME, the code page is 936)
    // will crash in ImmGetCompositionStringW for GCS_COMPCLAUSE (bug 424663).
    // See comment 35 of the bug for the detail. Therefore, we should use A
    // API for it, however, we should not kill Unicode support on all IMEs.
    bool useA_API = !(sIMEProperty & IME_PROP_UNICODE);

    MOZ_LOG(gIMELog, LogLevel::Info,
            ("  IMMHandler::HandleComposition, GCS_COMPCLAUSE, useA_API=%s",
             useA_API ? "TRUE" : "FALSE"));

    long clauseArrayLength2 =
        useA_API ? ::ImmGetCompositionStringA(
                       aContext.get(), GCS_COMPCLAUSE, mClauseArray.Elements(),
                       mClauseArray.Capacity() * sizeof(uint32_t))
                 : ::ImmGetCompositionStringW(
                       aContext.get(), GCS_COMPCLAUSE, mClauseArray.Elements(),
                       mClauseArray.Capacity() * sizeof(uint32_t));
    clauseArrayLength2 /= sizeof(uint32_t);

    if (clauseArrayLength != clauseArrayLength2) {
      MOZ_LOG(gIMELog, LogLevel::Info,
              ("    IMMHandler::HandleComposition, GCS_COMPCLAUSE, "
               "clauseArrayLength=%ld but clauseArrayLength2=%ld",
               clauseArrayLength, clauseArrayLength2));
      if (clauseArrayLength > clauseArrayLength2)
        clauseArrayLength = clauseArrayLength2;
    }

    if (useA_API && clauseArrayLength > 0) {
      // Convert each values of sIMECompClauseArray. The values mean offset of
      // the clauses in ANSI string. But we need the values in Unicode string.
      nsAutoCString compANSIStr;
      if (ConvertToANSIString(mCompositionString, GetKeyboardCodePage(),
                              compANSIStr)) {
        uint32_t maxlen = compANSIStr.Length();
        mClauseArray.SetLength(clauseArrayLength);
        mClauseArray[0] = 0;  // first value must be 0
        for (int32_t i = 1; i < clauseArrayLength; i++) {
          uint32_t len = std::min(mClauseArray[i], maxlen);
          mClauseArray[i] =
              ::MultiByteToWideChar(GetKeyboardCodePage(), MB_PRECOMPOSED,
                                    (LPCSTR)compANSIStr.get(), len, nullptr, 0);
        }
      }
    }
  }
  // compClauseArrayLength may be negative. I.e., ImmGetCompositionStringW
  // may return an error code.
  mClauseArray.SetLength(std::max<long>(0, clauseArrayLength));

  MOZ_LOG(gIMELog, LogLevel::Info,
          ("  IMMHandler::HandleComposition, GCS_COMPCLAUSE, mClauseLength=%zu",
           mClauseArray.Length()));

  //--------------------------------------------------------
  // 3. Get GCS_COMPATTR
  //--------------------------------------------------------
  // This provides us with the attribute string necessary
  // for doing hiliting
  long attrArrayLength =
      ::ImmGetCompositionStringW(aContext.get(), GCS_COMPATTR, nullptr, 0);
  attrArrayLength /= sizeof(uint8_t);

  if (attrArrayLength > 0) {
    nsresult rv = EnsureAttributeArray(attrArrayLength);
    NS_ENSURE_SUCCESS(rv, false);
    attrArrayLength = ::ImmGetCompositionStringW(
        aContext.get(), GCS_COMPATTR, mAttributeArray.Elements(),
        mAttributeArray.Capacity() * sizeof(uint8_t));
  }

  // attrStrLen may be negative. I.e., ImmGetCompositionStringW may return an
  // error code.
  mAttributeArray.SetLength(std::max<long>(0, attrArrayLength));

  MOZ_LOG(
      gIMELog, LogLevel::Info,
      ("  IMMHandler::HandleComposition, GCS_COMPATTR, mAttributeLength=%zu",
       mAttributeArray.Length()));

  //--------------------------------------------------------
  // 4. Get GCS_CURSOPOS
  //--------------------------------------------------------
  // Some IMEs (e.g., the standard IME for Korean) don't have caret position.
  if (lParam & GCS_CURSORPOS) {
    mCursorPosition =
        ::ImmGetCompositionStringW(aContext.get(), GCS_CURSORPOS, nullptr, 0);
    if (mCursorPosition < 0) {
      mCursorPosition = NO_IME_CARET;  // The result is error
    }
  } else {
    mCursorPosition = NO_IME_CARET;
  }

  NS_ASSERTION(mCursorPosition <= (long)mCompositionString.Length(),
               "illegal pos");

  MOZ_LOG(gIMELog, LogLevel::Info,
          ("  IMMHandler::HandleComposition, GCS_CURSORPOS, mCursorPosition=%d",
           mCursorPosition));

  //--------------------------------------------------------
  // 5. Send the compositionchange event
  //--------------------------------------------------------
  DispatchCompositionChangeEvent(aWindow, aContext);

  return ShouldDrawCompositionStringOurselves();
}

void IMMHandler::HandleEndComposition(nsWindow* aWindow,
                                      const nsAString* aCommitString) {
  MOZ_ASSERT(mIsComposing,
             "HandleEndComposition is called but mIsComposing is FALSE");

  MOZ_LOG(gIMELog, LogLevel::Info,
          ("IMMHandler::HandleEndComposition(aWindow=0x%p, aCommitString=0x%p "
           "(\"%s\"))",
           aWindow, aCommitString,
           aCommitString ? NS_ConvertUTF16toUTF8(*aCommitString).get() : ""));

  IMEHandler::MaybeDestroyNativeCaret();

  RefPtr<TextEventDispatcher> dispatcher = GetTextEventDispatcherFor(aWindow);
  nsresult rv = dispatcher->BeginNativeInputTransaction();
  if (NS_WARN_IF(NS_FAILED(rv))) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("  IMMHandler::HandleEndComposition, FAILED due to "
             "TextEventDispatcher::BeginNativeInputTransaction() failure"));
    return;
  }
  WidgetEventTime eventTime = aWindow->CurrentMessageWidgetEventTime();
  nsEventStatus status;
  rv = dispatcher->CommitComposition(status, aCommitString, &eventTime);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("  IMMHandler::HandleStartComposition, FAILED, due to "
             "TextEventDispatcher::CommitComposition() failure"));
    return;
  }
  ClearComposingData();
}

bool IMMHandler::HandleReconvert(nsWindow* aWindow, LPARAM lParam,
                                 LRESULT* oResult) {
  *oResult = 0;
  RECONVERTSTRING* pReconv = reinterpret_cast<RECONVERTSTRING*>(lParam);

  const Maybe<ContentSelection>& contentSelection =
      GetContentSelectionWithQueryIfNothing(aWindow);
  if (contentSelection.isNothing()) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("IMMHandler::HandleReconvert, FAILED, due to "
             "Selection::GetContentSelectionWithQueryIfNothing() failure"));
    return false;
  }

  const uint32_t len = contentSelection->HasRange()
                           ? contentSelection->OffsetAndDataRef().Length()
                           : 0u;
  uint32_t needSize = sizeof(RECONVERTSTRING) + len * sizeof(WCHAR);

  if (!pReconv) {
    // Return need size to reconvert.
    if (len == 0) {
      MOZ_LOG(gIMELog, LogLevel::Error,
              ("IMMHandler::HandleReconvert, There are not selected text"));
      return false;
    }
    *oResult = needSize;
    MOZ_LOG(gIMELog, LogLevel::Info,
            ("IMMHandler::HandleReconvert, succeeded, result=%" PRIdLPTR,
             *oResult));
    return true;
  }

  if (pReconv->dwSize < needSize) {
    MOZ_LOG(gIMELog, LogLevel::Info,
            ("IMMHandler::HandleReconvert, FAILED, pReconv->dwSize=%ld, "
             "needSize=%u",
             pReconv->dwSize, needSize));
    return false;
  }

  *oResult = needSize;

  // Fill reconvert struct
  pReconv->dwVersion = 0;
  pReconv->dwStrLen = len;
  pReconv->dwStrOffset = sizeof(RECONVERTSTRING);
  pReconv->dwCompStrLen = len;
  pReconv->dwCompStrOffset = 0;
  pReconv->dwTargetStrLen = len;
  pReconv->dwTargetStrOffset = 0;

  if (len) {
    ::CopyMemory(reinterpret_cast<LPVOID>(lParam + sizeof(RECONVERTSTRING)),
                 contentSelection->OffsetAndDataRef().DataRef().get(),
                 len * sizeof(WCHAR));
  }

  MOZ_LOG(
      gIMELog, LogLevel::Info,
      ("IMMHandler::HandleReconvert, SUCCEEDED, pReconv=%s, result=%" PRIdLPTR,
       GetReconvertStringLog(pReconv).get(), *oResult));

  return true;
}

bool IMMHandler::HandleQueryCharPosition(nsWindow* aWindow, LPARAM lParam,
                                         LRESULT* oResult) {
  uint32_t len = mIsComposing ? mCompositionString.Length() : 0;
  *oResult = false;
  IMECHARPOSITION* pCharPosition = reinterpret_cast<IMECHARPOSITION*>(lParam);
  if (!pCharPosition) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("IMMHandler::HandleQueryCharPosition, FAILED, due to "
             "pCharPosition is null"));
    return false;
  }
  if (pCharPosition->dwSize < sizeof(IMECHARPOSITION)) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("IMMHandler::HandleReconvert, FAILED, pCharPosition->dwSize=%lu, "
             "sizeof(IMECHARPOSITION)=%zu",
             pCharPosition->dwSize, sizeof(IMECHARPOSITION)));
    return false;
  }
  if (::GetFocus() != aWindow->GetWindowHandle()) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("IMMHandler::HandleReconvert, FAILED, ::GetFocus()=%p, "
             "OurWindowHandle=%p",
             ::GetFocus(), aWindow->GetWindowHandle()));
    return false;
  }
  if (pCharPosition->dwCharPos > len) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("IMMHandler::HandleQueryCharPosition, FAILED, "
             "pCharPosition->dwCharPos=%ld, len=%u",
             pCharPosition->dwCharPos, len));
    return false;
  }

  LayoutDeviceIntRect r;
  bool ret =
      GetCharacterRectOfSelectedTextAt(aWindow, pCharPosition->dwCharPos, r);
  NS_ENSURE_TRUE(ret, false);

  LayoutDeviceIntRect screenRect;
  // We always need top level window that is owner window of the popup window
  // even if the content of the popup window has focus.
  ResolveIMECaretPos(aWindow->GetTopLevelWindow(false), r, nullptr, screenRect);

  // XXX This might need to check writing mode.  However, MSDN doesn't explain
  //     how to set the values in vertical writing mode. Additionally, IME
  //     doesn't work well with top-left of the character (this is explicitly
  //     documented) and its horizontal width.  So, it might be better to set
  //     top-right corner of the character and horizontal width, but we're not
  //     sure if it doesn't cause any problems with a lot of IMEs...
  pCharPosition->pt.x = screenRect.X();
  pCharPosition->pt.y = screenRect.Y();

  pCharPosition->cLineHeight = r.Height();

  WidgetQueryContentEvent queryEditorRectEvent(true, eQueryEditorRect, aWindow);
  aWindow->InitEvent(queryEditorRectEvent);
  DispatchEvent(aWindow, queryEditorRectEvent);
  if (NS_WARN_IF(queryEditorRectEvent.Failed())) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("  IMMHandler::HandleQueryCharPosition, eQueryEditorRect failed"));
    ::GetWindowRect(aWindow->GetWindowHandle(), &pCharPosition->rcDocument);
  } else {
    LayoutDeviceIntRect editorRectInWindow = queryEditorRectEvent.mReply->mRect;
    nsWindow* window = !!queryEditorRectEvent.mReply->mFocusedWidget
                           ? static_cast<nsWindow*>(
                                 queryEditorRectEvent.mReply->mFocusedWidget)
                           : aWindow;
    LayoutDeviceIntRect editorRectInScreen;
    ResolveIMECaretPos(window, editorRectInWindow, nullptr, editorRectInScreen);
    ::SetRect(&pCharPosition->rcDocument, editorRectInScreen.X(),
              editorRectInScreen.Y(), editorRectInScreen.XMost(),
              editorRectInScreen.YMost());
  }

  *oResult = TRUE;

  MOZ_LOG(
      gIMELog, LogLevel::Info,
      ("IMMHandler::HandleQueryCharPosition, SUCCEEDED, pCharPosition={ "
       "pt={ x=%ld, y=%ld }, cLineHeight=%d, rcDocument={ left=%ld, top=%ld, "
       "right=%ld, bottom=%ld } }",
       pCharPosition->pt.x, pCharPosition->pt.y, pCharPosition->cLineHeight,
       pCharPosition->rcDocument.left, pCharPosition->rcDocument.top,
       pCharPosition->rcDocument.right, pCharPosition->rcDocument.bottom));
  return true;
}

bool IMMHandler::HandleDocumentFeed(nsWindow* aWindow, LPARAM lParam,
                                    LRESULT* oResult) {
  *oResult = 0;
  RECONVERTSTRING* pReconv = reinterpret_cast<RECONVERTSTRING*>(lParam);

  LayoutDeviceIntPoint point(0, 0);

  bool hasCompositionString =
      mIsComposing && ShouldDrawCompositionStringOurselves();

  int32_t targetOffset, targetLength;
  if (!hasCompositionString) {
    const Maybe<ContentSelection>& contentSelection =
        GetContentSelectionWithQueryIfNothing(aWindow);
    if (contentSelection.isNothing()) {
      MOZ_LOG(gIMELog, LogLevel::Error,
              ("IMMHandler::HandleDocumentFeed, FAILED, due to "
               "Selection::GetContentSelectionWithQueryIfNothing() failure"));
      return false;
    }
    if (contentSelection->HasRange()) {
      targetOffset = static_cast<int32_t>(
          contentSelection->OffsetAndDataRef().StartOffset());
      targetLength =
          static_cast<int32_t>(contentSelection->OffsetAndDataRef().Length());
    } else {
      // If there is no selection range, let's return all text in the editor.
      targetOffset = 0;
      targetLength = INT32_MAX;
    }
  } else {
    targetOffset = int32_t(mCompositionStart);
    targetLength = int32_t(mCompositionString.Length());
  }

  // XXX nsString::Find and nsString::RFind take int32_t for offset, so,
  //     we cannot support this message when the current offset is larger than
  //     INT32_MAX.
  if (targetOffset < 0 || targetLength < 0 || targetOffset + targetLength < 0) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("IMMHandler::HandleDocumentFeed, FAILED, "
             "due to the selection is out of range"));
    return false;
  }

  // Get all contents of the focused editor.
  WidgetQueryContentEvent queryTextContentEvent(true, eQueryTextContent,
                                                aWindow);
  queryTextContentEvent.InitForQueryTextContent(0, UINT32_MAX);
  aWindow->InitEvent(queryTextContentEvent, &point);
  DispatchEvent(aWindow, queryTextContentEvent);
  if (NS_WARN_IF(queryTextContentEvent.Failed())) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("IMMHandler::HandleDocumentFeed, FAILED, "
             "due to eQueryTextContent failure"));
    return false;
  }

  nsAutoString str(queryTextContentEvent.mReply->DataRef());
  if (targetOffset > static_cast<int32_t>(str.Length())) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("  IMMHandler::HandleDocumentFeed, FAILED, "
             "due to the caret offset is invalid"));
    return false;
  }

  // Get the focused paragraph, we decide that it starts from the previous CRLF
  // (or start of the editor) to the next one (or the end of the editor).
  int32_t paragraphStart = 0;
  if (targetOffset > 0) {
    paragraphStart = Substring(str, 0, targetOffset).RFind(u"\n") + 1;
  }
  int32_t paragraphEnd = str.Find(u"\r", targetOffset + targetLength);
  if (paragraphEnd < 0) {
    paragraphEnd = str.Length();
  }
  nsDependentSubstring paragraph(str, paragraphStart,
                                 paragraphEnd - paragraphStart);

  uint32_t len = paragraph.Length();
  uint32_t needSize = sizeof(RECONVERTSTRING) + len * sizeof(WCHAR);

  if (!pReconv) {
    *oResult = needSize;
    MOZ_LOG(gIMELog, LogLevel::Info,
            ("IMMHandler::HandleDocumentFeed, succeeded, result=%" PRIdLPTR,
             *oResult));
    return true;
  }

  if (pReconv->dwSize < needSize) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("IMMHandler::HandleDocumentFeed, FAILED, "
             "pReconv->dwSize=%ld, needSize=%u",
             pReconv->dwSize, needSize));
    return false;
  }

  // Fill reconvert struct
  pReconv->dwVersion = 0;
  pReconv->dwStrLen = len;
  pReconv->dwStrOffset = sizeof(RECONVERTSTRING);
  if (hasCompositionString) {
    pReconv->dwCompStrLen = targetLength;
    pReconv->dwCompStrOffset = (targetOffset - paragraphStart) * sizeof(WCHAR);
    // Set composition target clause information
    uint32_t offset, length;
    if (!GetTargetClauseRange(&offset, &length)) {
      MOZ_LOG(gIMELog, LogLevel::Error,
              ("IMMHandler::HandleDocumentFeed, FAILED, "
               "due to IMMHandler::GetTargetClauseRange() failure"));
      return false;
    }
    pReconv->dwTargetStrLen = length;
    pReconv->dwTargetStrOffset = (offset - paragraphStart) * sizeof(WCHAR);
  } else {
    pReconv->dwTargetStrLen = targetLength;
    pReconv->dwTargetStrOffset =
        (targetOffset - paragraphStart) * sizeof(WCHAR);
    // There is no composition string, so, the length is zero but we should
    // set the cursor offset to the composition str offset.
    pReconv->dwCompStrLen = 0;
    pReconv->dwCompStrOffset = pReconv->dwTargetStrOffset;
  }

  *oResult = needSize;
  ::CopyMemory(reinterpret_cast<LPVOID>(lParam + sizeof(RECONVERTSTRING)),
               paragraph.BeginReading(), len * sizeof(WCHAR));

  MOZ_LOG(gIMELog, LogLevel::Info,
          ("IMMHandler::HandleDocumentFeed, SUCCEEDED, pReconv=%s, "
           "result=%" PRIdLPTR,
           GetReconvertStringLog(pReconv).get(), *oResult));

  return true;
}

bool IMMHandler::CommitCompositionOnPreviousWindow(nsWindow* aWindow) {
  if (!mComposingWindow || mComposingWindow == aWindow) {
    return false;
  }

  MOZ_LOG(gIMELog, LogLevel::Info,
          ("IMMHandler::CommitCompositionOnPreviousWindow, mIsComposing=%s",
           TrueOrFalse(mIsComposing)));

  // If we have composition, we should dispatch composition events internally.
  if (mIsComposing) {
    IMEContext context(mComposingWindow);
    NS_ASSERTION(context.IsValid(), "IME context must be valid");

    HandleEndComposition(mComposingWindow);
    return true;
  }

  return false;
}

static TextRangeType PlatformToNSAttr(uint8_t aAttr) {
  switch (aAttr) {
    case ATTR_INPUT_ERROR:
    // case ATTR_FIXEDCONVERTED:
    case ATTR_INPUT:
      return TextRangeType::eRawClause;
    case ATTR_CONVERTED:
      return TextRangeType::eConvertedClause;
    case ATTR_TARGET_NOTCONVERTED:
      return TextRangeType::eSelectedRawClause;
    case ATTR_TARGET_CONVERTED:
      return TextRangeType::eSelectedClause;
    default:
      NS_ASSERTION(false, "unknown attribute");
      return TextRangeType::eCaret;
  }
}

// static
void IMMHandler::DispatchEvent(nsWindow* aWindow, WidgetGUIEvent& aEvent) {
  MOZ_LOG(
      gIMELog, LogLevel::Info,
      ("IMMHandler::DispatchEvent(aWindow=0x%p, aEvent={ mMessage=%s }, "
       "aWindow->Destroyed()=%s",
       aWindow, ToChar(aEvent.mMessage), TrueOrFalse(aWindow->Destroyed())));

  if (aWindow->Destroyed()) {
    return;
  }

  aWindow->DispatchWindowEvent(aEvent);
}

void IMMHandler::DispatchCompositionChangeEvent(nsWindow* aWindow,
                                                const IMEContext& aContext) {
  NS_ASSERTION(mIsComposing, "conflict state");
  MOZ_LOG(gIMELog, LogLevel::Info,
          ("IMMHandler::DispatchCompositionChangeEvent"));

  // If we don't need to draw composition string ourselves, we don't need to
  // fire compositionchange event during composing.
  if (!ShouldDrawCompositionStringOurselves()) {
    // But we need to adjust composition window pos and native caret pos, here.
    SetIMERelatedWindowsPos(aWindow, aContext);
    return;
  }

  RefPtr<nsWindow> kungFuDeathGrip(aWindow);
  RefPtr<TextEventDispatcher> dispatcher = GetTextEventDispatcherFor(aWindow);
  nsresult rv = dispatcher->BeginNativeInputTransaction();
  if (NS_WARN_IF(NS_FAILED(rv))) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("  IMMHandler::DispatchCompositionChangeEvent, FAILED due to "
             "TextEventDispatcher::BeginNativeInputTransaction() failure"));
    return;
  }

  // NOTE: Calling SetIMERelatedWindowsPos() from this method will be failure
  //       in e10s mode.  compositionchange event will notify this of
  //       NOTIFY_IME_OF_COMPOSITION_EVENT_HANDLED, then
  //       SetIMERelatedWindowsPos() will be called.

  // XXX Sogou (Simplified Chinese IME) returns contradictory values:
  //     The cursor position is actual cursor position. However, other values
  //     (composition string and attributes) are empty.

  if (mCompositionString.IsEmpty()) {
    // Don't append clause information if composition string is empty.
  } else if (mClauseArray.IsEmpty()) {
    // Some IMEs don't return clause array information, then, we assume that
    // all characters in the composition string are in one clause.
    MOZ_LOG(gIMELog, LogLevel::Info,
            ("  IMMHandler::DispatchCompositionChangeEvent, "
             "mClauseArray.Length()=0"));
    rv = dispatcher->SetPendingComposition(mCompositionString, nullptr);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      MOZ_LOG(gIMELog, LogLevel::Error,
              ("  IMMHandler::DispatchCompositionChangeEvent, FAILED due to"
               "TextEventDispatcher::SetPendingComposition() failure"));
      return;
    }
  } else {
    // iterate over the attributes
    rv = dispatcher->SetPendingCompositionString(mCompositionString);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      MOZ_LOG(gIMELog, LogLevel::Error,
              ("  IMMHandler::DispatchCompositionChangeEvent, FAILED due to"
               "TextEventDispatcher::SetPendingCompositionString() failure"));
      return;
    }
    uint32_t lastOffset = 0;
    for (uint32_t i = 0; i < mClauseArray.Length() - 1; i++) {
      uint32_t current = mClauseArray[i + 1];
      if (current > mCompositionString.Length()) {
        MOZ_LOG(gIMELog, LogLevel::Info,
                ("  IMMHandler::DispatchCompositionChangeEvent, "
                 "mClauseArray[%u]=%u. "
                 "This is larger than mCompositionString.Length()=%zu",
                 i + 1, current, mCompositionString.Length()));
        current = int32_t(mCompositionString.Length());
      }

      uint32_t length = current - lastOffset;
      if (NS_WARN_IF(lastOffset >= mAttributeArray.Length())) {
        MOZ_LOG(gIMELog, LogLevel::Error,
                ("  IMMHandler::DispatchCompositionChangeEvent, FAILED due to "
                 "invalid data of mClauseArray or mAttributeArray"));
        return;
      }
      TextRangeType textRangeType =
          PlatformToNSAttr(mAttributeArray[lastOffset]);
      rv = dispatcher->AppendClauseToPendingComposition(length, textRangeType);
      if (NS_WARN_IF(NS_FAILED(rv))) {
        MOZ_LOG(gIMELog, LogLevel::Error,
                ("  IMMHandler::DispatchCompositionChangeEvent, FAILED due to"
                 "TextEventDispatcher::AppendClauseToPendingComposition() "
                 "failure"));
        return;
      }

      lastOffset = current;

      MOZ_LOG(gIMELog, LogLevel::Info,
              ("  IMMHandler::DispatchCompositionChangeEvent, index=%u, "
               "rangeType=%s, range length=%u",
               i, ToChar(textRangeType), length));
    }
  }

  if (mCursorPosition == NO_IME_CARET) {
    MOZ_LOG(gIMELog, LogLevel::Info,
            ("  IMMHandler::DispatchCompositionChangeEvent, no caret"));
  } else {
    uint32_t cursor = static_cast<uint32_t>(mCursorPosition);
    if (cursor > mCompositionString.Length()) {
      MOZ_LOG(gIMELog, LogLevel::Info,
              ("  IMMHandler::CreateTextRangeArray, mCursorPosition=%d. "
               "This is larger than mCompositionString.Length()=%zu",
               mCursorPosition, mCompositionString.Length()));
      cursor = mCompositionString.Length();
    }

    // If caret is in the target clause, the target clause will be painted as
    // normal selection range.  Since caret shouldn't be in selection range on
    // Windows, we shouldn't append caret range in such case.
    const TextRangeArray* clauses = dispatcher->GetPendingCompositionClauses();
    const TextRange* targetClause =
        clauses ? clauses->GetTargetClause() : nullptr;
    if (targetClause && cursor >= targetClause->mStartOffset &&
        cursor <= targetClause->mEndOffset) {
      // Forget the caret position specified by IME since Gecko's caret position
      // will be at the end of composition string.
      mCursorPosition = NO_IME_CARET;
      MOZ_LOG(gIMELog, LogLevel::Info,
              ("  IMMHandler::CreateTextRangeArray, no caret due to it's in "
               "the target clause, now, mCursorPosition is NO_IME_CARET"));
    }

    if (mCursorPosition != NO_IME_CARET) {
      rv = dispatcher->SetCaretInPendingComposition(cursor, 0);
      if (NS_WARN_IF(NS_FAILED(rv))) {
        MOZ_LOG(
            gIMELog, LogLevel::Error,
            ("  IMMHandler::DispatchCompositionChangeEvent, FAILED due to"
             "TextEventDispatcher::SetCaretInPendingComposition() failure"));
        return;
      }
    }
  }

  WidgetEventTime eventTime = aWindow->CurrentMessageWidgetEventTime();
  nsEventStatus status;
  rv = dispatcher->FlushPendingComposition(status, &eventTime);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("  IMMHandler::DispatchCompositionChangeEvent, FAILED due to"
             "TextEventDispatcher::FlushPendingComposition() failure"));
    return;
  }
}

void IMMHandler::GetCompositionString(const IMEContext& aContext, DWORD aIndex,
                                      nsAString& aCompositionString) const {
  aCompositionString.Truncate();

  // Retrieve the size of the required output buffer.
  long lRtn = ::ImmGetCompositionStringW(aContext.get(), aIndex, nullptr, 0);
  if (lRtn < 0 || !aCompositionString.SetLength((lRtn / sizeof(WCHAR)) + 1,
                                                mozilla::fallible)) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("IMMHandler::GetCompositionString, FAILED, due to OOM"));
    return;  // Error or out of memory.
  }

  // Actually retrieve the composition string information.
  lRtn = ::ImmGetCompositionStringW(aContext.get(), aIndex,
                                    (LPVOID)aCompositionString.BeginWriting(),
                                    lRtn + sizeof(WCHAR));
  aCompositionString.SetLength(lRtn / sizeof(WCHAR));

  MOZ_LOG(
      gIMELog, LogLevel::Info,
      ("IMMHandler::GetCompositionString, succeeded, aCompositionString=\"%s\"",
       NS_ConvertUTF16toUTF8(aCompositionString).get()));
}

bool IMMHandler::GetTargetClauseRange(uint32_t* aOffset, uint32_t* aLength) {
  NS_ENSURE_TRUE(aOffset, false);
  NS_ENSURE_TRUE(mIsComposing, false);
  NS_ENSURE_TRUE(ShouldDrawCompositionStringOurselves(), false);

  bool found = false;
  *aOffset = mCompositionStart;
  for (uint32_t i = 0; i < mAttributeArray.Length(); i++) {
    if (mAttributeArray[i] == ATTR_TARGET_NOTCONVERTED ||
        mAttributeArray[i] == ATTR_TARGET_CONVERTED) {
      *aOffset = mCompositionStart + i;
      found = true;
      break;
    }
  }

  if (!aLength) {
    return true;
  }

  if (!found) {
    // The all composition string is targetted when there is no ATTR_TARGET_*
    // clause. E.g., there is only ATTR_INPUT
    *aLength = mCompositionString.Length();
    return true;
  }

  uint32_t offsetInComposition = *aOffset - mCompositionStart;
  *aLength = mCompositionString.Length() - offsetInComposition;
  for (uint32_t i = offsetInComposition; i < mAttributeArray.Length(); i++) {
    if (mAttributeArray[i] != ATTR_TARGET_NOTCONVERTED &&
        mAttributeArray[i] != ATTR_TARGET_CONVERTED) {
      *aLength = i - offsetInComposition;
      break;
    }
  }
  return true;
}

bool IMMHandler::ConvertToANSIString(const nsString& aStr, UINT aCodePage,
                                     nsACString& aANSIStr) {
  int len = ::WideCharToMultiByte(aCodePage, 0, (LPCWSTR)aStr.get(),
                                  aStr.Length(), nullptr, 0, nullptr, nullptr);
  NS_ENSURE_TRUE(len >= 0, false);

  if (!aANSIStr.SetLength(len, mozilla::fallible)) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("IMMHandler::ConvertToANSIString, FAILED, due to OOM"));
    return false;
  }
  ::WideCharToMultiByte(aCodePage, 0, (LPCWSTR)aStr.get(), aStr.Length(),
                        (LPSTR)aANSIStr.BeginWriting(), len, nullptr, nullptr);
  return true;
}

bool IMMHandler::GetCharacterRectOfSelectedTextAt(
    nsWindow* aWindow, uint32_t aOffset, LayoutDeviceIntRect& aCharRect,
    WritingMode* aWritingMode) {
  LayoutDeviceIntPoint point(0, 0);

  const Maybe<ContentSelection>& contentSelection =
      GetContentSelectionWithQueryIfNothing(aWindow);
  if (contentSelection.isNothing()) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("IMMHandler::GetCharacterRectOfSelectedTextAt, FAILED, due to "
             "Selection::GetContentSelectionWithQueryIfNothing() failure"));
    return false;
  }

  // If there is neither a selection range nor composition string, cannot return
  // character rect, of course.
  if (!contentSelection->HasRange() && !mIsComposing) {
    MOZ_LOG(gIMELog, LogLevel::Warning,
            ("IMMHandler::GetCharacterRectOfSelectedTextAt, FAILED, due to "
             "there is neither a selection range nor composition string"));
    return false;
  }

  // If the offset is larger than the end of composition string or selected
  // string, we should return false since such case must be a bug of the caller
  // or the active IME.  If it's an IME's bug, we need to set targetLength to
  // aOffset.
  const uint32_t targetLength =
      mIsComposing ? mCompositionString.Length()
                   : contentSelection->OffsetAndDataRef().Length();
  if (NS_WARN_IF(aOffset > targetLength)) {
    MOZ_LOG(
        gIMELog, LogLevel::Error,
        ("IMMHandler::GetCharacterRectOfSelectedTextAt, FAILED, due to "
         "aOffset is too large (aOffset=%u, targetLength=%u, mIsComposing=%s)",
         aOffset, targetLength, TrueOrFalse(mIsComposing)));
    return false;
  }

  // If there is caret, we might be able to use caret rect.
  uint32_t caretOffset = UINT32_MAX;
  // There is a caret only when the normal selection is collapsed.
  if (contentSelection.isNothing() ||
      contentSelection->OffsetAndDataRef().IsDataEmpty()) {
    if (mIsComposing) {
      // If it's composing, mCursorPosition is the offset to caret in
      // the composition string.
      if (mCursorPosition != NO_IME_CARET) {
        MOZ_ASSERT(mCursorPosition >= 0);
        caretOffset = mCursorPosition;
      } else if (!ShouldDrawCompositionStringOurselves() ||
                 mCompositionString.IsEmpty()) {
        // Otherwise, if there is no composition string, we should assume that
        // there is a caret at the start of composition string.
        caretOffset = 0;
      }
    } else {
      // If there is no composition, the selection offset is the caret offset.
      caretOffset = 0;
    }
  }

  // If there is a caret and retrieving offset is same as the caret offset,
  // we should use the caret rect.
  if (aOffset != caretOffset) {
    WidgetQueryContentEvent queryTextRectEvent(true, eQueryTextRect, aWindow);
    WidgetQueryContentEvent::Options options;
    options.mRelativeToInsertionPoint = true;
    queryTextRectEvent.InitForQueryTextRect(aOffset, 1, options);
    aWindow->InitEvent(queryTextRectEvent, &point);
    DispatchEvent(aWindow, queryTextRectEvent);
    if (queryTextRectEvent.Succeeded()) {
      aCharRect = queryTextRectEvent.mReply->mRect;
      if (aWritingMode) {
        *aWritingMode = queryTextRectEvent.mReply->WritingModeRef();
      }
      MOZ_LOG(
          gIMELog, LogLevel::Debug,
          ("IMMHandler::GetCharacterRectOfSelectedTextAt, Succeeded, "
           "aOffset=%u, aCharRect={ x: %d, y: %d, width: %d, height: %d }, "
           "queryTextRectEvent={ mReply=%s }",
           aOffset, aCharRect.X(), aCharRect.Y(), aCharRect.Width(),
           aCharRect.Height(), ToString(queryTextRectEvent.mReply).c_str()));
      return true;
    }
  }

  return GetCaretRect(aWindow, aCharRect, aWritingMode);
}

bool IMMHandler::GetCaretRect(nsWindow* aWindow,
                              LayoutDeviceIntRect& aCaretRect,
                              WritingMode* aWritingMode) {
  LayoutDeviceIntPoint point(0, 0);

  WidgetQueryContentEvent queryCaretRectEvent(true, eQueryCaretRect, aWindow);
  WidgetQueryContentEvent::Options options;
  options.mRelativeToInsertionPoint = true;
  queryCaretRectEvent.InitForQueryCaretRect(0, options);
  aWindow->InitEvent(queryCaretRectEvent, &point);
  DispatchEvent(aWindow, queryCaretRectEvent);
  if (queryCaretRectEvent.Failed()) {
    MOZ_LOG(
        gIMELog, LogLevel::Info,
        ("IMMHandler::GetCaretRect, FAILED, due to eQueryCaretRect failure"));
    return false;
  }
  aCaretRect = queryCaretRectEvent.mReply->mRect;
  if (aWritingMode) {
    *aWritingMode = queryCaretRectEvent.mReply->WritingModeRef();
  }
  MOZ_LOG(gIMELog, LogLevel::Info,
          ("IMMHandler::GetCaretRect, SUCCEEDED, "
           "aCaretRect={ x: %d, y: %d, width: %d, height: %d }, "
           "queryCaretRectEvent={ mReply=%s }",
           aCaretRect.X(), aCaretRect.Y(), aCaretRect.Width(),
           aCaretRect.Height(), ToString(queryCaretRectEvent.mReply).c_str()));
  return true;
}

bool IMMHandler::SetIMERelatedWindowsPos(nsWindow* aWindow,
                                         const IMEContext& aContext) {
  // Get first character rect of current a normal selected text or a composing
  // string.
  WritingMode writingMode;
  LayoutDeviceIntRect firstSelectedCharRectRelativeToWindow;
  bool ret = GetCharacterRectOfSelectedTextAt(
      aWindow, 0, firstSelectedCharRectRelativeToWindow, &writingMode);
  NS_ENSURE_TRUE(ret, false);
  nsWindow* toplevelWindow = aWindow->GetTopLevelWindow(false);
  LayoutDeviceIntRect firstSelectedCharRect;
  ResolveIMECaretPos(toplevelWindow, firstSelectedCharRectRelativeToWindow,
                     aWindow, firstSelectedCharRect);

  // Set native caret size/position to our caret. Some IMEs honor it. E.g.,
  // "Intelligent ABC" (Simplified Chinese) and "MS PinYin 3.0" (Simplified
  // Chinese) on XP.  But if a11y module is handling native caret, we shouldn't
  // touch it.
  if (!IMEHandler::IsA11yHandlingNativeCaret()) {
    LayoutDeviceIntRect caretRect(firstSelectedCharRect),
        caretRectRelativeToWindow;
    if (GetCaretRect(aWindow, caretRectRelativeToWindow)) {
      ResolveIMECaretPos(toplevelWindow, caretRectRelativeToWindow, aWindow,
                         caretRect);
    } else {
      NS_WARNING("failed to get caret rect");
      caretRect.SetWidth(1);
    }
    IMEHandler::CreateNativeCaret(aWindow, caretRect);
  }

  if (ShouldDrawCompositionStringOurselves()) {
    MOZ_LOG(gIMELog, LogLevel::Info,
            ("IMMHandler::SetIMERelatedWindowsPos, Set candidate window"));

    // Get a rect of first character in current target in composition string.
    LayoutDeviceIntRect firstTargetCharRect, lastTargetCharRect;
    if (mIsComposing && !mCompositionString.IsEmpty()) {
      // If there are no targetted selection, we should use it's first character
      // rect instead.
      uint32_t offset, length;
      if (!GetTargetClauseRange(&offset, &length)) {
        MOZ_LOG(gIMELog, LogLevel::Error,
                ("  IMMHandler::SetIMERelatedWindowsPos, FAILED, due to "
                 "GetTargetClauseRange() failure"));
        return false;
      }
      ret =
          GetCharacterRectOfSelectedTextAt(aWindow, offset - mCompositionStart,
                                           firstTargetCharRect, &writingMode);
      NS_ENSURE_TRUE(ret, false);
      if (length) {
        ret = GetCharacterRectOfSelectedTextAt(
            aWindow, offset + length - 1 - mCompositionStart,
            lastTargetCharRect);
        NS_ENSURE_TRUE(ret, false);
      } else {
        lastTargetCharRect = firstTargetCharRect;
      }
    } else {
      // If there are no composition string, we should use a first character
      // rect.
      ret = GetCharacterRectOfSelectedTextAt(aWindow, 0, firstTargetCharRect,
                                             &writingMode);
      NS_ENSURE_TRUE(ret, false);
      lastTargetCharRect = firstTargetCharRect;
    }
    ResolveIMECaretPos(toplevelWindow, firstTargetCharRect, aWindow,
                       firstTargetCharRect);
    ResolveIMECaretPos(toplevelWindow, lastTargetCharRect, aWindow,
                       lastTargetCharRect);
    LayoutDeviceIntRect targetClauseRect;
    targetClauseRect.UnionRect(firstTargetCharRect, lastTargetCharRect);

    // Move the candidate window to proper position from the target clause as
    // far as possible.
    CANDIDATEFORM candForm;
    candForm.dwIndex = 0;
    if (!writingMode.IsVertical() || IsVerticalWritingSupported()) {
      candForm.dwStyle = CFS_EXCLUDE;
      // Candidate window shouldn't overlap the target clause in any writing
      // mode.
      candForm.rcArea.left = targetClauseRect.X();
      candForm.rcArea.right = targetClauseRect.XMost();
      candForm.rcArea.top = targetClauseRect.Y();
      candForm.rcArea.bottom = targetClauseRect.YMost();
      if (!writingMode.IsVertical()) {
        // In horizontal layout, current point of interest should be top-left
        // of the first character.
        candForm.ptCurrentPos.x = firstTargetCharRect.X();
        candForm.ptCurrentPos.y = firstTargetCharRect.Y();
      } else if (writingMode.IsVerticalRL()) {
        // In vertical layout (RL), candidate window should be positioned right
        // side of target clause.  However, we don't set vertical writing font
        // to the IME.  Therefore, the candidate window may be positioned
        // bottom-left of target clause rect with these information.
        candForm.ptCurrentPos.x = targetClauseRect.X();
        candForm.ptCurrentPos.y = targetClauseRect.Y();
      } else {
        MOZ_ASSERT(writingMode.IsVerticalLR(), "Did we miss some causes?");
        // In vertical layout (LR), candidate window should be poisitioned left
        // side of target clause.  Although, we don't set vertical writing font
        // to the IME, the candidate window may be positioned bottom-right of
        // the target clause rect with these information.
        candForm.ptCurrentPos.x = targetClauseRect.XMost();
        candForm.ptCurrentPos.y = targetClauseRect.Y();
      }
    } else {
      // If vertical writing is not supported by IME, let's set candidate
      // window position to the bottom-left of the target clause because
      // the position must be the safest position to prevent the candidate
      // window to overlap with the target clause.
      candForm.dwStyle = CFS_CANDIDATEPOS;
      candForm.ptCurrentPos.x = targetClauseRect.X();
      candForm.ptCurrentPos.y = targetClauseRect.YMost();
    }
    MOZ_LOG(gIMELog, LogLevel::Info,
            ("  IMMHandler::SetIMERelatedWindowsPos, Calling "
             "ImmSetCandidateWindow()... ptCurrentPos={ x=%ld, y=%ld }, "
             "rcArea={ left=%ld, top=%ld, right=%ld, bottom=%ld }, "
             "writingMode=%s",
             candForm.ptCurrentPos.x, candForm.ptCurrentPos.y,
             candForm.rcArea.left, candForm.rcArea.top, candForm.rcArea.right,
             candForm.rcArea.bottom, ToString(writingMode).c_str()));
    ::ImmSetCandidateWindow(aContext.get(), &candForm);
  } else {
    MOZ_LOG(gIMELog, LogLevel::Info,
            ("IMMHandler::SetIMERelatedWindowsPos, Set composition window"));

    // Move the composition window to caret position (if selected some
    // characters, we should use first character rect of them).
    // And in this mode, IME adjusts the candidate window position
    // automatically. So, we don't need to set it.
    COMPOSITIONFORM compForm;
    compForm.dwStyle = CFS_POINT;
    compForm.ptCurrentPos.x = !writingMode.IsVerticalLR()
                                  ? firstSelectedCharRect.X()
                                  : firstSelectedCharRect.XMost();
    compForm.ptCurrentPos.y = firstSelectedCharRect.Y();
    ::ImmSetCompositionWindow(aContext.get(), &compForm);
  }

  return true;
}

void IMMHandler::ResolveIMECaretPos(nsIWidget* aReferenceWidget,
                                    LayoutDeviceIntRect& aCursorRect,
                                    nsIWidget* aNewOriginWidget,
                                    LayoutDeviceIntRect& aOutRect) {
  aOutRect = aCursorRect;

  if (aReferenceWidget == aNewOriginWidget) return;

  if (aReferenceWidget)
    aOutRect.MoveBy(aReferenceWidget->WidgetToScreenOffset());

  if (aNewOriginWidget)
    aOutRect.MoveBy(-aNewOriginWidget->WidgetToScreenOffset());
}

static void SetHorizontalFontToLogFont(const nsAString& aFontFace,
                                       LOGFONTW& aLogFont) {
  aLogFont.lfEscapement = aLogFont.lfOrientation = 0;
  if (NS_WARN_IF(aFontFace.Length() > LF_FACESIZE - 1)) {
    memcpy(aLogFont.lfFaceName, L"System", sizeof(L"System"));
    return;
  }
  memcpy(aLogFont.lfFaceName, aFontFace.BeginReading(),
         aFontFace.Length() * sizeof(wchar_t));
  aLogFont.lfFaceName[aFontFace.Length()] = 0;
}

static void SetVerticalFontToLogFont(const nsAString& aFontFace,
                                     LOGFONTW& aLogFont) {
  aLogFont.lfEscapement = aLogFont.lfOrientation = 2700;
  if (NS_WARN_IF(aFontFace.Length() > LF_FACESIZE - 2)) {
    memcpy(aLogFont.lfFaceName, L"@System", sizeof(L"@System"));
    return;
  }
  aLogFont.lfFaceName[0] = '@';
  memcpy(&aLogFont.lfFaceName[1], aFontFace.BeginReading(),
         aFontFace.Length() * sizeof(wchar_t));
  aLogFont.lfFaceName[aFontFace.Length() + 1] = 0;
}

void IMMHandler::AdjustCompositionFont(nsWindow* aWindow,
                                       const IMEContext& aContext,
                                       const WritingMode& aWritingMode,
                                       bool aForceUpdate) {
  // An instance of IMMHandler is destroyed when active IME is changed.
  // Therefore, we need to store the information which are set to the IM
  // context to static variables since IM context is never recreated.
  static bool sCompositionFontsInitialized = false;
  static nsString sCompositionFont;
  static bool sCompositionFontPrefDone = false;
  if (!sCompositionFontPrefDone) {
    sCompositionFontPrefDone = true;
    Preferences::GetString("intl.imm.composition_font", sCompositionFont);
  }

  // If composition font is customized by pref, we need to modify the
  // composition font of the IME context at first time even if the writing mode
  // is horizontal.
  bool setCompositionFontForcibly =
      aForceUpdate ||
      (!sCompositionFontsInitialized && !sCompositionFont.IsEmpty());

  static nsString sCurrentIMEName;
  if (!setCompositionFontForcibly &&
      sWritingModeOfCompositionFont == aWritingMode &&
      sCurrentIMEName == sIMEName) {
    // Nothing to do if writing mode isn't being changed.
    return;
  }

  // Decide composition fonts for both horizontal writing mode and vertical
  // writing mode.  If the font isn't specified by the pref, use default
  // font which is already set to the IM context.  And also in vertical writing
  // mode, insert '@' to the start of the font.
  if (!sCompositionFontsInitialized) {
    sCompositionFontsInitialized = true;
    // sCompositionFontH must not start with '@' and its length is less than
    // LF_FACESIZE since it needs to end with null terminating character.
    if (sCompositionFont.IsEmpty() ||
        sCompositionFont.Length() > LF_FACESIZE - 1 ||
        sCompositionFont[0] == '@') {
      LOGFONTW defaultLogFont;
      if (NS_WARN_IF(
              !::ImmGetCompositionFont(aContext.get(), &defaultLogFont))) {
        MOZ_LOG(
            gIMELog, LogLevel::Error,
            ("  IMMHandler::AdjustCompositionFont, ::ImmGetCompositionFont() "
             "failed"));
        sCompositionFont.AssignLiteral("System");
      } else {
        // The font face is typically, "System".
        sCompositionFont.Assign(defaultLogFont.lfFaceName);
      }
    }

    MOZ_LOG(gIMELog, LogLevel::Info,
            ("  IMMHandler::AdjustCompositionFont, sCompositionFont=\"%s\" is "
             "initialized",
             NS_ConvertUTF16toUTF8(sCompositionFont).get()));
  }

  static nsString sCompositionFontForJapanist2003;
  if (IsJapanist2003Active() && sCompositionFontForJapanist2003.IsEmpty()) {
    const char* kCompositionFontForJapanist2003 =
        "intl.imm.composition_font.japanist_2003";
    Preferences::GetString(kCompositionFontForJapanist2003,
                           sCompositionFontForJapanist2003);
    // If the font name is not specified properly, let's use
    // "MS PGothic" instead.
    if (sCompositionFontForJapanist2003.IsEmpty() ||
        sCompositionFontForJapanist2003.Length() > LF_FACESIZE - 2 ||
        sCompositionFontForJapanist2003[0] == '@') {
      sCompositionFontForJapanist2003.AssignLiteral("MS PGothic");
    }
  }

  sWritingModeOfCompositionFont = aWritingMode;
  sCurrentIMEName = sIMEName;

  LOGFONTW logFont;
  memset(&logFont, 0, sizeof(logFont));
  if (!::ImmGetCompositionFont(aContext.get(), &logFont)) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("  IMMHandler::AdjustCompositionFont, ::ImmGetCompositionFont() "
             "failed"));
    logFont.lfFaceName[0] = 0;
  }
  // Need to reset some information which should be recomputed with new font.
  logFont.lfWidth = 0;
  logFont.lfWeight = FW_DONTCARE;
  logFont.lfOutPrecision = OUT_DEFAULT_PRECIS;
  logFont.lfClipPrecision = CLIP_DEFAULT_PRECIS;
  logFont.lfPitchAndFamily = DEFAULT_PITCH;

  if (aWritingMode.IsVertical() && IsVerticalWritingSupported()) {
    SetVerticalFontToLogFont(IsJapanist2003Active()
                                 ? sCompositionFontForJapanist2003
                                 : sCompositionFont,
                             logFont);
  } else {
    SetHorizontalFontToLogFont(IsJapanist2003Active()
                                   ? sCompositionFontForJapanist2003
                                   : sCompositionFont,
                               logFont);
  }
  MOZ_LOG(gIMELog, LogLevel::Warning,
          ("  IMMHandler::AdjustCompositionFont, calling "
           "::ImmSetCompositionFont(\"%s\")",
           NS_ConvertUTF16toUTF8(nsDependentString(logFont.lfFaceName)).get()));
  ::ImmSetCompositionFontW(aContext.get(), &logFont);
}

// static
nsresult IMMHandler::OnMouseButtonEvent(
    nsWindow* aWindow, const IMENotification& aIMENotification) {
  // We don't need to create the instance of the handler here.
  if (!gIMMHandler) {
    return NS_OK;
  }

  if (!sWM_MSIME_MOUSE || !IsComposingOnOurEditor() ||
      !ShouldDrawCompositionStringOurselves()) {
    return NS_OK;
  }

  // We need to handle only mousedown event.
  if (aIMENotification.mMouseButtonEventData.mEventMessage != eMouseDown) {
    return NS_OK;
  }

  // If the character under the cursor is not in the composition string,
  // we don't need to notify IME of it.
  uint32_t compositionStart = gIMMHandler->mCompositionStart;
  uint32_t compositionEnd =
      compositionStart + gIMMHandler->mCompositionString.Length();
  if (aIMENotification.mMouseButtonEventData.mOffset < compositionStart ||
      aIMENotification.mMouseButtonEventData.mOffset >= compositionEnd) {
    return NS_OK;
  }

  BYTE button;
  switch (aIMENotification.mMouseButtonEventData.mButton) {
    case MouseButton::ePrimary:
      button = IMEMOUSE_LDOWN;
      break;
    case MouseButton::eMiddle:
      button = IMEMOUSE_MDOWN;
      break;
    case MouseButton::eSecondary:
      button = IMEMOUSE_RDOWN;
      break;
    default:
      return NS_OK;
  }

  // calcurate positioning and offset
  // char :            JCH1|JCH2|JCH3
  // offset:           0011 1122 2233
  // positioning:      2301 2301 2301
  LayoutDeviceIntPoint cursorPos =
      aIMENotification.mMouseButtonEventData.mCursorPos;
  LayoutDeviceIntRect charRect =
      aIMENotification.mMouseButtonEventData.mCharRect;
  int32_t cursorXInChar = cursorPos.x - charRect.X();
  // The event might hit to zero-width character, see bug 694913.
  // The reason might be:
  // * There are some zero-width characters are actually.
  // * font-size is specified zero.
  // But nobody reproduced this bug actually...
  // We should assume that user clicked on right most of the zero-width
  // character in such case.
  int positioning = 1;
  if (charRect.Width() > 0) {
    positioning = cursorXInChar * 4 / charRect.Width();
    positioning = (positioning + 2) % 4;
  }

  int offset =
      aIMENotification.mMouseButtonEventData.mOffset - compositionStart;
  if (positioning < 2) {
    offset++;
  }

  MOZ_LOG(gIMELog, LogLevel::Info,
          ("IMMHandler::OnMouseButtonEvent, x,y=%d,%d, offset=%d, "
           "positioning=%d",
           cursorPos.x.value, cursorPos.y.value, offset, positioning));

  // send MS_MSIME_MOUSE message to default IME window.
  HWND imeWnd = ::ImmGetDefaultIMEWnd(aWindow->GetWindowHandle());
  IMEContext context(aWindow);
  if (::SendMessageW(imeWnd, sWM_MSIME_MOUSE,
                     MAKELONG(MAKEWORD(button, positioning), offset),
                     (LPARAM)context.get()) == 1) {
    return NS_SUCCESS_EVENT_CONSUMED;
  }
  return NS_OK;
}

// static
bool IMMHandler::OnKeyDownEvent(nsWindow* aWindow, WPARAM wParam, LPARAM lParam,
                                MSGResult& aResult) {
  MOZ_LOG(
      gIMELog, LogLevel::Info,
      ("IMMHandler::OnKeyDownEvent, hWnd=%p, wParam=%08zx, lParam=%08" PRIxLPTR,
       aWindow->GetWindowHandle(), wParam, lParam));
  aResult.mConsumed = false;
  switch (wParam) {
    case VK_TAB:
    case VK_PRIOR:
    case VK_NEXT:
    case VK_END:
    case VK_HOME:
    case VK_LEFT:
    case VK_UP:
    case VK_RIGHT:
    case VK_DOWN:
    case VK_RETURN:
      // If IME didn't process the key message (the virtual key code wasn't
      // converted to VK_PROCESSKEY), and the virtual key code event causes
      // moving caret or editing text with keeping composing state, we should
      // cancel the composition here because we cannot support moving
      // composition string with DOM events (IE also cancels the composition
      // in same cases).  Then, this event will be dispatched.
      if (IsComposingOnOurEditor()) {
        // NOTE: We don't need to cancel the composition on another window.
        CancelComposition(aWindow, false);
      }
      return false;
    default:
      return false;
  }
}

Maybe<ContentSelection> IMMHandler::QueryContentSelection(nsWindow* aWindow) {
  WidgetQueryContentEvent querySelectedTextEvent(true, eQuerySelectedText,
                                                 aWindow);
  LayoutDeviceIntPoint point(0, 0);
  aWindow->InitEvent(querySelectedTextEvent, &point);
  DispatchEvent(aWindow, querySelectedTextEvent);
  if (NS_WARN_IF(querySelectedTextEvent.Failed())) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("  IMMHandler::Selection::Init, FAILED, due to eQuerySelectedText "
             "failure"));
    return Nothing();
  }
  // If the window is destroyed during querying selected text, we shouldn't
  // do anymore.
  if (aWindow->Destroyed()) {
    MOZ_LOG(
        gIMELog, LogLevel::Error,
        ("  IMMHandler::Selection::Init, FAILED, due to the widget destroyed"));
    return Nothing();
  }

  ContentSelection contentSelection(querySelectedTextEvent);

  MOZ_LOG(gIMELog, LogLevel::Info,
          ("IMMHandler::Selection::Init, querySelectedTextEvent={ mReply=%s }",
           ToString(querySelectedTextEvent.mReply).c_str()));

  if (contentSelection.HasRange() &&
      !contentSelection.OffsetAndDataRef().IsValid()) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("  IMMHandler::Selection::Init, FAILED, due to invalid range"));
    return Nothing();
  }
  return Some(contentSelection);
}

}  // namespace widget
}  // namespace mozilla
