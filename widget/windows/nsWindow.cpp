/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sts=2 sw=2 et cin: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * nsWindow - Native window management and event handling.
 *
 * nsWindow is organized into a set of major blocks and
 * block subsections. The layout is as follows:
 *
 *  Includes
 *  Variables
 *  nsIWidget impl.
 *     nsIWidget methods and utilities
 *  nsSwitchToUIThread impl.
 *     nsSwitchToUIThread methods and utilities
 *  Moz events
 *     Event initialization
 *     Event dispatching
 *  Native events
 *     Wndproc(s)
 *     Event processing
 *     OnEvent event handlers
 *  IME management and accessibility
 *  Transparency
 *  Popup hook handling
 *  Misc. utilities
 *  Child window impl.
 *
 * Search for "BLOCK:" to find major blocks.
 * Search for "SECTION:" to find specific sections.
 *
 * Blocks should be split out into separate files if they
 * become unmanageable.
 *
 * Notable related sources:
 *
 *  nsWindowDefs.h     - Definitions, macros, structs, enums
 *                       and general setup.
 *  nsWindowDbg.h/.cpp - Debug related code and directives.
 *  nsWindowGfx.h/.cpp - Graphics and painting.
 *
 */

/**************************************************************
 **************************************************************
 **
 ** BLOCK: Includes
 **
 ** Include headers.
 **
 **************************************************************
 **************************************************************/

#include "gfx2DGlue.h"
#include "gfxEnv.h"
#include "gfxPlatform.h"

#include "mozilla/AppShutdown.h"
#include "mozilla/AutoRestore.h"
#include "mozilla/Likely.h"
#include "mozilla/PreXULSkeletonUI.h"
#include "mozilla/Logging.h"
#include "mozilla/MathAlgorithms.h"
#include "mozilla/MiscEvents.h"
#include "mozilla/MouseEvents.h"
#include "mozilla/PresShell.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/StaticPrefs_browser.h"
#include "mozilla/SwipeTracker.h"
#include "mozilla/TouchEvents.h"
#include "mozilla/TimeStamp.h"

#include "mozilla/ipc/MessageChannel.h"
#include <algorithm>
#include <limits>

#include "mozilla/widget/WinEventObserver.h"
#include "mozilla/widget/WinMessages.h"
#include "nsLookAndFeel.h"
#include "nsWindow.h"
#include "nsWindowTaskbarConcealer.h"
#include "nsAppRunner.h"

#include <appmodel.h>
#include <shellapi.h>
#include <windows.h>
#include <wtsapi32.h>
#include <process.h>
#include <commctrl.h>
#include <unknwn.h>
#include <psapi.h>
#include <rpc.h>
#include <propvarutil.h>
#include <propkey.h>

#include "mozilla/Logging.h"
#include "prtime.h"
#include "prenv.h"

#include "nsContentUtils.h"
#include "nsISupportsPrimitives.h"
#include "nsITheme.h"
#include "nsIObserverService.h"
#include "nsIScreenManager.h"
#include "imgIContainer.h"
#include "nsIFile.h"
#include "nsIRollupListener.h"
#include "nsIClipboard.h"
#include "WinMouseScrollHandler.h"
#include "nsFontMetrics.h"
#include "nsIFontEnumerator.h"
#include "nsFont.h"
#include "nsRect.h"
#include "nsThreadUtils.h"
#include "nsNativeCharsetUtils.h"
#include "nsGkAtoms.h"
#include "nsCRT.h"
#include "nsAppDirectoryServiceDefs.h"
#include "nsWidgetsCID.h"
#include "nsTHashtable.h"
#include "nsHashKeys.h"
#include "nsString.h"
#include "mozilla/Components.h"
#include "nsNativeThemeWin.h"
#include "nsXULPopupManager.h"
#include "nsWindowsDllInterceptor.h"
#include "nsLayoutUtils.h"
#include "nsView.h"
#include "nsWindowGfx.h"
#include "gfxWindowsPlatform.h"
#include "gfxDWriteFonts.h"
#include "nsPrintfCString.h"
#include "mozilla/Preferences.h"
#include "SystemTimeConverter.h"
#include "WinTaskbar.h"
#include "WidgetUtils.h"
#include "WinWindowOcclusionTracker.h"
#include "nsIWidgetListener.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/MouseEventBinding.h"
#include "mozilla/dom/Touch.h"
#include "mozilla/gfx/2D.h"
#include "mozilla/gfx/GPUProcessManager.h"
#include "mozilla/intl/LocaleService.h"
#include "mozilla/layers/WebRenderLayerManager.h"
#include "mozilla/WindowsVersion.h"
#include "mozilla/TextEvents.h"  // For WidgetKeyboardEvent
#include "mozilla/TextEventDispatcherListener.h"
#include "mozilla/widget/nsAutoRollup.h"
#include "mozilla/widget/PlatformWidgetTypes.h"
#include "mozilla/widget/Screen.h"
#include "nsStyleConsts.h"
#include "nsBidiKeyboard.h"
#include "nsStyleConsts.h"
#include "gfxConfig.h"
#include "InProcessWinCompositorWidget.h"
#include "InputDeviceUtils.h"
#include "ScreenHelperWin.h"
#include "mozilla/StaticPrefs_apz.h"
#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/StaticPrefs_gfx.h"
#include "mozilla/StaticPrefs_layout.h"
#include "mozilla/StaticPrefs_ui.h"
#include "mozilla/StaticPrefs_widget.h"
#include "nsNativeAppSupportWin.h"

#include "nsIGfxInfo.h"
#include "nsUXThemeConstants.h"
#include "KeyboardLayout.h"
#include "nsNativeDragTarget.h"
#include <mmsystem.h>  // needed for WIN32_LEAN_AND_MEAN
#include <zmouse.h>
#include <richedit.h>

#ifdef ACCESSIBILITY
#  ifdef DEBUG
#    include "mozilla/a11y/Logging.h"
#  endif
#  include "mozilla/a11y/Compatibility.h"
#  include "oleidl.h"
#  include <uiautomation.h>
#  include <winuser.h>
#  include "nsAccessibilityService.h"
#  include "mozilla/a11y/DocAccessible.h"
#  include "mozilla/a11y/LazyInstantiator.h"
#  include "mozilla/a11y/Platform.h"
#  if !defined(WINABLEAPI)
#    include <winable.h>
#  endif  // !defined(WINABLEAPI)
#endif

#include "WindowsUIUtils.h"

#include "nsWindowDefs.h"

#include "nsCrashOnException.h"

#include "nsIContent.h"

#include "mozilla/BackgroundHangMonitor.h"
#include "WinIMEHandler.h"

#include "npapi.h"

#include <d3d11.h>

// ERROR from wingdi.h (below) gets undefined by some code.
// #define ERROR               0
// #define RGN_ERROR ERROR
#define ERROR 0

#if !defined(SM_CONVERTIBLESLATEMODE)
#  define SM_CONVERTIBLESLATEMODE 0x2003
#endif

#include "mozilla/gfx/DeviceManagerDx.h"
#include "mozilla/layers/APZInputBridge.h"
#include "mozilla/layers/InputAPZContext.h"
#include "mozilla/layers/KnowsCompositor.h"
#include "InputData.h"

#include "mozilla/TaskController.h"
#include "mozilla/webrender/WebRenderAPI.h"
#include "mozilla/layers/IAPZCTreeManager.h"

#include "DirectManipulationOwner.h"

using namespace mozilla;
using namespace mozilla::dom;
using namespace mozilla::gfx;
using namespace mozilla::layers;
using namespace mozilla::widget;
using namespace mozilla::plugins;

/**************************************************************
 **************************************************************
 **
 ** BLOCK: Variables
 **
 ** nsWindow Class static initializations and global variables.
 **
 **************************************************************
 **************************************************************/

/**************************************************************
 *
 * SECTION: nsWindow statics
 *
 **************************************************************/
static const wchar_t kUser32LibName[] = L"user32.dll";

uint32_t nsWindow::sInstanceCount = 0;
bool nsWindow::sIsOleInitialized = false;
MOZ_RUNINIT nsIWidget::Cursor nsWindow::sCurrentCursor = {};
nsWindow* nsWindow::sCurrentWindow = nullptr;
bool nsWindow::sJustGotDeactivate = false;
bool nsWindow::sJustGotActivate = false;
bool nsWindow::sIsInMouseCapture = false;

// Urgent-message reentrancy depth for the static `WindowProc` callback.
//
// Three unfortunate facts collide:
//
// 𝛼) Some messages must be processed promptly. If not, Windows will leave the
//    receiving window in an intermediate, and potentially unusable, state until
//    the WindowProc invocation that is handling it returns.
//
// 𝛽) Some messages have indefinitely long processing time. These are mostly
//    messages which may cause us to enter a nested modal loop (via
//    `SpinEventLoopUntil` or similar).
//
// 𝛾) Sometimes, messages skip the queue entirely. Our `WindowProc` may be
//    reentrantly reinvoked from the kernel while we're blocking _on_ the
//    kernel, even briefly, during processing of other messages. (Relevant
//    search term: `KeUserModeCallback`.)
//
// The nightmare scenario, then, is that during processing of an 𝛼-message, we
// briefly become blocked (e.g., by calling `::SendMessageW()`), and the kernel
// takes that opportunity to use 𝛾 to hand us a 𝛽-message. (Concretely, see
// bug 1842170.)
//
// There is little we can do to prevent the first half of this scenario. 𝛼) and
// 𝛾) are effectively immutable facts of Windows, and we sometimes legitimately
// need to make blocking calls to process 𝛼-messages. (We may not even be aware
// that we're making such calls, if they're undocumented implementation details
// of another API.)
//
// In an ideal world, WindowProc would always return promptly (or at least in
// bounded time), and 𝛽-messages would not _per se_ exist; long-running modal
// states would instead be implemented in async fashion. In practice, that's far
// easier said than done -- replacing existing uses of `SpinEventLoopUntil` _et
// al._ with asynchronous mechanisms is a collection of mostly-unrelated cross-
// cutting architectural tasks, each of potentially unbounded scope. For now,
// and for the foreseeable future, we're stuck with them.
//
// We therefore simply punt. More specifically: if a known 𝛽-message jumps the
// queue to come in while we're in the middle of processing a known 𝛼-message,
// we:
//  * properly queue the message for processing later;
//  * respond to the 𝛽-message as though we actually had processed it; and
//  * just hope that it can wait until we get around to it.
//
// The word "known" requires a bit of justification. There is no canonical set
// of 𝛼-messages, nor is the set of 𝛽-messages fixed (or even demarcable). We
// can't safely assume that all messages are 𝛼-messages, as that could cause
// 𝛽-messages to be arbitrarily and surprisingly delayed whenever any nested
// event loop is active. We also can't assume all messages are 𝛽-messages,
// since one 𝛼-message jumping the queue while processing another 𝛼-message is
// part of normal and required operation for windowed Windows applications.
//
// So we simply add messages to those sets as we identify them. (Or, preferably,
// rework the 𝛽-message's handling to make it no longer 𝛽. But see above.)
//
// ---
//
// The actual value of `sDepth` is the number of active invocations of
// `WindowProc` that are processing known 𝛼-messages.
size_t nsWindow::WndProcUrgentInvocation::sDepth = 0;

// Hook Data Members for Dropdowns. sProcessHook Tells the
// hook methods whether they should be processing the hook
// messages.
HHOOK nsWindow::sMsgFilterHook = nullptr;
HHOOK nsWindow::sCallProcHook = nullptr;
HHOOK nsWindow::sCallMouseHook = nullptr;
bool nsWindow::sProcessHook = false;
UINT nsWindow::sRollupMsgId = 0;
HWND nsWindow::sRollupMsgWnd = nullptr;
UINT nsWindow::sHookTimerId = 0;

// Used to prevent dispatching mouse events that do not originate from user
// input.
POINT nsWindow::sLastMouseMovePoint = {0};

bool nsWindow::sIsRestoringSession = false;

bool nsWindow::sTouchInjectInitialized = false;
InjectTouchInputPtr nsWindow::sInjectTouchFuncPtr;

static SystemTimeConverter<DWORD>& TimeConverter() {
  static SystemTimeConverter<DWORD> timeConverterSingleton;
  return timeConverterSingleton;
}

static const wchar_t* GetMainWindowClass();
static const wchar_t* ChooseWindowClass(mozilla::widget::WindowType);
// This method registers the given window class, and returns the class name.
static void RegisterWindowClass(const wchar_t* aClassName, UINT aExtraStyle,
                                LPWSTR aIconID);

// Global event hook for window cloaking. Never deregistered.
//  - `Nothing` if not yet set.
//  - `Some(nullptr)` if no attempt should be made to set it.
static mozilla::Maybe<HWINEVENTHOOK> sWinCloakEventHook = Nothing();
static mozilla::LazyLogModule sCloakingLog("DWMCloaking");

namespace mozilla {

class CurrentWindowsTimeGetter {
 public:
  explicit CurrentWindowsTimeGetter(HWND aWnd) : mWnd(aWnd) {}

  DWORD GetCurrentTime() const { return ::GetTickCount(); }

  void GetTimeAsyncForPossibleBackwardsSkew(const TimeStamp& aNow) {
    DWORD currentTime = GetCurrentTime();
    if (sBackwardsSkewStamp && currentTime == sLastPostTime) {
      // There's already one inflight with this timestamp. Don't
      // send a duplicate.
      return;
    }
    sBackwardsSkewStamp = Some(aNow);
    sLastPostTime = currentTime;
    static_assert(sizeof(WPARAM) >= sizeof(DWORD),
                  "Can't fit a DWORD in a WPARAM");
    ::PostMessage(mWnd, MOZ_WM_SKEWFIX, sLastPostTime, 0);
  }

  static bool GetAndClearBackwardsSkewStamp(DWORD aPostTime,
                                            TimeStamp* aOutSkewStamp) {
    if (aPostTime != sLastPostTime) {
      // The SKEWFIX message is stale; we've sent a new one since then.
      // Ignore this one.
      return false;
    }
    MOZ_ASSERT(sBackwardsSkewStamp);
    *aOutSkewStamp = sBackwardsSkewStamp.value();
    sBackwardsSkewStamp = Nothing();
    return true;
  }

 private:
  static Maybe<TimeStamp> sBackwardsSkewStamp;
  static DWORD sLastPostTime;
  HWND mWnd;
};

Maybe<TimeStamp> CurrentWindowsTimeGetter::sBackwardsSkewStamp;
DWORD CurrentWindowsTimeGetter::sLastPostTime = 0;

}  // namespace mozilla

/**************************************************************
 *
 * SECTION: globals variables
 *
 **************************************************************/

static const char* sScreenManagerContractID =
    "@mozilla.org/gfx/screenmanager;1";

extern mozilla::LazyLogModule gWindowsLog;

static NS_DEFINE_CID(kCClipboardCID, NS_CLIPBOARD_CID);

// General purpose user32.dll hook object
MOZ_RUNINIT static WindowsDllInterceptor sUser32Intercept;

// When the client area is extended out into the default window frame area,
// this is the minimum amount of space along the edge of resizable windows
// we will always display a resize cursor in, regardless of the underlying
// content.
static const int32_t kResizableBorderMinSize = 3;

// Getting this object from the window server can be expensive. Keep it
// around, also get it off the main thread. (See bug 1640852)
StaticRefPtr<IVirtualDesktopManager> gVirtualDesktopManager;
static bool gInitializedVirtualDesktopManager = false;

// We should never really try to accelerate windows bigger than this. In some
// cases this might lead to no D3D9 acceleration where we could have had it
// but D3D9 does not reliably report when it supports bigger windows. 8192
// is as safe as we can get, we know at least D3D10 hardware always supports
// this, other hardware we expect to report correctly in D3D9.
#define MAX_ACCELERATED_DIMENSION 8192

// On window open (as well as after), Windows has an unfortunate habit of
// sending rather a lot of WM_NCHITTEST messages. Because we have to do point
// to DOM target conversions for these, we cache responses for a given
// coordinate this many milliseconds:
#define HITTEST_CACHE_LIFETIME_MS 50

#if defined(ACCESSIBILITY)

namespace mozilla {

/**
 * Windows touchscreen code works by setting a global WH_GETMESSAGE hook and
 * injecting tiptsf.dll. The touchscreen process then posts registered messages
 * to our main thread. The tiptsf hook picks up those registered messages and
 * uses them as commands, some of which call into UIA, which then calls into
 * MSAA, which then sends WM_GETOBJECT to us.
 *
 * We can get ahead of this by installing our own thread-local WH_GETMESSAGE
 * hook. Since thread-local hooks are called ahead of global hooks, we will
 * see these registered messages before tiptsf does. At this point we can then
 * raise a flag that blocks a11y before invoking CallNextHookEx which will then
 * invoke the global tiptsf hook. Then when we see WM_GETOBJECT, we check the
 * flag by calling TIPMessageHandler::IsA11yBlocked().
 *
 * For Windows 8, we also hook tiptsf!ProcessCaretEvents, which is an a11y hook
 * function that also calls into UIA.
 */
class TIPMessageHandler {
 public:
  ~TIPMessageHandler() {
    if (mHook) {
      ::UnhookWindowsHookEx(mHook);
    }
  }

  static void Initialize() {
    if (sInstance) {
      return;
    }

    sInstance = new TIPMessageHandler();
    ClearOnShutdown(&sInstance);
  }

  static bool IsA11yBlocked() {
    if (!sInstance) {
      return false;
    }

    return sInstance->mA11yBlockCount > 0;
  }

 private:
  TIPMessageHandler() : mHook(nullptr), mA11yBlockCount(0) {
    MOZ_ASSERT(NS_IsMainThread());

    // Registered messages used by tiptsf
    mMessages[0] = ::RegisterWindowMessage(L"ImmersiveFocusNotification");
    mMessages[1] = ::RegisterWindowMessage(L"TipCloseMenus");
    mMessages[2] = ::RegisterWindowMessage(L"TabletInputPanelOpening");
    mMessages[3] = ::RegisterWindowMessage(L"IHM Pen or Touch Event noticed");
    mMessages[4] = ::RegisterWindowMessage(L"ProgrammabilityCaretVisibility");
    mMessages[5] = ::RegisterWindowMessage(L"CaretTrackingUpdateIPHidden");
    mMessages[6] = ::RegisterWindowMessage(L"CaretTrackingUpdateIPInfo");

    mHook = ::SetWindowsHookEx(WH_GETMESSAGE, &TIPHook, nullptr,
                               ::GetCurrentThreadId());
    MOZ_ASSERT(mHook);

    if (!sSendMessageTimeoutWStub) {
      sUser32Intercept.Init("user32.dll");
      DebugOnly<bool> hooked = sSendMessageTimeoutWStub.Set(
          sUser32Intercept, "SendMessageTimeoutW", &SendMessageTimeoutWHook);
      MOZ_ASSERT(hooked);
    }
  }

  class MOZ_RAII A11yInstantiationBlocker {
   public:
    A11yInstantiationBlocker() {
      if (!TIPMessageHandler::sInstance) {
        return;
      }
      ++TIPMessageHandler::sInstance->mA11yBlockCount;
    }  // namespace mozilla

    ~A11yInstantiationBlocker() {
      if (!TIPMessageHandler::sInstance) {
        return;
      }
      MOZ_ASSERT(TIPMessageHandler::sInstance->mA11yBlockCount > 0);
      --TIPMessageHandler::sInstance->mA11yBlockCount;
    }
  };

  friend class A11yInstantiationBlocker;

  static LRESULT CALLBACK TIPHook(int aCode, WPARAM aWParam, LPARAM aLParam) {
    if (aCode < 0 || !sInstance) {
      return ::CallNextHookEx(nullptr, aCode, aWParam, aLParam);
    }

    MSG* msg = reinterpret_cast<MSG*>(aLParam);
    UINT& msgCode = msg->message;

    for (uint32_t i = 0; i < std::size(sInstance->mMessages); ++i) {
      if (msgCode == sInstance->mMessages[i]) {
        A11yInstantiationBlocker block;
        return ::CallNextHookEx(nullptr, aCode, aWParam, aLParam);
      }
    }

    return ::CallNextHookEx(nullptr, aCode, aWParam, aLParam);
  }

  static LRESULT WINAPI SendMessageTimeoutWHook(HWND aHwnd, UINT aMsgCode,
                                                WPARAM aWParam, LPARAM aLParam,
                                                UINT aFlags, UINT aTimeout,
                                                PDWORD_PTR aMsgResult) {
    // We don't want to handle this unless the message is a WM_GETOBJECT that we
    // want to block, and the aHwnd is a nsWindow that belongs to the current
    // (i.e., main) thread.
    if (!aMsgResult || aMsgCode != WM_GETOBJECT ||
        static_cast<LONG>(aLParam) != OBJID_CLIENT || !::NS_IsMainThread() ||
        !WinUtils::GetNSWindowPtr(aHwnd) || !IsA11yBlocked()) {
      return sSendMessageTimeoutWStub(aHwnd, aMsgCode, aWParam, aLParam, aFlags,
                                      aTimeout, aMsgResult);
    }

    // In this case we want to fake the result that would happen if we had
    // decided not to handle WM_GETOBJECT in our WndProc. We hand the message
    // off to DefWindowProc to accomplish this.
    *aMsgResult = static_cast<DWORD_PTR>(
        ::DefWindowProcW(aHwnd, aMsgCode, aWParam, aLParam));

    return static_cast<LRESULT>(TRUE);
  }

  static WindowsDllInterceptor::FuncHookType<decltype(&SendMessageTimeoutW)>
      sSendMessageTimeoutWStub;
  static StaticAutoPtr<TIPMessageHandler> sInstance;

  HHOOK mHook;
  UINT mMessages[7];
  uint32_t mA11yBlockCount;
};

WindowsDllInterceptor::FuncHookType<decltype(&SendMessageTimeoutW)>
    TIPMessageHandler::sSendMessageTimeoutWStub;
StaticAutoPtr<TIPMessageHandler> TIPMessageHandler::sInstance;

}  // namespace mozilla

#endif  // defined(ACCESSIBILITY)

namespace mozilla {

// This task will get the VirtualDesktopManager from the generic thread pool
// since doing this on the main thread on startup causes performance issues.
//
// See bug 1640852.
//
// This should be fine and should not require any locking, as when the main
// thread will access it, if it races with this function it will either find
// it to be null or to have a valid value.
class InitializeVirtualDesktopManagerTask : public Task {
 public:
  InitializeVirtualDesktopManagerTask()
      : Task(Kind::OffMainThreadOnly, kDefaultPriorityValue) {}

#ifdef MOZ_COLLECTING_RUNNABLE_TELEMETRY
  bool GetName(nsACString& aName) override {
    aName.AssignLiteral("InitializeVirtualDesktopManagerTask");
    return true;
  }
#endif

  virtual TaskResult Run() override {
    RefPtr<IVirtualDesktopManager> desktopManager;
    HRESULT hr = ::CoCreateInstance(
        CLSID_VirtualDesktopManager, NULL, CLSCTX_INPROC_SERVER,
        __uuidof(IVirtualDesktopManager), getter_AddRefs(desktopManager));
    if (FAILED(hr)) {
      return TaskResult::Complete;
    }

    gVirtualDesktopManager = desktopManager;
    return TaskResult::Complete;
  }
};

// Ground-truth query: does Windows claim the window is cloaked right now?
static bool IsCloaked(HWND hwnd) {
  DWORD cloakedState;
  HRESULT hr = ::DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloakedState,
                                       sizeof(cloakedState));

  if (FAILED(hr)) {
    MOZ_LOG(sCloakingLog, LogLevel::Warning,
            ("failed (%08lX) to query cloaking state for HWND %p", hr, hwnd));
    return false;
  }

  return cloakedState != 0;
}

}  // namespace mozilla

/**************************************************************
 **************************************************************
 **
 ** BLOCK: nsIWidget impl.
 **
 ** nsIWidget interface implementation, broken down into
 ** sections.
 **
 **************************************************************
 **************************************************************/

/**************************************************************
 *
 * SECTION: nsWindow construction and destruction
 *
 **************************************************************/

nsWindow::nsWindow()
    : nsBaseWidget(BorderStyle::Default),
      mFrameState(std::in_place, this),
      mPIPWindow(false),
      mMicaBackdrop(false),
      mLastPaintEndTime(TimeStamp::Now()),
      mCachedHitTestTime(TimeStamp::Now()),
      mSizeConstraintsScale(GetDefaultScale().scale),
      mDesktopId("DesktopIdMutex") {
  MOZ_ASSERT(mWindowType == WindowType::Child);

  if (!gInitializedVirtualDesktopManager) {
    TaskController::Get()->AddTask(
        MakeAndAddRef<InitializeVirtualDesktopManagerTask>());
    gInitializedVirtualDesktopManager = true;
  }

  // Global initialization
  if (!sInstanceCount) {
    // Global app registration id for Win7 and up. See
    // WinTaskbar.cpp for details.
    // MSIX packages explicitly do not support setting the appid from within
    // the app, as it is set in the package manifest instead.
    if (!WinUtils::HasPackageIdentity()) {
      mozilla::widget::WinTaskbar::RegisterAppUserModelID();
    }
    if (!StaticPrefs::ui_key_layout_load_when_first_needed()) {
      KeyboardLayout::GetInstance()->OnLayoutChange(::GetKeyboardLayout(0));
    }
#if defined(ACCESSIBILITY)
    mozilla::TIPMessageHandler::Initialize();
#endif  // defined(ACCESSIBILITY)
    if (SUCCEEDED(::OleInitialize(nullptr))) {
      sIsOleInitialized = true;
    }
    NS_ASSERTION(sIsOleInitialized, "***** OLE is not initialized!\n");
    MouseScrollHandler::Initialize();
    RedirectedKeyDownMessageManager::Forget();
  }  // !sInstanceCount

  sInstanceCount++;
}

nsWindow::~nsWindow() {
  mInDtor = true;

  // If the widget was released without calling Destroy() then the native window
  // still exists, and we need to destroy it. Destroy() will early-return if it
  // was already called. In any case it is important to call it before
  // destroying mPresentLock (cf. 1156182).
  Destroy();

  // Free app icon resources.  This must happen after `OnDestroy` (see bug
  // 708033).
  if (mIconSmall) ::DestroyIcon(mIconSmall);

  if (mIconBig) ::DestroyIcon(mIconBig);

  sInstanceCount--;

  // Global shutdown
  if (sInstanceCount == 0) {
    sCurrentCursor = {};
    if (sIsOleInitialized) {
      // When we reach here, IMEHandler::Terminate() should've already been
      // called because it causes releasing the last nsWindow instance.
      // However, it **could** occur that we are shutting down without giving
      // IME focus, but we need to release TSF objects before the following
      // ::OleUninitialize() call.  Fortunately, it's fine to call the method
      // twice so that we can always call it here.
      IMEHandler::Terminate();
      ::OleFlushClipboard();
      ::OleUninitialize();
      sIsOleInitialized = false;
    }
  }

  NS_IF_RELEASE(mNativeDragTarget);
}

/**************************************************************
 *
 * SECTION: nsIWidget::Create, nsIWidget::Destroy
 *
 * Creating and destroying windows for this widget.
 *
 **************************************************************/

void nsWindow::SendAnAPZEvent(InputData& aEvent) {
  LRESULT popupHandlingResult;
  if (DealWithPopups(mWnd, MOZ_WM_DMANIP, 0, 0, &popupHandlingResult)) {
    // We need to consume the event after using it to roll up the popup(s).
    return;
  }

  if (mSwipeTracker && aEvent.mInputType == PANGESTURE_INPUT) {
    // Give the swipe tracker a first pass at the event. If a new pan gesture
    // has been started since the beginning of the swipe, the swipe tracker
    // will know to ignore the event.
    nsEventStatus status =
        mSwipeTracker->ProcessEvent(aEvent.AsPanGestureInput());
    if (status == nsEventStatus_eConsumeNoDefault) {
      return;
    }
  }

  APZEventResult result;
  if (mAPZC) {
    result = mAPZC->InputBridge()->ReceiveInputEvent(aEvent);
  }
  if (result.GetStatus() == nsEventStatus_eConsumeNoDefault) {
    return;
  }

  MOZ_ASSERT(aEvent.mInputType == PANGESTURE_INPUT ||
             aEvent.mInputType == PINCHGESTURE_INPUT);

  if (aEvent.mInputType == PANGESTURE_INPUT) {
    PanGestureInput& panInput = aEvent.AsPanGestureInput();
    WidgetWheelEvent event = panInput.ToWidgetEvent(this);
    if (!mAPZC) {
      if (MayStartSwipeForNonAPZ(panInput)) {
        return;
      }
    } else {
      event = MayStartSwipeForAPZ(panInput, result);
    }

    ProcessUntransformedAPZEvent(&event, result);

    return;
  }

  PinchGestureInput& pinchInput = aEvent.AsPinchGestureInput();
  WidgetWheelEvent event = pinchInput.ToWidgetEvent(this);
  ProcessUntransformedAPZEvent(&event, result);
}

void nsWindow::RecreateDirectManipulationIfNeeded() {
  DestroyDirectManipulation();

  if (mWindowType != WindowType::TopLevel && mWindowType != WindowType::Popup) {
    return;
  }

  if (!(StaticPrefs::apz_allow_zooming() ||
        StaticPrefs::apz_windows_use_direct_manipulation()) ||
      StaticPrefs::apz_windows_force_disable_direct_manipulation()) {
    return;
  }

  mDmOwner = MakeUnique<DirectManipulationOwner>(this);

  LayoutDeviceIntRect bounds(mBounds.X(), mBounds.Y(), mBounds.Width(),
                             mBounds.Height());
  mDmOwner->Init(bounds);
}

void nsWindow::ResizeDirectManipulationViewport() {
  if (mDmOwner) {
    LayoutDeviceIntRect bounds(mBounds.X(), mBounds.Y(), mBounds.Width(),
                               mBounds.Height());
    mDmOwner->ResizeViewport(bounds);
  }
}

void nsWindow::DestroyDirectManipulation() {
  if (mDmOwner) {
    mDmOwner->Destroy();
    mDmOwner.reset();
  }
}

namespace mozilla::widget {

// A mask specifying the window-styles associated with window-chrome.
constexpr static const WindowStyles kChromeStylesMask{
    .style = WS_CAPTION | WS_THICKFRAME,
    .ex = WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE |
          WS_EX_STATICEDGE,
};

WindowStyles WindowStyles::FromHWND(HWND aWnd) {
  return {.style = ::GetWindowLongPtrW(aWnd, GWL_STYLE),
          .ex = ::GetWindowLongPtrW(aWnd, GWL_EXSTYLE)};
}

void SetWindowStyles(HWND aWnd, const WindowStyles& aStyles) {
  VERIFY_WINDOW_STYLE(aStyles.style);
  ::SetWindowLongPtrW(aWnd, GWL_STYLE, aStyles.style);
  ::SetWindowLongPtrW(aWnd, GWL_EXSTYLE, aStyles.ex);
}

}  // namespace mozilla::widget

// Create the proper widget
nsresult nsWindow::Create(nsIWidget* aParent, const LayoutDeviceIntRect& aRect,
                          widget::InitData* aInitData) {
  // Historical note: there was once some belief and/or intent that nsWindows
  // could be created on arbitrary threads, and this may still be reflected in
  // some comments.
  MOZ_ASSERT(NS_IsMainThread());

  // Ensure that the hidden window exists, so that broadcast Windows messages
  // (WM_FONTCHANGE et al.) are received and processed.
  WinEventWindow::Ensure();

  widget::InitData defaultInitData;
  if (!aInitData) aInitData = &defaultInitData;

  MOZ_DIAGNOSTIC_ASSERT(aInitData->mWindowType != WindowType::Invisible);
  MOZ_DIAGNOSTIC_ASSERT(aInitData->mWindowType != WindowType::Child);

  mBounds = aRect;

  // Ensure that the toolkit is created.
  nsToolkit::GetToolkit();

  BaseCreate(aParent, aInitData);

  HWND parent =
      aParent ? (HWND)aParent->GetNativeData(NS_NATIVE_WINDOW) : nullptr;

  mIsRTL = aInitData->mRTL;
  mPIPWindow = aInitData->mPIPWindow;
  mOpeningAnimationSuppressed = aInitData->mIsAnimationSuppressed;
  mAlwaysOnTop = aInitData->mAlwaysOnTop;
  mIsAlert = aInitData->mIsAlert;
  mResizable = aInitData->mResizable;

  Styles desiredStyles{
      .style = static_cast<LONG_PTR>(WindowStyle()),
      .ex = static_cast<LONG_PTR>(WindowExStyle()),
  };

  if (mWindowType != WindowType::Popup) {
    // See if the caller wants to explicitly set clip children and clip siblings
    if (aInitData->mClipChildren) {
      desiredStyles.style |= WS_CLIPCHILDREN;
    } else {
      desiredStyles.style &= ~WS_CLIPCHILDREN;
    }
    if (aInitData->mClipSiblings) {
      desiredStyles.style |= WS_CLIPSIBLINGS;
    }
  }

  const wchar_t* className = ChooseWindowClass(mWindowType);

  // Take specific actions when creating the first top-level window
  static bool sFirstTopLevelWindowCreated = false;
  if (aInitData->mWindowType == WindowType::TopLevel && !aParent &&
      !sFirstTopLevelWindowCreated) {
    sFirstTopLevelWindowCreated = true;
    mWnd = ConsumePreXULSkeletonUIHandle();
    if (mWnd) {
      MOZ_ASSERT(desiredStyles.style == kPreXULSkeletonUIWindowStyle,
                 "The skeleton UI window style should match the expected "
                 "style for the first window created");
      MOZ_ASSERT(desiredStyles.ex == kPreXULSkeletonUIWindowStyleEx,
                 "The skeleton UI window extended style should match the "
                 "expected extended style for the first window created");
      MOZ_ASSERT(
          ::GetWindowThreadProcessId(mWnd, nullptr) == ::GetCurrentThreadId(),
          "The skeleton UI window should be created on the same thread as "
          "other windows");
      mIsShowingPreXULSkeletonUI = true;

      // If we successfully consumed the pre-XUL skeleton UI, just update
      // our internal state to match what is currently being displayed.
      mIsVisible = true;
      mIsCloaked = mozilla::IsCloaked(mWnd);
      mFrameState->ConsumePreXULSkeletonState(WasPreXULSkeletonUIMaximized());

      MOZ_ASSERT(BoundsUseDesktopPixels());
      auto scale = GetDesktopToDeviceScale();
      mBounds = mLastPaintBounds = LayoutDeviceIntRect::FromUnknownRect(
          DesktopIntRect::Round(LayoutDeviceRect(GetBounds()) / scale)
              .ToUnknownRect());

      // Skeleton ui is disabled when custom titlebar is off, see bug 1673092.
      SetCustomTitlebar(true);

      // Reset the WNDPROC for this window and its whole class, as we had
      // to use our own WNDPROC when creating the the skeleton UI window.
      ::SetWindowLongPtrW(mWnd, GWLP_WNDPROC,
                          reinterpret_cast<LONG_PTR>(
                              WinUtils::NonClientDpiScalingDefWindowProcW));
      ::SetClassLongPtrW(mWnd, GCLP_WNDPROC,
                         reinterpret_cast<LONG_PTR>(
                             WinUtils::NonClientDpiScalingDefWindowProcW));
    }
  }

  if (!mWnd) {
    mWnd =
        ::CreateWindowExW(desiredStyles.ex, className, L"", desiredStyles.style,
                          aRect.X(), aRect.Y(), aRect.Width(), aRect.Height(),
                          parent, nullptr, nsToolkit::mDllInstance, nullptr);
    if (!mWnd) {
      NS_WARNING("nsWindow CreateWindowEx failed.");
      return NS_ERROR_FAILURE;
    }
  }

  {
    // Some of the chrome mask window styles can be added implicitly by
    // CreateWindowEx, but we really don't want that.
    // To be safe, only deal with those bits for now, instead of just
    // overriding with extendedStyle or style.
    // This can happen with non-native alert windows for example.
    const auto actualStyles = Styles::FromHWND(mWnd);
    auto newStyles = (actualStyles & ~kChromeStylesMask) |
                     (desiredStyles & kChromeStylesMask);
    if (newStyles != actualStyles) {
      SetWindowStyles(mWnd, newStyles);
    }
  }

  if (!sWinCloakEventHook) {
    MOZ_LOG(sCloakingLog, LogLevel::Info, ("Registering cloaking event hook"));

    // C++03 lambda approximation until P2173R1 is available (-std=c++2b)
    struct StdcallLambda {
      static void CALLBACK OnCloakUncloakHook(HWINEVENTHOOK hWinEventHook,
                                              DWORD event, HWND hwnd,
                                              LONG idObject, LONG idChild,
                                              DWORD idEventThread,
                                              DWORD dwmsEventTime) {
        const bool isCloaked = event == EVENT_OBJECT_CLOAKED ? true : false;
        nsWindow::OnCloakEvent(hwnd, isCloaked);
      }
    };

    const HWINEVENTHOOK hook = ::SetWinEventHook(
        EVENT_OBJECT_CLOAKED, EVENT_OBJECT_UNCLOAKED, HMODULE(nullptr),
        &StdcallLambda::OnCloakUncloakHook, ::GetCurrentProcessId(),
        ::GetCurrentThreadId(), WINEVENT_OUTOFCONTEXT);
    sWinCloakEventHook = Some(hook);

    if (!hook) {
      const DWORD err = ::GetLastError();
      MOZ_LOG(sCloakingLog, LogLevel::Error,
              ("Failed to register cloaking event hook! GLE = %lu (0x%lX)", err,
               err));
    }
  }

  {
    // Although permanent Private Browsing mode is indeed Private Browsing,
    // we choose to make it look like regular Firefox in terms of the icon
    // it uses (which also means we shouldn't use the Private Browsing
    // AUMID).
    bool usePrivateAumid =
        Preferences::GetBool("browser.privateWindowSeparation.enabled", true) &&
        (aInitData->mIsPrivate) &&
        !StaticPrefs::browser_privatebrowsing_autostart();
    RefPtr<IPropertyStore> pPropStore;
    if (!FAILED(SHGetPropertyStoreForWindow(mWnd, IID_IPropertyStore,
                                            getter_AddRefs(pPropStore)))) {
      PROPVARIANT pv;
      nsAutoString aumid;
      // Make sure we're using the correct AUMID so that taskbar
      // grouping works properly
      Unused << NS_WARN_IF(!mozilla::widget::WinTaskbar::GenerateAppUserModelID(
          aumid, usePrivateAumid));
      if (!usePrivateAumid && widget::WinUtils::HasPackageIdentity()) {
        // On MSIX we should always have a provided process AUMID
        // that we can explicitly assign to a regular window.
        UINT32 maxLength = MAX_PATH;
        aumid.SetLength(maxLength);
        Unused << NS_WARN_IF(
            GetCurrentApplicationUserModelId(&maxLength, aumid.get()));
      }
      if (!FAILED(InitPropVariantFromString(aumid.get(), &pv))) {
        if (!FAILED(pPropStore->SetValue(PKEY_AppUserModel_ID, pv))) {
          pPropStore->Commit();
        }

        PropVariantClear(&pv);
      }
    }
    HICON icon = ::LoadIconW(
        ::GetModuleHandleW(nullptr),
        MAKEINTRESOURCEW(usePrivateAumid ? IDI_PBMODE : IDI_APPICON));
    SetBigIcon(icon);
    SetSmallIcon(icon);
  }

  // If mDefaultScale is set before mWnd has been set, it will have the scale of
  // the primary monitor, rather than the monitor that the window is actually
  // on. For non-popup windows this gets corrected by the WM_DPICHANGED message
  // which resets mDefaultScale, but for popup windows we don't reset
  // mDefaultScale on that message. In order to ensure that popup windows
  // spawned on a non-primary monitor end up with the correct scale, we reset
  // mDefaultScale here so that it gets recomputed using the correct monitor now
  // that we have a mWnd.
  mDefaultScale = -1.0;

  if (mIsRTL) {
    DWORD dwAttribute = TRUE;
    DwmSetWindowAttribute(mWnd, DWMWA_NONCLIENT_RTL_LAYOUT, &dwAttribute,
                          sizeof dwAttribute);
  }

  // Default to the system color scheme unless getting told otherwise.
  SetColorScheme(Nothing());

  if (mOpeningAnimationSuppressed) {
    SuppressAnimation(true);
  }

  if (mAlwaysOnTop) {
    ::SetWindowPos(mWnd, HWND_TOPMOST, 0, 0, 0, 0,
                   SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
  }

  if (MouseScrollHandler::Device::IsFakeScrollableWindowNeeded()) {
    // Ugly Thinkpad Driver Hack (Bugs 507222 and 594977)
    //
    // We create two zero-sized windows as descendants of the top-level window,
    // like so:
    //
    //   Top-level window (MozillaWindowClass)
    //     FAKETRACKPOINTSCROLLCONTAINER (MozillaWindowClass)
    //       FAKETRACKPOINTSCROLLABLE (MozillaWindowClass)
    //
    // We need to have the middle window, otherwise the Trackpoint driver
    // will fail to deliver scroll messages.  WM_MOUSEWHEEL messages are
    // sent to the FAKETRACKPOINTSCROLLABLE, which then propagate up the
    // window hierarchy until they are handled by nsWindow::WindowProc.
    // WM_HSCROLL messages are also sent to the FAKETRACKPOINTSCROLLABLE,
    // but these do not propagate automatically, so we have the window
    // procedure pretend that they were dispatched to the top-level window
    // instead.
    //
    // The FAKETRACKPOINTSCROLLABLE needs to have the specific window styles it
    // is given below so that it catches the Trackpoint driver's heuristics.
    HWND scrollContainerWnd = ::CreateWindowW(
        className, L"FAKETRACKPOINTSCROLLCONTAINER", WS_CHILD | WS_VISIBLE, 0,
        0, 0, 0, mWnd, nullptr, nsToolkit::mDllInstance, nullptr);
    HWND scrollableWnd = ::CreateWindowW(
        className, L"FAKETRACKPOINTSCROLLABLE",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_TABSTOP | 0x30, 0, 0, 0, 0,
        scrollContainerWnd, nullptr, nsToolkit::mDllInstance, nullptr);

    // Give the FAKETRACKPOINTSCROLLABLE window a specific ID so that
    // WindowProcInternal can distinguish it from the top-level window
    // easily.
    ::SetWindowLongPtrW(scrollableWnd, GWLP_ID, eFakeTrackPointScrollableID);

    // Make FAKETRACKPOINTSCROLLABLE use nsWindow::WindowProc, and store the
    // old window procedure in its "user data".
    WNDPROC oldWndProc = (WNDPROC)::SetWindowLongPtrW(
        scrollableWnd, GWLP_WNDPROC, (LONG_PTR)nsWindow::WindowProc);
    ::SetWindowLongPtrW(scrollableWnd, GWLP_USERDATA, (LONG_PTR)oldWndProc);
  }

  // We will start receiving native events after associating with our native
  // window. We will also become the output of WinUtils::GetNSWindowPtr for that
  // window.
  if (!AssociateWithNativeWindow()) {
    return NS_ERROR_FAILURE;
  }

  // Starting with Windows XP, a process always runs within a terminal services
  // session. In order to play nicely with RDP, fast user switching, and the
  // lock screen, we should be handling WM_WTSSESSION_CHANGE. We must register
  // our HWND in order to receive this message.
  DebugOnly<BOOL> wtsRegistered =
      ::WTSRegisterSessionNotification(mWnd, NOTIFY_FOR_THIS_SESSION);
  NS_ASSERTION(wtsRegistered, "WTSRegisterSessionNotification failed!\n");

  mDefaultIMC.Init(this);
  IMEHandler::InitInputContext(this, mInputContext);

  static bool a11yPrimed = false;
  if (!a11yPrimed && mWindowType == WindowType::TopLevel) {
    a11yPrimed = true;
    if (Preferences::GetInt("accessibility.force_disabled", 0) == -1) {
      ::PostMessage(mWnd, MOZ_WM_STARTA11Y, 0, 0);
    }
  }

  RecreateDirectManipulationIfNeeded();

  return NS_OK;
}

void nsWindow::LocalesChanged() {
  bool isRTL = intl::LocaleService::GetInstance()->IsAppLocaleRTL();
  if (mIsRTL != isRTL) {
    DWORD dwAttribute = isRTL;
    DwmSetWindowAttribute(mWnd, DWMWA_NONCLIENT_RTL_LAYOUT, &dwAttribute,
                          sizeof dwAttribute);
    mIsRTL = isRTL;
  }
}

// Close this nsWindow
void nsWindow::Destroy() {
  // WM_DESTROY has already fired, avoid calling it twice
  if (mOnDestroyCalled) return;

  // Don't destroy windows that have file pickers open, we'll tear these down
  // later once the picker is closed.
  mDestroyCalled = true;
  if (mPickerDisplayCount) return;

  // During the destruction of all of our children, make sure we don't get
  // deleted.
  nsCOMPtr<nsIWidget> kungFuDeathGrip(this);

  DestroyDirectManipulation();

  /**
   * On windows the LayerManagerOGL destructor wants the widget to be around for
   * cleanup. It also would like to have the HWND intact, so we nullptr it here.
   */
  DestroyLayerManager();

  // The DestroyWindow function destroys the specified window. The function
  // sends WM_DESTROY and WM_NCDESTROY messages to the window to deactivate it
  // and remove the keyboard focus from it. The function also destroys the
  // window's menu, flushes the thread message queue, destroys timers, removes
  // clipboard ownership, and breaks the clipboard viewer chain (if the window
  // is at the top of the viewer chain).
  //
  // If the specified window is a parent or owner window, DestroyWindow
  // automatically destroys the associated child or owned windows when it
  // destroys the parent or owner window. The function first destroys child or
  // owned windows, and then it destroys the parent or owner window.
  VERIFY(::DestroyWindow(mWnd));

  // Our windows can be subclassed which may prevent us receiving WM_DESTROY. If
  // OnDestroy() didn't get called, call it now.
  if (!mOnDestroyCalled) {
    MSGResult msgResult;
    mWindowHook.Notify(mWnd, WM_DESTROY, 0, 0, msgResult);
    OnDestroy();
  }
}

/**************************************************************
 *
 * SECTION: Window class utilities
 *
 * Utilities for calculating the proper window class name for
 * Create window.
 *
 **************************************************************/

static void RegisterWindowClass(const wchar_t* aClassName, UINT aExtraStyle,
                                LPWSTR aIconID) {
  WNDCLASSW wc = {};
  if (::GetClassInfoW(nsToolkit::mDllInstance, aClassName, &wc)) {
    // already registered
    return;
  }

  wc.style = CS_DBLCLKS | aExtraStyle;
  wc.lpfnWndProc = WinUtils::NonClientDpiScalingDefWindowProcW;
  wc.hInstance = nsToolkit::mDllInstance;
  wc.hIcon =
      aIconID ? ::LoadIconW(::GetModuleHandleW(nullptr), aIconID) : nullptr;
  wc.lpszClassName = aClassName;

  // Since we discard WM_ERASEBKGND events, the window-class background brush is
  // mostly not used -- it shows up when resizing, but scarcely ever otherwise.
  //
  // In theory we could listen for theme changes and set this brush to an
  // appropriate background color as needed; but given the hoops Win32 makes us
  // jump through to change class data, it's probably not worth the trouble.
  // (See bug 1901875.) Instead, we just make it dark grey, which is probably
  // acceptable in either light or dark mode.
  wc.hbrBackground = (HBRUSH)::GetStockObject(DKGRAY_BRUSH);

  // Failures are ignored as they are handled when ::CreateWindow fails
  ::RegisterClassW(&wc);
}

static LPWSTR const gStockApplicationIcon = MAKEINTRESOURCEW(32512);

static const wchar_t* ChooseWindowClass(WindowType aWindowType) {
  const wchar_t* className = [aWindowType] {
    switch (aWindowType) {
      case WindowType::Dialog:
        return kClassNameDialog;
      case WindowType::Popup:
        return kClassNameDropShadow;
      default:
        return GetMainWindowClass();
    }
  }();
  RegisterWindowClass(className, 0, gStockApplicationIcon);
  return className;
}

/**************************************************************
 *
 * SECTION: Window styles utilities
 *
 * Return the proper windows styles and extended styles.
 *
 **************************************************************/

const DWORD kTitlebarItemsWindowStyles =
    WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
const DWORD kAllBorderStyles =
    kTitlebarItemsWindowStyles | WS_THICKFRAME | WS_DLGFRAME;

static DWORD WindowStylesRemovedForBorderStyle(BorderStyle aStyle) {
  if (aStyle == BorderStyle::Default || aStyle == BorderStyle::All) {
    return 0;
  }
  if (aStyle == BorderStyle::None) {
    return kAllBorderStyles;
  }
  DWORD toRemove = 0;
  if (!(aStyle & BorderStyle::Border)) {
    toRemove |= WS_BORDER;
  }
  if (!(aStyle & BorderStyle::Title)) {
    toRemove |= WS_DLGFRAME;
  }
  if (!(aStyle & (BorderStyle::Menu | BorderStyle::Close))) {
    // Looks like getting rid of the system menu also does away with the close
    // box. So, we only get rid of the system menu and the close box if you
    // want neither. How does the Windows "Dialog" window class get just
    // closebox and no sysmenu? Who knows.
    toRemove |= WS_SYSMENU;
  }
  if (!(aStyle & BorderStyle::ResizeH)) {
    toRemove |= WS_THICKFRAME;
  }
  if (!(aStyle & BorderStyle::Minimize)) {
    toRemove |= WS_MINIMIZEBOX;
  }
  if (!(aStyle & BorderStyle::Maximize)) {
    toRemove |= WS_MAXIMIZEBOX;
  }
  return toRemove;
}

// Return nsWindow styles
DWORD nsWindow::WindowStyle() {
  DWORD style;
  switch (mWindowType) {
    case WindowType::Child:
      style = WS_OVERLAPPED;
      break;

    case WindowType::Dialog:
      style = WS_OVERLAPPED | WS_BORDER | WS_DLGFRAME | WS_SYSMENU |
              DS_MODALFRAME | WS_CLIPCHILDREN;
      if (mBorderStyle != BorderStyle::Default) {
        style |= WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
      }
      break;

    case WindowType::Popup:
      style = WS_OVERLAPPED | WS_POPUP | WS_CLIPCHILDREN;
      break;

    default:
      NS_ERROR("unknown border style");
      [[fallthrough]];

    case WindowType::TopLevel:
      style = WS_OVERLAPPED | WS_CLIPCHILDREN | WS_DLGFRAME | WS_BORDER |
              WS_THICKFRAME | kTitlebarItemsWindowStyles;
      break;
  }

  style &= ~WindowStylesRemovedForBorderStyle(mBorderStyle);

  VERIFY_WINDOW_STYLE(style);
  return style;
}

// Return nsWindow extended styles
DWORD nsWindow::WindowExStyle() {
  switch (mWindowType) {
    case WindowType::Child:
      return 0;
    case WindowType::Popup: {
      DWORD extendedStyle = WS_EX_TOOLWINDOW;
      if (mPopupLevel == PopupLevel::Top) {
        extendedStyle |= WS_EX_TOPMOST;
      }
      return extendedStyle;
    }
    case WindowType::Dialog:
    case WindowType::TopLevel:
    case WindowType::Invisible:
      break;
  }
  if (mIsAlert) {
    MOZ_ASSERT(mWindowType == WindowType::Dialog,
               "Expect alert windows to have type=dialog");
    return WS_EX_TOOLWINDOW | WS_EX_TOPMOST;
  }
  return WS_EX_WINDOWEDGE;
}

/**************************************************************
 *
 * SECTION: Native window association utilities
 *
 * Used in Create and Destroy. A nsWindow can associate with its
 * underlying native window mWnd. Once a native window is
 * associated with a nsWindow, its native events will be handled
 * by the static member function nsWindow::WindowProc. Moreover,
 * the association will be registered in the WinUtils association
 * list, that is, calling WinUtils::GetNSWindowPtr on the native
 * window will return the associated nsWindow. This is used in
 * nsWindow::WindowProc to correctly dispatch native events to
 * the handler methods defined in nsWindow, even though it is a
 * static member function.
 *
 * After dissociation, the native events of the native window will
 * no longer be handled by nsWindow::WindowProc, and will thus not
 * be dispatched to the nsWindow native event handler methods.
 * Moreover, the association will no longer be registered in the
 * WinUtils association list, so calling WinUtils::GetNSWindowPtr
 * on the native window will return nullptr.
 *
 **************************************************************/

bool nsWindow::AssociateWithNativeWindow() {
  if (!mWnd || !IsWindow(mWnd)) {
    NS_ERROR("Invalid window handle");
    return false;
  }

  // Connect the this pointer to the native window handle.
  // This should be done before SetWindowLongPtrW, because nsWindow::WindowProc
  // uses WinUtils::GetNSWindowPtr internally.
  WinUtils::SetNSWindowPtr(mWnd, this);

  ::SetLastError(ERROR_SUCCESS);
  const auto prevWndProc = reinterpret_cast<WNDPROC>(::SetWindowLongPtrW(
      mWnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(nsWindow::WindowProc)));
  if (!prevWndProc && GetLastError() != ERROR_SUCCESS) {
    NS_ERROR("Failure in SetWindowLongPtrW");
    WinUtils::SetNSWindowPtr(mWnd, nullptr);
    return false;
  }

  mPrevWndProc.emplace(prevWndProc);
  return true;
}

void nsWindow::DissociateFromNativeWindow() {
  if (!mWnd || !IsWindow(mWnd) || mPrevWndProc.isNothing()) {
    return;
  }

  DebugOnly<WNDPROC> wndProcBeforeDissociate =
      reinterpret_cast<WNDPROC>(::SetWindowLongPtrW(
          mWnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(*mPrevWndProc)));
  NS_ASSERTION(wndProcBeforeDissociate == nsWindow::WindowProc,
               "Unstacked an unexpected native window procedure");

  WinUtils::SetNSWindowPtr(mWnd, nullptr);
  mPrevWndProc.reset();
}

void nsWindow::DidClearParent(nsIWidget*) {
  if (mWindowType == WindowType::Popup || !mWnd) {
    return;
  }
  ::SetParent(mWnd, nullptr);
  RecreateDirectManipulationIfNeeded();
}

static int32_t RoundDown(double aDouble) {
  return aDouble > 0 ? static_cast<int32_t>(floor(aDouble))
                     : static_cast<int32_t>(ceil(aDouble));
}

float nsWindow::GetDPI() {
  float dpi = 96.0f;
  nsCOMPtr<nsIScreen> screen = GetWidgetScreen();
  if (screen) {
    screen->GetDpi(&dpi);
  }
  return dpi;
}

double nsWindow::GetDefaultScaleInternal() {
  if (mDefaultScale <= 0.0) {
    mDefaultScale = WinUtils::LogToPhysFactor(mWnd);
  }
  return mDefaultScale;
}

int32_t nsWindow::LogToPhys(double aValue) {
  return WinUtils::LogToPhys(
      ::MonitorFromWindow(mWnd, MONITOR_DEFAULTTOPRIMARY), aValue);
}

nsWindow* nsWindow::GetParentWindow(bool aIncludeOwner) {
  return static_cast<nsWindow*>(GetParentWindowBase(aIncludeOwner));
}

nsWindow* nsWindow::GetParentWindowBase(bool aIncludeOwner) {
  if (IsTopLevelWidget()) {
    // Must use a flag instead of mWindowType to tell if the window is the
    // owned by the topmost widget, because a child window can be embedded
    // inside a HWND which is not associated with a nsIWidget.
    return nullptr;
  }

  // If this widget has already been destroyed, pretend we have no parent.
  // This corresponds to code in Destroy which removes the destroyed
  // widget from its parent's child list.
  if (mInDtor || mOnDestroyCalled) return nullptr;

  // aIncludeOwner set to true implies walking the parent chain to retrieve the
  // root owner. aIncludeOwner set to false implies the search will stop at the
  // true parent (default).
  nsWindow* widget = nullptr;
  if (mWnd) {
    HWND parent = nullptr;
    if (aIncludeOwner)
      parent = ::GetParent(mWnd);
    else
      parent = ::GetAncestor(mWnd, GA_PARENT);

    if (parent) {
      widget = WinUtils::GetNSWindowPtr(parent);
      if (widget) {
        // If the widget is in the process of being destroyed then
        // do NOT return it
        if (widget->mInDtor) {
          widget = nullptr;
        }
      }
    }
  }

  return widget;
}

/**************************************************************
 *
 * SECTION: nsIWidget::Show
 *
 * Hide or show this component.
 *
 **************************************************************/

void nsWindow::Show(bool aState) {
  if (aState && mIsShowingPreXULSkeletonUI) {
    // The first time we decide to actually show the window is when we decide
    // that we've taken over the window from the skeleton UI, and we should
    // no longer treat resizes / moves specially.
    //
    // NOTE(emilio): mIsShowingPreXULSkeletonUI feels a bit odd, or at least
    // misnamed. During regular startup we create the skeleton UI, then the
    // early blank window consumes it, and at that point we set
    // mIsShowingPreXULSkeletonUI to false, but in fact, we're still showing
    // the skeleton UI (because the blank window is, well, blank). We should
    // consider guarding this with !mIsEarlyBlankWindow...
    mIsShowingPreXULSkeletonUI = false;
    // Concomitantly, this is also when we change the cursor away from the
    // default "wait" cursor.
    SetCursor(Cursor{eCursor_standard});
#if defined(ACCESSIBILITY)
    // If our HWND has focus and the a11y engine hasn't started yet, fire a
    // focus win event. Windows already did this when the skeleton UI appeared,
    // but a11y wouldn't have been able to start at that point even if a client
    // responded. Firing this now gives clients the chance to respond with
    // WM_GETOBJECT, which will trigger the a11y engine. We don't want to do
    // this if the a11y engine has already started because it has probably
    // already fired focus on a descendant.
    if (::GetFocus() == mWnd && !GetAccService()) {
      ::NotifyWinEvent(EVENT_OBJECT_FOCUS, mWnd, OBJID_CLIENT, CHILDID_SELF);
    }
#endif  // defined(ACCESSIBILITY)
  }

  MOZ_ASSERT_IF(mWindowType == WindowType::Popup,
                ChooseWindowClass(mWindowType) == kClassNameDropShadow);

  bool syncInvalidate = false;

  bool wasVisible = mIsVisible;
  // Set the status now so that anyone asking during ShowWindow or
  // SetWindowPos would get the correct answer.
  mIsVisible = aState;

  if (mWnd) {
    if (aState) {
      if (!wasVisible && mWindowType == WindowType::TopLevel) {
        // speed up the initial paint after show for
        // top level windows:
        syncInvalidate = true;

        // Cloak (or uncloak) the window.
        //
        // (DWMWA_CLOAK is effectively orthogonal to any cloaking done by the
        // shell to implement virtual desktops; we don't have to worry about
        // accidentally forcing something on another desktop to become visible.)
        constexpr static const auto CloakWindow = [](HWND hwnd, BOOL state) {
          ::DwmSetWindowAttribute(hwnd, DWMWA_CLOAK, &state, sizeof(state));
        };

        // Clear the window using a theme-appropriate color.
        constexpr static const auto ClearWindow = [](HWND hwnd) {
          // default background color from current theme
          auto const bgcolor = LookAndFeel::Color(
              StyleSystemColor::Window, PreferenceSheet::ColorSchemeForChrome(),
              LookAndFeel::UseStandins::No, NS_RGB(0, 0, 0));

          HBRUSH brush = ::CreateSolidBrush(NSRGB_2_COLOREF(bgcolor));
          if (NS_WARN_IF(!brush)) {
            // GDI object cap hit, possibly?
            return;
          }
          auto const _releaseBrush =
              MakeScopeExit([&] { ::DeleteObject(brush); });

          HDC hdc = ::GetWindowDC(hwnd);
          MOZ_ASSERT(hdc);
          auto const _cleanupDC =
              MakeScopeExit([&] { ::ReleaseDC(hwnd, hdc); });

          RECT rect;
          ::GetWindowRect(hwnd, &rect);  // includes non-client area

          // Convert from screen- to client-coordinates, accounting for the
          // desktop (or, in theory, us) possibly being WS_EX_LAYOUTRTL...
          ::MapWindowPoints(HWND_DESKTOP, hwnd, (LPPOINT)&rect, 2);
          // ... then convert from client- to window- coordinates, with no
          // separate RTL-handling needed.
          ::OffsetRect(&rect, -rect.left, -rect.top);

          ::FillRect(hdc, &rect, brush);
        };

        if (!mHasBeenShown) {
          // On creation, the window's content is not specified; in practice,
          // it's observed to usually be full of bright white, regardless of any
          // window-class options. DWM will happily render that unspecified
          // content to the screen before we get a chance to process a
          // WM_ERASEBKGND event (or, indeed, anything else). To avoid dark-mode
          // users being assaulted with a bright white flash, we need to draw
          // something on top of that at least once before showing the window.
          //
          // Unfortunately, there's a bit of a catch-22 here: until the window
          // has been set "visible" at least once, it doesn't have a backing
          // surface, so we can't draw anything to it! To work around this, we
          // cloak the window before "showing" it.
          CloakWindow(mWnd, TRUE);
        }

        // Set the cursor before showing the window to avoid the default wait
        // cursor.
        SetCursor(Cursor{eCursor_standard});

        switch (mFrameState->GetSizeMode()) {
          case nsSizeMode_Fullscreen:
            ::ShowWindow(mWnd, SW_SHOW);
            break;
          case nsSizeMode_Maximized:
            ::ShowWindow(mWnd, SW_SHOWMAXIMIZED);
            break;
          case nsSizeMode_Minimized:
            ::ShowWindow(mWnd, SW_SHOWMINIMIZED);
            break;
          default:
            if (CanTakeFocus() && !mAlwaysOnTop) {
              ::ShowWindow(mWnd, SW_SHOWNORMAL);
            } else {
              ::ShowWindow(mWnd, SW_SHOWNOACTIVATE);
              // Don't flicker the window if we're restoring session
              if (!sIsRestoringSession) {
                Unused << GetAttention(2);
              }
            }
            break;
        }

        if (!mHasBeenShown) {
          // Now that ::ShowWindow() has been called once, the window surface
          // actually exists, so we can draw to it. Fill it with the theme's
          // background color before uncloaking it to complete the Show().
          ClearWindow(mWnd);
          CloakWindow(mWnd, FALSE);
          mHasBeenShown = true;
        }

      } else {
        DWORD flags = SWP_NOSIZE | SWP_NOMOVE | SWP_SHOWWINDOW;
        if (wasVisible) {
          flags |= SWP_NOZORDER;
        }
        if (mAlwaysOnTop || mIsAlert) {
          flags |= SWP_NOACTIVATE;
        }

        if (mWindowType == WindowType::Popup) {
          // ensure popups are the topmost of the TOPMOST
          // layer. Remember not to set the SWP_NOZORDER
          // flag as that might allow the taskbar to overlap
          // the popup.
          flags |= SWP_NOACTIVATE | SWP_NOOWNERZORDER;
          HWND owner = ::GetWindow(mWnd, GW_OWNER);
          if (owner) {
            // PopupLevel::Top popups should be above all else.  All other
            // types should be placed in front of their owner, without
            // changing the owner's z-level relative to other windows.
            if (mPopupLevel != PopupLevel::Top) {
              ::SetWindowPos(mWnd, owner, 0, 0, 0, 0, flags);
              ::SetWindowPos(
                  owner, mWnd, 0, 0, 0, 0,
                  SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
            } else {
              ::SetWindowPos(mWnd, HWND_TOP, 0, 0, 0, 0, flags);
            }
          } else {
            ::SetWindowPos(mWnd, HWND_TOPMOST, 0, 0, 0, 0, flags);
          }
        } else {
          if (mWindowType == WindowType::Dialog && !CanTakeFocus())
            flags |= SWP_NOACTIVATE;

          ::SetWindowPos(mWnd, HWND_TOP, 0, 0, 0, 0, flags);
        }
      }
    } else {
      if (mWindowType != WindowType::Dialog) {
        ::ShowWindow(mWnd, SW_HIDE);
      } else {
        ::SetWindowPos(mWnd, 0, 0, 0, 0, 0,
                       SWP_HIDEWINDOW | SWP_NOSIZE | SWP_NOMOVE | SWP_NOZORDER |
                           SWP_NOACTIVATE);
      }
    }
  }

  if (!wasVisible && aState) {
    Invalidate();
    if (syncInvalidate && !mInDtor && !mOnDestroyCalled) {
      ::UpdateWindow(mWnd);
    }
  }

  if (mOpeningAnimationSuppressed) {
    SuppressAnimation(false);
  }
}

/**************************************************************
 *
 * SECTION: nsIWidget::IsVisible
 *
 * Returns the visibility state.
 *
 **************************************************************/

// Return true if the component is visible, false otherwise.
//
// This does not take cloaking into account.
bool nsWindow::IsVisible() const { return mIsVisible; }

/**************************************************************
 *
 * SECTION: Touch and APZ-related functions
 *
 **************************************************************/

void nsWindow::RegisterTouchWindow() {
  mTouchWindow = true;
  ::RegisterTouchWindow(mWnd, TWF_WANTPALM);
  ::EnumChildWindows(mWnd, nsWindow::RegisterTouchForDescendants, 0);
}

BOOL CALLBACK nsWindow::RegisterTouchForDescendants(HWND aWnd, LPARAM aMsg) {
  nsWindow* win = WinUtils::GetNSWindowPtr(aWnd);
  if (win) {
    ::RegisterTouchWindow(aWnd, TWF_WANTPALM);
  }
  return TRUE;
}

void nsWindow::LockAspectRatio(bool aShouldLock) {
  if (aShouldLock) {
    mAspectRatio = (float)mBounds.Width() / (float)mBounds.Height();
  } else {
    mAspectRatio = 0.0;
  }
}

/**************************************************************
 *
 * SECTION: nsIWidget::SetInputRegion
 *
 * Sets whether the window should ignore mouse events.
 *
 **************************************************************/
void nsWindow::SetInputRegion(const InputRegion& aInputRegion) {
  mInputRegion = aInputRegion;
}

/**************************************************************
 *
 * SECTION: nsIWidget::Move, nsIWidget::Resize, nsIWidget::Size
 *
 * Repositioning and sizing a window.
 *
 **************************************************************/

void nsWindow::SetSizeConstraints(const SizeConstraints& aConstraints) {
  SizeConstraints c = aConstraints;

  if (mWindowType != WindowType::Popup && mResizable) {
    c.mMinSize.width =
        std::max(int32_t(::GetSystemMetrics(SM_CXMINTRACK)), c.mMinSize.width);
    c.mMinSize.height =
        std::max(int32_t(::GetSystemMetrics(SM_CYMINTRACK)), c.mMinSize.height);
  }

  if (mMaxTextureSize > 0) {
    // We can't make ThebesLayers bigger than this anyway.. no point it letting
    // a window grow bigger as we won't be able to draw content there in
    // general.
    c.mMaxSize.width = std::min(c.mMaxSize.width, mMaxTextureSize);
    c.mMaxSize.height = std::min(c.mMaxSize.height, mMaxTextureSize);
  }

  mSizeConstraintsScale = GetDefaultScale().scale;

  nsBaseWidget::SetSizeConstraints(c);
}

const SizeConstraints nsWindow::GetSizeConstraints() {
  double scale = GetDefaultScale().scale;
  if (mSizeConstraintsScale == scale || mSizeConstraintsScale == 0.0) {
    return mSizeConstraints;
  }
  scale /= mSizeConstraintsScale;
  SizeConstraints c = mSizeConstraints;
  if (c.mMinSize.width != NS_MAXSIZE) {
    c.mMinSize.width = NSToIntRound(c.mMinSize.width * scale);
  }
  if (c.mMinSize.height != NS_MAXSIZE) {
    c.mMinSize.height = NSToIntRound(c.mMinSize.height * scale);
  }
  if (c.mMaxSize.width != NS_MAXSIZE) {
    c.mMaxSize.width = NSToIntRound(c.mMaxSize.width * scale);
  }
  if (c.mMaxSize.height != NS_MAXSIZE) {
    c.mMaxSize.height = NSToIntRound(c.mMaxSize.height * scale);
  }
  return c;
}

// Move this component
void nsWindow::Move(double aX, double aY) {
  if (mWindowType == WindowType::TopLevel ||
      mWindowType == WindowType::Dialog) {
    SetSizeMode(nsSizeMode_Normal);
  }

  // for top-level windows only, convert coordinates from desktop pixels
  // (the "parent" coordinate space) to the window's device pixel space
  double scale =
      BoundsUseDesktopPixels() ? GetDesktopToDeviceScale().scale : 1.0;
  int32_t x = NSToIntRound(aX * scale);
  int32_t y = NSToIntRound(aY * scale);

  // Check to see if window needs to be moved first
  // to avoid a costly call to SetWindowPos. This check
  // can not be moved to the calling code in nsView, because
  // some platforms do not position child windows correctly

  // Only perform this check for non-popup windows, since the positioning can
  // in fact change even when the x/y do not.  We always need to perform the
  // check. See bug #97805 for details.
  if (mWindowType != WindowType::Popup && mBounds.IsEqualXY(x, y)) {
    // Nothing to do, since it is already positioned correctly.
    return;
  }

  // Normally, when the skeleton UI is disabled, we resize+move the window
  // before showing it in order to ensure that it restores to the correct
  // position when the user un-maximizes it. However, when we are using the
  // skeleton UI, this results in the skeleton UI window being moved around
  // undesirably before being locked back into the maximized position. To
  // avoid this, we simply set the placement to restore to via
  // SetWindowPlacement. It's a little bit more of a dance, though, since we
  // need to convert the workspace coords that SetWindowPlacement uses to the
  // screen space coordinates we normally use with SetWindowPos.
  if (mIsShowingPreXULSkeletonUI && WasPreXULSkeletonUIMaximized()) {
    WINDOWPLACEMENT pl = {sizeof(WINDOWPLACEMENT)};
    VERIFY(::GetWindowPlacement(mWnd, &pl));

    HMONITOR monitor = ::MonitorFromWindow(mWnd, MONITOR_DEFAULTTONULL);
    if (NS_WARN_IF(!monitor)) {
      return;
    }
    MONITORINFO mi = {sizeof(MONITORINFO)};
    VERIFY(::GetMonitorInfo(monitor, &mi));

    int32_t deltaX =
        x + mi.rcWork.left - mi.rcMonitor.left - pl.rcNormalPosition.left;
    int32_t deltaY =
        y + mi.rcWork.top - mi.rcMonitor.top - pl.rcNormalPosition.top;
    pl.rcNormalPosition.left += deltaX;
    pl.rcNormalPosition.right += deltaX;
    pl.rcNormalPosition.top += deltaY;
    pl.rcNormalPosition.bottom += deltaY;
    VERIFY(::SetWindowPlacement(mWnd, &pl));
    return;
  }

  mBounds.MoveTo(x, y);

  if (mWnd) {
#ifdef DEBUG
    // complain if a window is moved offscreen (legal, but potentially
    // worrisome)
    if (IsTopLevelWidget()) {  // only a problem for top-level windows
      // Make sure this window is actually on the screen before we move it
      // XXX: Needs multiple monitor support
      HDC dc = ::GetDC(mWnd);
      if (dc) {
        if (::GetDeviceCaps(dc, TECHNOLOGY) == DT_RASDISPLAY) {
          RECT workArea;
          ::SystemParametersInfo(SPI_GETWORKAREA, 0, &workArea, 0);
          // no annoying assertions. just mention the issue.
          if (x < 0 || x >= workArea.right || y < 0 || y >= workArea.bottom) {
            MOZ_LOG(gWindowsLog, LogLevel::Info,
                    ("window moved to offscreen position\n"));
          }
        }
        ::ReleaseDC(mWnd, dc);
      }
    }
#endif
    UINT flags = SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOSIZE;
    double oldScale = mDefaultScale;
    mResizeState = IN_SIZEMOVE;
    VERIFY(::SetWindowPos(mWnd, nullptr, x, y, 0, 0, flags));
    mResizeState = NOT_RESIZING;
    if (WinUtils::LogToPhysFactor(mWnd) != oldScale) {
      ChangedDPI();
    }

    ResizeDirectManipulationViewport();
  }
}

// Resize this component
void nsWindow::Resize(double aWidth, double aHeight, bool aRepaint) {
  // for top-level windows only, convert coordinates from desktop pixels
  // (the "parent" coordinate space) to the window's device pixel space
  double scale =
      BoundsUseDesktopPixels() ? GetDesktopToDeviceScale().scale : 1.0;
  int32_t width = NSToIntRound(aWidth * scale);
  int32_t height = NSToIntRound(aHeight * scale);

  NS_ASSERTION((width >= 0), "Negative width passed to nsWindow::Resize");
  NS_ASSERTION((height >= 0), "Negative height passed to nsWindow::Resize");
  if (width < 0 || height < 0) {
    gfxCriticalNoteOnce << "Negative passed to Resize(" << width << ", "
                        << height << ") repaint: " << aRepaint;
  }

  ConstrainSize(&width, &height);

  // Avoid unnecessary resizing calls
  if (mBounds.IsEqualSize(width, height)) {
    if (aRepaint) {
      Invalidate();
    }
    return;
  }

  // Refer to the comment above a similar check in nsWindow::Move
  if (mIsShowingPreXULSkeletonUI && WasPreXULSkeletonUIMaximized()) {
    WINDOWPLACEMENT pl = {sizeof(WINDOWPLACEMENT)};
    VERIFY(::GetWindowPlacement(mWnd, &pl));
    pl.rcNormalPosition.right = pl.rcNormalPosition.left + width;
    pl.rcNormalPosition.bottom = pl.rcNormalPosition.top + height;
    mResizeState = RESIZING;
    VERIFY(::SetWindowPlacement(mWnd, &pl));
    mResizeState = NOT_RESIZING;
    return;
  }

  // Set cached value for lightweight and printing
  bool wasLocking = mAspectRatio != 0.0;
  mBounds.SizeTo(width, height);
  if (wasLocking) {
    LockAspectRatio(true);  // This causes us to refresh the mAspectRatio value
  }

  if (mWnd) {
    UINT flags = SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOMOVE;
    if (!aRepaint) {
      flags |= SWP_NOREDRAW;
    }
    double oldScale = mDefaultScale;
    mResizeState = RESIZING;
    VERIFY(::SetWindowPos(mWnd, nullptr, 0, 0, width, height, flags));
    mResizeState = NOT_RESIZING;
    if (WinUtils::LogToPhysFactor(mWnd) != oldScale) {
      ChangedDPI();
    }
    ResizeDirectManipulationViewport();
  }

  if (aRepaint) Invalidate();
}

// Resize this component
void nsWindow::Resize(double aX, double aY, double aWidth, double aHeight,
                      bool aRepaint) {
  // for top-level windows only, convert coordinates from desktop pixels
  // (the "parent" coordinate space) to the window's device pixel space
  double scale =
      BoundsUseDesktopPixels() ? GetDesktopToDeviceScale().scale : 1.0;
  int32_t x = NSToIntRound(aX * scale);
  int32_t y = NSToIntRound(aY * scale);
  int32_t width = NSToIntRound(aWidth * scale);
  int32_t height = NSToIntRound(aHeight * scale);

  NS_ASSERTION((width >= 0), "Negative width passed to nsWindow::Resize");
  NS_ASSERTION((height >= 0), "Negative height passed to nsWindow::Resize");
  if (width < 0 || height < 0) {
    gfxCriticalNoteOnce << "Negative passed to Resize(" << x << " ," << y
                        << ", " << width << ", " << height
                        << ") repaint: " << aRepaint;
  }

  ConstrainSize(&width, &height);

  // Avoid unnecessary resizing calls
  if (mBounds.IsEqualRect(x, y, width, height)) {
    if (aRepaint) {
      Invalidate();
    }
    return;
  }

  // Refer to the comment above a similar check in nsWindow::Move
  if (mIsShowingPreXULSkeletonUI && WasPreXULSkeletonUIMaximized()) {
    WINDOWPLACEMENT pl = {sizeof(WINDOWPLACEMENT)};
    VERIFY(::GetWindowPlacement(mWnd, &pl));

    HMONITOR monitor = ::MonitorFromWindow(mWnd, MONITOR_DEFAULTTONULL);
    if (NS_WARN_IF(!monitor)) {
      return;
    }
    MONITORINFO mi = {sizeof(MONITORINFO)};
    VERIFY(::GetMonitorInfo(monitor, &mi));

    int32_t deltaX =
        x + mi.rcWork.left - mi.rcMonitor.left - pl.rcNormalPosition.left;
    int32_t deltaY =
        y + mi.rcWork.top - mi.rcMonitor.top - pl.rcNormalPosition.top;
    pl.rcNormalPosition.left += deltaX;
    pl.rcNormalPosition.right = pl.rcNormalPosition.left + width;
    pl.rcNormalPosition.top += deltaY;
    pl.rcNormalPosition.bottom = pl.rcNormalPosition.top + height;
    VERIFY(::SetWindowPlacement(mWnd, &pl));
    return;
  }

  // Set cached value for lightweight and printing
  mBounds.SetRect(x, y, width, height);

  if (mWnd) {
    UINT flags = SWP_NOZORDER | SWP_NOACTIVATE;
    if (!aRepaint) {
      flags |= SWP_NOREDRAW;
    }

    double oldScale = mDefaultScale;
    mResizeState = RESIZING;
    VERIFY(::SetWindowPos(mWnd, nullptr, x, y, width, height, flags));
    mResizeState = NOT_RESIZING;
    if (WinUtils::LogToPhysFactor(mWnd) != oldScale) {
      ChangedDPI();
    }

    if (mTransitionWnd) {
      // If we have a fullscreen transition window, we need to make
      // it topmost again, otherwise the taskbar may be raised by
      // the system unexpectedly when we leave fullscreen state.
      ::SetWindowPos(mTransitionWnd, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }

    ResizeDirectManipulationViewport();
  }

  if (aRepaint) Invalidate();
}

/**************************************************************
 *
 * SECTION: Window state.
 *
 * nsIWidget::SetSizeMode, nsIWidget::ConstrainPosition
 *
 * Positioning, restore, minimize, and maximize.
 *
 **************************************************************/

static UINT GetCurrentShowCmd(HWND aWnd) {
  WINDOWPLACEMENT pl;
  pl.length = sizeof(pl);
  ::GetWindowPlacement(aWnd, &pl);
  return pl.showCmd;
}

// Maximize, minimize or restore the window.
void nsWindow::SetSizeMode(nsSizeMode aMode) {
  // If we are still displaying a maximized pre-XUL skeleton UI, ignore the
  // noise of sizemode changes. Once we have "shown" the window for the first
  // time (called nsWindow::Show(true), even though the window is already
  // technically displayed), we will again accept sizemode changes.
  if (mIsShowingPreXULSkeletonUI && WasPreXULSkeletonUIMaximized()) {
    return;
  }

  mFrameState->EnsureSizeMode(aMode);
}

nsSizeMode nsWindow::SizeMode() { return mFrameState->GetSizeMode(); }

void DoGetWorkspaceID(HWND aWnd, nsAString* aWorkspaceID) {
  RefPtr<IVirtualDesktopManager> desktopManager = gVirtualDesktopManager;
  if (!desktopManager || !aWnd) {
    return;
  }

  GUID desktop;
  HRESULT hr = desktopManager->GetWindowDesktopId(aWnd, &desktop);
  if (FAILED(hr)) {
    return;
  }

  RPC_WSTR workspaceIDStr = nullptr;
  if (UuidToStringW(&desktop, &workspaceIDStr) == RPC_S_OK) {
    aWorkspaceID->Assign((wchar_t*)workspaceIDStr);
    RpcStringFreeW(&workspaceIDStr);
  }
}

void nsWindow::GetWorkspaceID(nsAString& workspaceID) {
  // If we have a value cached, use that, but also make sure it is
  // scheduled to be updated.  If we don't yet have a value, get
  // one synchronously.
  auto desktop = mDesktopId.Lock();
  if (desktop->mID.IsEmpty()) {
    DoGetWorkspaceID(mWnd, &desktop->mID);
    desktop->mUpdateIsQueued = false;
  } else {
    AsyncUpdateWorkspaceID(*desktop);
  }

  workspaceID = desktop->mID;
}

void nsWindow::AsyncUpdateWorkspaceID(Desktop& aDesktop) {
  struct UpdateWorkspaceIdTask : public Task {
    explicit UpdateWorkspaceIdTask(nsWindow* aSelf)
        : Task(Kind::OffMainThreadOnly, EventQueuePriority::Normal),
          mSelf(aSelf) {}

    TaskResult Run() override {
      RefPtr<nsWindow> self(mSelf);
      // If the window is not alive anymore, no need to do anything
      if (!self) {
        return TaskResult::Complete;
      }
      auto desktop = self->mDesktopId.Lock();
      if (desktop->mUpdateIsQueued) {
        DoGetWorkspaceID(self->mWnd, &desktop->mID);
        desktop->mUpdateIsQueued = false;
      }
      return TaskResult::Complete;
    }

#ifdef MOZ_COLLECTING_RUNNABLE_TELEMETRY
    bool GetName(nsACString& aName) override {
      aName.AssignLiteral("UpdateWorkspaceIdTask");
      return true;
    }
#endif

    // Only hold a weak pointer so this structure can't keep the window alive
    // and possibly Release() it on the wrong thread (bug 1824697)
    ThreadSafeWeakPtr<nsWindow> mSelf;
  };

  if (aDesktop.mUpdateIsQueued) {
    return;
  }

  aDesktop.mUpdateIsQueued = true;
  TaskController::Get()->AddTask(MakeAndAddRef<UpdateWorkspaceIdTask>(this));
}

void nsWindow::MoveToWorkspace(const nsAString& workspaceID) {
  RefPtr<IVirtualDesktopManager> desktopManager = gVirtualDesktopManager;
  if (!desktopManager) {
    return;
  }

  GUID desktop;
  const nsString flat = PromiseFlatString(workspaceID);
  RPC_WSTR workspaceIDStr = reinterpret_cast<RPC_WSTR>((wchar_t*)flat.get());
  if (UuidFromStringW(workspaceIDStr, &desktop) == RPC_S_OK) {
    if (SUCCEEDED(desktopManager->MoveWindowToDesktop(mWnd, desktop))) {
      auto desktop = mDesktopId.Lock();
      desktop->mID = workspaceID;
    }
  }
}

void nsWindow::SuppressAnimation(bool aSuppress) {
  DWORD dwAttribute = aSuppress ? TRUE : FALSE;
  DwmSetWindowAttribute(mWnd, DWMWA_TRANSITIONS_FORCEDISABLED, &dwAttribute,
                        sizeof dwAttribute);
}

// Constrain a potential move to fit onscreen
// Position (aX, aY) is specified in Windows screen (logical) pixels,
// except when using per-monitor DPI, in which case it's device pixels.
void nsWindow::ConstrainPosition(DesktopIntPoint& aPoint) {
  if (!IsTopLevelWidget()) {
    // only a problem for top-level windows
    return;
  }

  // If the window is already at (0, 0), nothing we do to it here can help.
  // Leave it alone.
  //
  // (This also happens to cover the case where the window was Aero Snapped into
  // the upper-left corner.)
  if (aPoint == DesktopIntPoint{0, 0}) {
    return;
  }

  double dpiScale = GetDesktopToDeviceScale().scale;

  // We need to use the window size in the kind of pixels used for window-
  // manipulation APIs.
  int32_t logWidth =
      std::max<int32_t>(NSToIntRound(mBounds.Width() / dpiScale), 1);
  int32_t logHeight =
      std::max<int32_t>(NSToIntRound(mBounds.Height() / dpiScale), 1);

  /* get our playing field. use the current screen, or failing that
  for any reason, use device caps for the default screen. */
  DesktopIntRect screenRect;

  nsCOMPtr<nsIScreenManager> screenmgr =
      do_GetService(sScreenManagerContractID);
  if (!screenmgr) {
    return;
  }
  nsCOMPtr<nsIScreen> screen;

  screenmgr->ScreenForRect(aPoint.x, aPoint.y, logWidth, logHeight,
                           getter_AddRefs(screen));
  if (mFrameState->GetSizeMode() != nsSizeMode_Fullscreen) {
    // For normalized windows, use the desktop work area.
    screenRect = screen->GetAvailRectDisplayPix();
  } else {
    // For full screen windows, use the desktop.
    screenRect = screen->GetRectDisplayPix();
  }

  // Check for the case where the window was Aero Snapped to the right. (The
  // window will extend off the right and bottom of the screen in this case by a
  // small but DPI-dependent value.)
  //
  // We do not check WINDOWPLACEMENT for a position mismatch. That would catch
  // whether the window is _currently_ Aero Snapped to the right, but we may be
  // restoring the window. (We can't guarantee a restore into a snapped state:
  // there is no known API to do so. Fortunately, the shell seems to detect this
  // case anyway, and treats the window as snapped.)
  //
  // Note that this _is_ a heuristic. False positives are possible; but they
  // seem unlikely (it would require manually positioning a window to extend
  // just barely offscreen to the lower right), and anyway are probably
  // harmless: the effect will simply be that we leave the window exactly where
  // the user put it, instead of nudging it slightly.
  if (aPoint.y == 0) {
    auto const xMax = aPoint.x + logWidth;
    auto const yMax = aPoint.y + logHeight;
    auto const deltaX = xMax - screenRect.XMost();
    auto const deltaY = yMax - screenRect.YMost();
    if (deltaX == deltaY) {
      if (8 <= deltaX && deltaX <= 16) {
        // If so, don't try to fix the position; Windows will (probably) deal
        // with it.
        return;
      }
    }
  }

  aPoint = ConstrainPositionToBounds(aPoint, {logWidth, logHeight}, screenRect);
}

/**************************************************************
 *
 * SECTION: nsIWidget::Enable, nsIWidget::IsEnabled
 *
 * Enabling and disabling the widget.
 *
 **************************************************************/

// Enable/disable this component
void nsWindow::Enable(bool aState) {
  if (mWnd) {
    ::EnableWindow(mWnd, aState);
  }
}

// Return the current enable state
bool nsWindow::IsEnabled() const {
  return !mWnd || (::IsWindowEnabled(mWnd) &&
                   ::IsWindowEnabled(::GetAncestor(mWnd, GA_ROOT)));
}

/**************************************************************
 *
 * SECTION: nsIWidget::SetFocus
 *
 * Give the focus to this widget.
 *
 **************************************************************/

void nsWindow::SetFocus(Raise aRaise, mozilla::dom::CallerType aCallerType) {
  if (!mWnd) {
    return;
  }
#ifdef WINSTATE_DEBUG_OUTPUT
  if (mWnd == WinUtils::GetTopLevelHWND(mWnd)) {
    MOZ_LOG(gWindowsLog, LogLevel::Info,
            ("*** SetFocus: [  top] raise=%d\n", aRaise == Raise::Yes));
  } else {
    MOZ_LOG(gWindowsLog, LogLevel::Info,
            ("*** SetFocus: [child] raise=%d\n", aRaise == Raise::Yes));
  }
#endif
  // Uniconify, if necessary
  HWND toplevelWnd = WinUtils::GetTopLevelHWND(mWnd);
  if (aRaise == Raise::Yes && ::IsIconic(toplevelWnd)) {
    ::ShowWindow(toplevelWnd, SW_RESTORE);
  }
  ::SetFocus(mWnd);
}

/**************************************************************
 *
 * SECTION: Bounds
 *
 * GetBounds, GetClientBounds, GetScreenBounds,
 * GetRestoredBounds, GetClientOffset, SetCustomTitlebar
 *
 * Bound calculations.
 *
 **************************************************************/

// Return the window's full dimensions in screen coordinates.
// If the window has a parent, converts the origin to an offset
// of the parent's screen origin.
LayoutDeviceIntRect nsWindow::GetBounds() {
  if (!mWnd) {
    return mBounds;
  }

  RECT r;
  VERIFY(::GetWindowRect(mWnd, &r));

  LayoutDeviceIntRect rect;

  // assign size
  rect.SizeTo(r.right - r.left, r.bottom - r.top);

  // popup window bounds' are in screen coordinates, not relative to parent
  // window
  if (mWindowType == WindowType::Popup) {
    rect.MoveTo(r.left, r.top);
    return rect;
  }

  // chrome on parent:
  //  ___      5,5   (chrome start)
  // |  ____   10,10 (client start)
  // | |  ____ 20,20 (child start)
  // | | |
  // 20,20 - 5,5 = 15,15 (??)
  // minus GetClientOffset:
  // 15,15 - 5,5 = 10,10
  //
  // no chrome on parent:
  //  ______   10,10 (win start)
  // |  ____   20,20 (child start)
  // | |
  // 20,20 - 10,10 = 10,10
  //
  // walking the chain:
  //  ___      5,5   (chrome start)
  // |  ___    10,10 (client start)
  // | |  ___  20,20 (child start)
  // | | |  __ 30,30 (child start)
  // | | | |
  // 30,30 - 20,20 = 10,10 (offset from second child to first)
  // 20,20 - 5,5 = 15,15 + 10,10 = 25,25 (??)
  // minus GetClientOffset:
  // 25,25 - 5,5 = 20,20 (offset from second child to parent client)

  // convert coordinates if parent exists
  HWND parent = ::GetParent(mWnd);
  if (parent) {
    RECT pr;
    VERIFY(::GetWindowRect(parent, &pr));
    r.left -= pr.left;
    r.top -= pr.top;
    // adjust for chrome
    nsWindow* pWidget = static_cast<nsWindow*>(GetParent());
    if (pWidget && pWidget->IsTopLevelWidget()) {
      LayoutDeviceIntPoint clientOffset = pWidget->GetClientOffset();
      r.left -= clientOffset.x;
      r.top -= clientOffset.y;
    }
  }
  rect.MoveTo(r.left, r.top);
  if (mCompositorSession &&
      !wr::WindowSizeSanityCheck(rect.width, rect.height)) {
    gfxCriticalNoteOnce << "Invalid size" << rect << " size mode "
                        << mFrameState->GetSizeMode();
  }

  return rect;
}

LayoutDeviceIntSize nsWindow::GetSize() const {
  if (!mWnd) {
    return mBounds.Size();
  }
  RECT r;
  VERIFY(::GetWindowRect(mWnd, &r));
  return {r.right - r.left, r.bottom - r.top};
}

// Get this component dimension
LayoutDeviceIntRect nsWindow::GetClientBounds() {
  if (!mWnd) {
    return LayoutDeviceIntRect(0, 0, 0, 0);
  }

  RECT r;
  if (!::GetClientRect(mWnd, &r)) {
    MOZ_ASSERT_UNREACHABLE("unexpected to be called");
    gfxCriticalNoteOnce << "GetClientRect failed " << ::GetLastError();
    return mBounds;
  }

  LayoutDeviceIntRect bounds = GetBounds();
  LayoutDeviceIntRect rect;
  rect.MoveTo(bounds.TopLeft() + GetClientOffset());
  rect.SizeTo(r.right - r.left, r.bottom - r.top);
  return rect;
}

// Like GetBounds, but don't offset by the parent
LayoutDeviceIntRect nsWindow::GetScreenBounds() {
  if (!mWnd) {
    return mBounds;
  }

  RECT r;
  VERIFY(::GetWindowRect(mWnd, &r));

  return LayoutDeviceIntRect(r.left, r.top, r.right - r.left, r.bottom - r.top);
}

nsresult nsWindow::GetRestoredBounds(LayoutDeviceIntRect& aRect) {
  if (SizeMode() == nsSizeMode_Normal) {
    aRect = GetScreenBounds();
    return NS_OK;
  }
  if (!mWnd) {
    return NS_ERROR_FAILURE;
  }

  WINDOWPLACEMENT pl = {sizeof(WINDOWPLACEMENT)};
  VERIFY(::GetWindowPlacement(mWnd, &pl));
  const RECT& r = pl.rcNormalPosition;

  HMONITOR monitor = ::MonitorFromWindow(mWnd, MONITOR_DEFAULTTONULL);
  if (!monitor) {
    return NS_ERROR_FAILURE;
  }
  MONITORINFO mi = {sizeof(MONITORINFO)};
  VERIFY(::GetMonitorInfo(monitor, &mi));

  aRect.SetRect(r.left, r.top, r.right - r.left, r.bottom - r.top);
  aRect.MoveBy(mi.rcWork.left - mi.rcMonitor.left,
               mi.rcWork.top - mi.rcMonitor.top);
  return NS_OK;
}

// Return the x,y offset of the client area from the origin of the window. If
// the window is borderless returns (0,0).
LayoutDeviceIntPoint nsWindow::GetClientOffset() {
  if (!mWnd) {
    return LayoutDeviceIntPoint(0, 0);
  }

  RECT r1;
  GetWindowRect(mWnd, &r1);
  LayoutDeviceIntPoint pt = WidgetToScreenOffset();
  return LayoutDeviceIntPoint(pt.x - LayoutDeviceIntCoord(r1.left),
                              pt.y - LayoutDeviceIntCoord(r1.top));
}

void nsWindow::ResetLayout() {
  // This will trigger a frame changed event, triggering
  // nc calc size and a sizemode gecko event.
  SetWindowPos(mWnd, 0, 0, 0, 0, 0,
               SWP_FRAMECHANGED | SWP_NOACTIVATE | SWP_NOMOVE |
                   SWP_NOOWNERZORDER | SWP_NOSIZE | SWP_NOZORDER);

  // If hidden, just send the frame changed event for now.
  if (!mIsVisible) {
    return;
  }

  // Send a gecko size event to trigger reflow.
  RECT clientRc = {0};
  GetClientRect(mWnd, &clientRc);
  OnResize(WinUtils::ToIntRect(clientRc).Size());

  // Invalidate and update
  Invalidate();
}

#define DWMWA_USE_IMMERSIVE_DARK_MODE_BEFORE_20H1 19
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20

void nsWindow::SetColorScheme(const Maybe<ColorScheme>& aScheme) {
  BOOL dark =
      aScheme.valueOrFrom(LookAndFeel::SystemColorScheme) == ColorScheme::Dark;
  DwmSetWindowAttribute(mWnd, DWMWA_USE_IMMERSIVE_DARK_MODE_BEFORE_20H1, &dark,
                        sizeof dark);
  DwmSetWindowAttribute(mWnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark,
                        sizeof dark);
}

void nsWindow::SetMicaBackdrop(bool aEnabled) {
  if (aEnabled == mMicaBackdrop) {
    return;
  }

  mMicaBackdrop = aEnabled;
  UpdateMicaBackdrop();
}

void nsWindow::UpdateMicaBackdrop(bool aForce) {
  const bool micaEnabled =
      IsPopup() ? WinUtils::MicaPopupsEnabled() : WinUtils::MicaEnabled();
  if (!micaEnabled && !aForce) {
    return;
  }

  const bool useBackdrop = mMicaBackdrop && micaEnabled;

  const DWM_SYSTEMBACKDROP_TYPE backdrop = [&] {
    if (!useBackdrop) {
      return DWMSBT_AUTO;
    }
    if (IsPopup()) {
      return DWMSBT_TRANSIENTWINDOW;
    }
    switch (StaticPrefs::widget_windows_mica_toplevel_backdrop()) {
      case 1:
        return DWMSBT_MAINWINDOW;
      case 2:
        return DWMSBT_TRANSIENTWINDOW;
      case 3:
      default:
        return DWMSBT_TABBEDWINDOW;
    }
  }();
  ::DwmSetWindowAttribute(mWnd, DWMWA_SYSTEMBACKDROP_TYPE, &backdrop,
                          sizeof backdrop);
  if (IsPopup()) {
    // For popups, we need a couple extra tweaks:
    //  * We want the native rounded corners and borders (as otherwise we can't
    //    clip the backdrop).
    //  * We want to draw as if the window was active all the time (as
    //    otherwise it'd draw the inactive window backdrop rather than
    //    acrylic). See also the WM_NCACTIVATE implementation.
    const DWM_WINDOW_CORNER_PREFERENCE corner =
        useBackdrop ? DWMWCP_ROUND : DWMWCP_DEFAULT;
    ::DwmSetWindowAttribute(mWnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner,
                            sizeof corner);
    ::PostMessageW(mWnd, WM_NCACTIVATE, TRUE, -1);
  }
}

LayoutDeviceIntMargin nsWindow::NormalWindowNonClientOffset() const {
  MOZ_ASSERT(mCustomNonClient);
  // We're dealing with a "normal" window (not maximized, minimized, or
  // fullscreen), so set `mNonClientOffset` accordingly.
  //
  // Setting `mNonClientOffset` to 0 has the effect of leaving the default
  // frame intact.  Setting it to a value greater than 0 reduces the frame
  // size by that amount.
  //
  // When using custom titlebar, we hide the titlebar and leave the default
  // frame on the other sides.
  return LayoutDeviceIntMargin(mCustomNonClientMetrics.DefaultMargins().top, 0,
                               0, 0);
}

/**
 * Called when the window layout changes: full screen mode transitions,
 * theme changes, and composition changes. Calculates the new non-client
 * margins and fires off a frame changed event, which triggers an nc calc
 * size windows event, kicking the changes in.
 *
 * This function calculates and populates `mNonClientOffset`.
 * In our processing of `WM_NCCALCSIZE`, the frame size will be calculated
 * as (default frame size - offset).  For example, if the left frame should
 * be 1 pixel narrower than the default frame size, `mNonClientOffset.left`
 * will equal 1.
 *
 * For maximized, fullscreen, and minimized windows special processing takes
 * place.
 */
bool nsWindow::UpdateNonClientMargins(bool aReflowWindow) {
  if (!mCustomNonClient) {
    return false;
  }

  const nsSizeMode sizeMode = mFrameState->GetSizeMode();

  const bool hasCaption =
      bool(mBorderStyle & (BorderStyle::All | BorderStyle::Title |
                           BorderStyle::Menu | BorderStyle::Default));

  float dpi = GetDPI();

  auto& metrics = mCustomNonClientMetrics;

  // mHorResizeMargin is the size of the default NC areas on the
  // left and right sides of our window.  It is calculated as
  // the sum of:
  //      SM_CXFRAME        - The thickness of the sizing border
  //      SM_CXPADDEDBORDER - The amount of border padding
  //                          for captioned windows
  //
  // If the window does not have a caption, mHorResizeMargin will be equal to
  // `WinUtils::GetSystemMetricsForDpi(SM_CXFRAME, dpi)`
  metrics.mHorResizeMargin =
      WinUtils::GetSystemMetricsForDpi(SM_CXFRAME, dpi) +
      (hasCaption ? WinUtils::GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi)
                  : 0);

  // mVertResizeMargin is the size of the default NC area at the
  // bottom of the window. It is calculated as the sum of:
  //      SM_CYFRAME        - The thickness of the sizing border
  //      SM_CXPADDEDBORDER - The amount of border padding
  //                          for captioned windows.
  //
  // If the window does not have a caption, mVertResizeMargin will be equal to
  // `WinUtils::GetSystemMetricsForDpi(SM_CYFRAME, dpi)`
  metrics.mVertResizeMargin =
      WinUtils::GetSystemMetricsForDpi(SM_CYFRAME, dpi) +
      (hasCaption ? WinUtils::GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi)
                  : 0);

  // mCaptionHeight is the default size of the caption. You need to include
  // mVertResizeMargin if you want the whole size of the default NC area at the
  // top of the window.
  metrics.mCaptionHeight =
      hasCaption ? WinUtils::GetSystemMetricsForDpi(SM_CYCAPTION, dpi) : 0;

  metrics.mOffset = {};
  if (sizeMode == nsSizeMode_Minimized) {
    // Use default frame size for minimized windows (so, do nothing).
  } else if (sizeMode == nsSizeMode_Fullscreen) {
    // Remove the default frame from the top of our fullscreen window.  This
    // makes the whole caption part of our client area, allowing us to draw
    // in the whole caption area.  Additionally remove the default frame from
    // the left, right, and bottom.
    //
    // NOTE(emilio): Fullscreen windows have completely different window styles
    // because of HideWindowChrome(), so we actually need to apply the offsets
    // and extend into the frame. It might be worth investigating if we can
    // make fullscreen work without messing with window styles (like
    // maximized windows work).
    metrics.mOffset = metrics.DefaultMargins();
  } else if (sizeMode == nsSizeMode_Maximized) {
    // We make the entire frame part of the client area. We leave the default
    // frame sizes for left, right and bottom since Windows will automagically
    // position the edges "offscreen" for maximized windows.
    metrics.mOffset.top = metrics.mCaptionHeight;
  } else if (mPIPWindow &&
             !StaticPrefs::widget_windows_pip_decorations_enabled()) {
    metrics.mOffset = metrics.DefaultMargins();
  } else {
    metrics.mOffset = NormalWindowNonClientOffset();
  }

  UpdateOpaqueRegionInternal();
  if (aReflowWindow) {
    // Force a reflow of content based on the new client
    // dimensions.
    ResetLayout();
  }

  return true;
}

void nsWindow::SetCustomTitlebar(bool aCustomTitlebar) {
  if (!IsTopLevelWidget() || mBorderStyle == BorderStyle::None) {
    return;
  }

  if (mCustomNonClient == aCustomTitlebar) {
    return;
  }

  if (mHideChrome) {
    mCustomTitlebarOnceChromeShows = Some(aCustomTitlebar);
    return;
  }

  mCustomTitlebarOnceChromeShows.reset();

  mCustomNonClient = aCustomTitlebar;

  const LONG_PTR style = GetWindowLongPtrW(mWnd, GWL_STYLE);
  // Force a reflow of content based on the new client dimensions.
  if (mCustomNonClient) {
    if (style & WS_SYSMENU) {
      // Remove the WS_SYSMENU style, so that DWM doesn't draw the caption
      // buttons. Note that we still need WS_MAXIMIZEBOX at least to
      // support Snap Layouts / Aero Snap.
      //
      // This behavior is not documented: per MSDN, WS_MAXIMIZEBOX simply
      // requires WS_SYSMENU, and is not valid without it. However, omitting it
      // doesn't seem to have negative side-effects on any version of Windows
      // tested (other than losing the default system menu handling, which we
      // implement ourselves in DisplaySystemMenu()).
      //
      // Since the system menu is lazily initialized (see [1]), we have to call
      // GetSystemMenu() here in order to get it created before it is too late.
      // An alternative would be to play with window styles later to force it
      // to be created, but that seems a bit more finicky.
      //
      // [1]: https://devblogs.microsoft.com/oldnewthing/20100528-00/?p=13893
      ::GetSystemMenu(mWnd, FALSE);
      ::SetWindowLongPtrW(mWnd, GWL_STYLE, style & ~WS_SYSMENU);
    }
    UpdateNonClientMargins();
  } else {
    if (WindowStyle() & WS_SYSMENU) {
      // Restore the WS_SYSMENU style if appropriate.
      ::SetWindowLongPtrW(mWnd, GWL_STYLE, style | WS_SYSMENU);
      // Reset the small icon as a workaround for a dwm bug, see bug 1935542.
      HICON icon =
          (HICON)::SendMessageW(mWnd, WM_SETICON, (WPARAM)ICON_SMALL, 0);
      ::SendMessageW(mWnd, WM_SETICON, (WPARAM)ICON_SMALL, (LPARAM)icon);
    }
    mCustomNonClientMetrics = {};
    ResetLayout();
  }
}

void nsWindow::SetResizeMargin(mozilla::LayoutDeviceIntCoord aResizeMargin) {
  mCustomResizeMargin = aResizeMargin;
}

/**************************************************************
 *
 * SECTION: nsIWidget::SetCursor
 *
 * SetCursor and related utilities for manging cursor state.
 *
 **************************************************************/

// Set this component cursor
static HCURSOR CursorFor(nsCursor aCursor) {
  switch (aCursor) {
    case eCursor_select:
      return ::LoadCursor(nullptr, IDC_IBEAM);
    case eCursor_wait:
      return ::LoadCursor(nullptr, IDC_WAIT);
    case eCursor_hyperlink:
      return ::LoadCursor(nullptr, IDC_HAND);
    case eCursor_standard:
    case eCursor_context_menu:  // XXX See bug 258960.
      return ::LoadCursor(nullptr, IDC_ARROW);

    case eCursor_n_resize:
    case eCursor_s_resize:
      return ::LoadCursor(nullptr, IDC_SIZENS);

    case eCursor_w_resize:
    case eCursor_e_resize:
      return ::LoadCursor(nullptr, IDC_SIZEWE);

    case eCursor_nw_resize:
    case eCursor_se_resize:
      return ::LoadCursor(nullptr, IDC_SIZENWSE);

    case eCursor_ne_resize:
    case eCursor_sw_resize:
      return ::LoadCursor(nullptr, IDC_SIZENESW);

    case eCursor_crosshair:
      return ::LoadCursor(nullptr, IDC_CROSS);

    case eCursor_move:
      return ::LoadCursor(nullptr, IDC_SIZEALL);

    case eCursor_help:
      return ::LoadCursor(nullptr, IDC_HELP);

    case eCursor_copy:  // CSS3
      return ::LoadCursor(nsToolkit::mDllInstance, MAKEINTRESOURCE(IDC_COPY));

    case eCursor_alias:
      return ::LoadCursor(nsToolkit::mDllInstance, MAKEINTRESOURCE(IDC_ALIAS));

    case eCursor_cell:
      return ::LoadCursor(nsToolkit::mDllInstance, MAKEINTRESOURCE(IDC_CELL));
    case eCursor_grab:
      return ::LoadCursor(nsToolkit::mDllInstance, MAKEINTRESOURCE(IDC_GRAB));

    case eCursor_grabbing:
      return ::LoadCursor(nsToolkit::mDllInstance,
                          MAKEINTRESOURCE(IDC_GRABBING));

    case eCursor_spinning:
      return ::LoadCursor(nullptr, IDC_APPSTARTING);

    case eCursor_zoom_in:
      return ::LoadCursor(nsToolkit::mDllInstance, MAKEINTRESOURCE(IDC_ZOOMIN));

    case eCursor_zoom_out:
      return ::LoadCursor(nsToolkit::mDllInstance,
                          MAKEINTRESOURCE(IDC_ZOOMOUT));

    case eCursor_not_allowed:
    case eCursor_no_drop:
      return ::LoadCursor(nullptr, IDC_NO);

    case eCursor_col_resize:
      return ::LoadCursor(nsToolkit::mDllInstance,
                          MAKEINTRESOURCE(IDC_COLRESIZE));

    case eCursor_row_resize:
      return ::LoadCursor(nsToolkit::mDllInstance,
                          MAKEINTRESOURCE(IDC_ROWRESIZE));

    case eCursor_vertical_text:
      return ::LoadCursor(nsToolkit::mDllInstance,
                          MAKEINTRESOURCE(IDC_VERTICALTEXT));

    case eCursor_all_scroll:
      // XXX not 100% appropriate perhaps
      return ::LoadCursor(nullptr, IDC_SIZEALL);

    case eCursor_nesw_resize:
      return ::LoadCursor(nullptr, IDC_SIZENESW);

    case eCursor_nwse_resize:
      return ::LoadCursor(nullptr, IDC_SIZENWSE);

    case eCursor_ns_resize:
      return ::LoadCursor(nullptr, IDC_SIZENS);

    case eCursor_ew_resize:
      return ::LoadCursor(nullptr, IDC_SIZEWE);

    case eCursor_none:
      return ::LoadCursor(nsToolkit::mDllInstance, MAKEINTRESOURCE(IDC_NONE));

    default:
      NS_ERROR("Invalid cursor type");
      return nullptr;
  }
}

static HCURSOR CursorForImage(const nsIWidget::Cursor& aCursor,
                              CSSToLayoutDeviceScale aScale) {
  if (!aCursor.IsCustom()) {
    return nullptr;
  }

  nsIntSize size = nsIWidget::CustomCursorSize(aCursor);

  // Reject cursors greater than 128 pixels in either direction, to prevent
  // spoofing.
  // XXX ideally we should rescale. Also, we could modify the API to
  // allow trusted content to set larger cursors.
  if (size.width > 128 || size.height > 128) {
    return nullptr;
  }

  LayoutDeviceIntSize layoutSize =
      RoundedToInt(CSSIntSize(size.width, size.height) * aScale);
  LayoutDeviceIntPoint hotspot =
      RoundedToInt(CSSIntPoint(aCursor.mHotspotX, aCursor.mHotspotY) * aScale);
  HCURSOR cursor;
  nsresult rv = nsWindowGfx::CreateIcon(aCursor.mContainer, nullptr, true,
                                        hotspot, layoutSize, &cursor);
  if (NS_FAILED(rv)) {
    return nullptr;
  }

  return cursor;
}

void nsWindow::SetCursor(const Cursor& aCursor) {
  static HCURSOR sCurrentHCursor = nullptr;
  static bool sCurrentHCursorIsCustom = false;

  mCursor = aCursor;

  if (sCurrentCursor == aCursor && sCurrentHCursor && !mUpdateCursor) {
    // Cursors in windows are global, so even if our mUpdateCursor flag is
    // false we always need to make sure the Windows cursor is up-to-date,
    // since stuff like native drag and drop / resizers code can mutate it
    // outside of this method.
    ::SetCursor(sCurrentHCursor);
    return;
  }

  mUpdateCursor = false;

  if (sCurrentHCursorIsCustom) {
    ::DestroyIcon(sCurrentHCursor);
  }
  sCurrentHCursor = nullptr;
  sCurrentHCursorIsCustom = false;
  sCurrentCursor = aCursor;

  HCURSOR cursor = nullptr;
  if (mCustomCursorAllowed) {
    cursor = CursorForImage(aCursor, GetDefaultScale());
  }
  bool custom = false;
  if (cursor) {
    custom = true;
  } else {
    cursor = CursorFor(aCursor.mDefaultCursor);
  }

  if (!cursor) {
    return;
  }

  sCurrentHCursor = cursor;
  sCurrentHCursorIsCustom = custom;
  ::SetCursor(cursor);
}

/**************************************************************
 *
 * SECTION: nsIWidget::UpdateWindowDraggingRegion
 *
 * For setting the draggable titlebar region from CSS
 * with -moz-window-dragging: drag.
 *
 **************************************************************/

void nsWindow::UpdateWindowDraggingRegion(
    const LayoutDeviceIntRegion& aRegion) {
  mDraggableRegion = aRegion;
}

/**************************************************************
 *
 * SECTION: nsIWidget::HideWindowChrome
 *
 * Show or hide window chrome.
 *
 **************************************************************/

void nsWindow::HideWindowChrome(bool aShouldHide) {
  HWND hwnd = WinUtils::GetTopLevelHWND(mWnd, true);
  if (!WinUtils::GetNSWindowPtr(hwnd)) {
    NS_WARNING("Trying to hide window decorations in an embedded context");
    return;
  }

  if (mHideChrome == aShouldHide) {
    return;
  }

  // The desired style-flagset for fullscreen windows. (This happens to be all
  // zeroes, but we don't need to rely on that.)
  constexpr static const WindowStyles kFullscreenChromeStyles{.style = 0,
                                                              .ex = 0};

  auto const [chromeless, currentChrome] =
      kChromeStylesMask.split(Styles::FromHWND(hwnd));
  Styles newChrome{}, oldChrome{};

  mHideChrome = aShouldHide;
  if (aShouldHide) {
    newChrome = kFullscreenChromeStyles;
    oldChrome = currentChrome;
  } else {
    // if there's nothing to "restore" it to, just use what's there now
    oldChrome = mOldStyles.refOr(currentChrome);
    newChrome = oldChrome;
    if (mCustomTitlebarOnceChromeShows) {
      SetCustomTitlebar(mCustomTitlebarOnceChromeShows.extract());
      MOZ_ASSERT(!mCustomTitlebarOnceChromeShows);
    }
  }

  mOldStyles = Some(oldChrome);
  SetWindowStyles(hwnd, kChromeStylesMask.merge(chromeless, newChrome));
}

/**************************************************************
 *
 * SECTION: nsWindow::Invalidate
 *
 * Invalidate an area of the client for painting.
 *
 **************************************************************/

// Invalidate this component visible area
void nsWindow::Invalidate(bool aEraseBackground, bool aUpdateNCArea,
                          bool aIncludeChildren) {
  if (!mWnd) {
    return;
  }

#ifdef WIDGET_DEBUG_OUTPUT
  debug_DumpInvalidate(stdout, this, nullptr, "noname", (int32_t)mWnd);
#endif  // WIDGET_DEBUG_OUTPUT

  DWORD flags = RDW_INVALIDATE;
  if (aEraseBackground) {
    flags |= RDW_ERASE;
  }
  if (aUpdateNCArea) {
    flags |= RDW_FRAME;
  }
  if (aIncludeChildren) {
    flags |= RDW_ALLCHILDREN;
  }

  VERIFY(::RedrawWindow(mWnd, nullptr, nullptr, flags));
}

// Invalidate this component visible area
void nsWindow::Invalidate(const LayoutDeviceIntRect& aRect) {
  if (mWnd) {
#ifdef WIDGET_DEBUG_OUTPUT
    debug_DumpInvalidate(stdout, this, &aRect, "noname", (int32_t)mWnd);
#endif  // WIDGET_DEBUG_OUTPUT

    RECT rect;

    rect.left = aRect.X();
    rect.top = aRect.Y();
    rect.right = aRect.XMost();
    rect.bottom = aRect.YMost();

    VERIFY(::InvalidateRect(mWnd, &rect, FALSE));
  }
}

static LRESULT CALLBACK FullscreenTransitionWindowProc(HWND hWnd, UINT uMsg,
                                                       WPARAM wParam,
                                                       LPARAM lParam) {
  switch (uMsg) {
    case WM_FULLSCREEN_TRANSITION_BEFORE:
    case WM_FULLSCREEN_TRANSITION_AFTER: {
      DWORD duration = (DWORD)lParam;
      DWORD flags = AW_BLEND;
      if (uMsg == WM_FULLSCREEN_TRANSITION_AFTER) {
        flags |= AW_HIDE;
      }
      ::AnimateWindow(hWnd, duration, flags);
      // The message sender should have added ref for us.
      NS_DispatchToMainThread(
          already_AddRefed<nsIRunnable>((nsIRunnable*)wParam));
      break;
    }
    case WM_DESTROY:
      ::PostQuitMessage(0);
      break;
    default:
      return ::DefWindowProcW(hWnd, uMsg, wParam, lParam);
  }
  return 0;
}

struct FullscreenTransitionInitData {
  LayoutDeviceIntRect mBounds;
  HANDLE mSemaphore;
  HANDLE mThread;
  HWND mWnd;

  FullscreenTransitionInitData()
      : mSemaphore(nullptr), mThread(nullptr), mWnd(nullptr) {}

  ~FullscreenTransitionInitData() {
    if (mSemaphore) {
      ::CloseHandle(mSemaphore);
    }
    if (mThread) {
      ::CloseHandle(mThread);
    }
  }
};

static DWORD WINAPI FullscreenTransitionThreadProc(LPVOID lpParam) {
  // Initialize window class
  static bool sInitialized = false;
  if (!sInitialized) {
    WNDCLASSW wc = {};
    wc.lpfnWndProc = ::FullscreenTransitionWindowProc;
    wc.hInstance = nsToolkit::mDllInstance;
    wc.hbrBackground = ::CreateSolidBrush(RGB(0, 0, 0));
    wc.lpszClassName = kClassNameTransition;
    ::RegisterClassW(&wc);
    sInitialized = true;
  }

  auto data = static_cast<FullscreenTransitionInitData*>(lpParam);
  HWND wnd = ::CreateWindowW(kClassNameTransition, L"", 0, 0, 0, 0, 0, nullptr,
                             nullptr, nsToolkit::mDllInstance, nullptr);
  if (!wnd) {
    ::ReleaseSemaphore(data->mSemaphore, 1, nullptr);
    return 0;
  }

  // Since AnimateWindow blocks the thread of the transition window,
  // we need to hide the cursor for that window, otherwise the system
  // would show the busy pointer to the user.
  ::ShowCursor(false);
  ::SetWindowLongW(wnd, GWL_STYLE, 0);
  ::SetWindowLongW(
      wnd, GWL_EXSTYLE,
      WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE);
  ::SetWindowPos(wnd, HWND_TOPMOST, data->mBounds.X(), data->mBounds.Y(),
                 data->mBounds.Width(), data->mBounds.Height(), 0);
  data->mWnd = wnd;
  ::ReleaseSemaphore(data->mSemaphore, 1, nullptr);
  // The initialization data may no longer be valid
  // after we release the semaphore.
  data = nullptr;

  MSG msg;
  while (::GetMessageW(&msg, nullptr, 0, 0)) {
    ::TranslateMessage(&msg);
    ::DispatchMessage(&msg);
  }
  ::ShowCursor(true);
  ::DestroyWindow(wnd);
  return 0;
}

class FullscreenTransitionData final : public nsISupports {
 public:
  NS_DECL_ISUPPORTS

  explicit FullscreenTransitionData(HWND aWnd) : mWnd(aWnd) {
    MOZ_ASSERT(NS_IsMainThread(),
               "FullscreenTransitionData "
               "should be constructed in the main thread");
  }

  const HWND mWnd;

 private:
  ~FullscreenTransitionData() {
    MOZ_ASSERT(NS_IsMainThread(),
               "FullscreenTransitionData "
               "should be deconstructed in the main thread");
    ::PostMessageW(mWnd, WM_DESTROY, 0, 0);
  }
};

NS_IMPL_ISUPPORTS0(FullscreenTransitionData)

/* virtual */
bool nsWindow::PrepareForFullscreenTransition(nsISupports** aData) {
  FullscreenTransitionInitData initData;
  nsCOMPtr<nsIScreen> screen = GetWidgetScreen();
  const DesktopIntRect rect = screen->GetRectDisplayPix();
  MOZ_ASSERT(BoundsUseDesktopPixels(),
             "Should only be called on top-level window");
  initData.mBounds =
      LayoutDeviceIntRect::Round(rect * GetDesktopToDeviceScale());

  // Create a semaphore for synchronizing the window handle which will
  // be created by the transition thread and used by the main thread for
  // posting the transition messages.
  initData.mSemaphore = ::CreateSemaphore(nullptr, 0, 1, nullptr);
  if (initData.mSemaphore) {
    initData.mThread = ::CreateThread(
        nullptr, 0, FullscreenTransitionThreadProc, &initData, 0, nullptr);
    if (initData.mThread) {
      ::WaitForSingleObject(initData.mSemaphore, INFINITE);
    }
  }
  if (!initData.mWnd) {
    return false;
  }

  mTransitionWnd = initData.mWnd;

  auto data = new FullscreenTransitionData(initData.mWnd);
  *aData = data;
  NS_ADDREF(data);
  return true;
}

/* virtual */
void nsWindow::PerformFullscreenTransition(FullscreenTransitionStage aStage,
                                           uint16_t aDuration,
                                           nsISupports* aData,
                                           nsIRunnable* aCallback) {
  auto data = static_cast<FullscreenTransitionData*>(aData);
  nsCOMPtr<nsIRunnable> callback = aCallback;
  UINT msg = aStage == eBeforeFullscreenToggle ? WM_FULLSCREEN_TRANSITION_BEFORE
                                               : WM_FULLSCREEN_TRANSITION_AFTER;
  WPARAM wparam = (WPARAM)callback.forget().take();
  ::PostMessage(data->mWnd, msg, wparam, (LPARAM)aDuration);
}

/* virtual */
void nsWindow::CleanupFullscreenTransition() {
  MOZ_ASSERT(NS_IsMainThread(),
             "CleanupFullscreenTransition "
             "should only run on the main thread");

  mTransitionWnd = nullptr;
}

void nsWindow::TryDwmResizeHack() {
  // The "DWM resize hack", aka the "fullscreen resize hack", is a workaround
  // for DWM's occasional and not-entirely-predictable failure to update its
  // internal state when the client area of a window changes without changing
  // the window size. The effect of this is that DWM will clip the content of
  // the window to its former client area.
  //
  // It is not known under what circumstances the bug will trigger. Windows 11
  // is known to be required, but many Windows 11 machines do not exhibit the
  // issue. Even machines that _do_ exhibit it will sometimes not do so when
  // apparently-irrelevant changes are made to the configuration. (See bug
  // 1763981.)
  //
  // The bug is triggered by Firefox when a maximized window (which has window
  // decorations) becomes fullscreen (which doesn't). To work around this, if we
  // think it may occur, we "flicker-resize" the relevant window -- that is, we
  // reduce its height by 1px, then restore it. This causes DWM to acquire the
  // new client-area metrics.
  //
  // Note that, in particular, this bug will not occur when using a separate
  // compositor window, as our compositor windows never have any nonclient area.
  //
  // This is admittedly a sledgehammer where a screwdriver should suffice.

  // ---------------------------------------------------------------------------

  // Regardless of preferences or heuristics, only apply the hack if this is the
  // first time we've entered fullscreen across the entire Firefox session.
  // (Subsequent transitions to fullscreen, even with different windows, don't
  // appear to induce the bug.)
  {
    // (main thread only; `atomic` not needed)
    static bool sIsFirstFullscreenEntry = true;
    bool isFirstFullscreenEntry = sIsFirstFullscreenEntry;
    sIsFirstFullscreenEntry = false;
    if (MOZ_LIKELY(!isFirstFullscreenEntry)) {
      return;
    }
    MOZ_LOG(gWindowsLog, LogLevel::Verbose,
            ("%s: first fullscreen entry", __PRETTY_FUNCTION__));
  }

  // Check whether to try to apply the DWM resize hack, based on the override
  // pref and/or some internal heuristics.
  {
    const auto hackApplicationHeuristics = [&]() -> bool {
      // The bug has only been seen under Windows 11. (At time of writing, this
      // is the latest version of Windows.)
      if (!IsWin11OrLater()) {
        return false;
      }

      KnowsCompositor const* const kc = mWindowRenderer->AsKnowsCompositor();
      // This should never happen...
      MOZ_ASSERT(kc);
      // ... so if it does, we are in uncharted territory: don't apply the hack.
      if (!kc) {
        return false;
      }

      // The bug doesn't occur when we're using a separate compositor window
      // (since the compositor window always comprises exactly its client area,
      // with no non-client border).
      if (kc->GetUseCompositorWnd()) {
        return false;
      }

      // Otherwise, apply the hack.
      return true;
    };

    // Figure out whether or not we should perform the hack, and -- arguably
    // more importantly -- log that decision.
    bool const shouldApplyHack = [&]() {
      enum Reason : bool { Pref, Heuristics };
      auto const msg = [&](bool decision, Reason reason) -> bool {
        MOZ_LOG(gWindowsLog, LogLevel::Verbose,
                ("%s %s per %s", decision ? "applying" : "skipping",
                 "DWM resize hack", reason == Pref ? "pref" : "heuristics"));
        return decision;
      };
      switch (StaticPrefs::widget_windows_apply_dwm_resize_hack()) {
        case 0:
          return msg(false, Pref);
        case 1:
          return msg(true, Pref);
        default:  // treat all other values as `auto`
          return msg(hackApplicationHeuristics(), Heuristics);
      }
    }();

    if (!shouldApplyHack) {
      return;
    }
  }

  // The DWM bug is believed to involve a race condition: some users have
  // reported that setting a custom theme or adding unused command-line
  // parameters sometimes causes the bug to vanish.
  //
  // Out of an abundance of caution, we therefore apply the hack in a later
  // event, rather than inline.
  NS_DispatchToMainThread(NS_NewRunnableFunction(
      "nsWindow::TryFullscreenResizeHack", [self = RefPtr(this)]() {
        HWND const hwnd = self->GetWindowHandle();

        if (self->mFrameState->GetSizeMode() != nsSizeMode_Fullscreen) {
          MOZ_LOG(gWindowsLog, mozilla::LogLevel::Info,
                  ("DWM resize hack: window no longer fullscreen; aborting"));
          return;
        }

        RECT origRect;
        if (!::GetWindowRect(hwnd, &origRect)) {
          MOZ_LOG(gWindowsLog, mozilla::LogLevel::Error,
                  ("DWM resize hack: could not get window size?!"));
          return;
        }
        LONG const x = origRect.left;
        LONG const y = origRect.top;
        LONG const width = origRect.right - origRect.left;
        LONG const height = origRect.bottom - origRect.top;

        MOZ_DIAGNOSTIC_ASSERT(!self->mIsPerformingDwmFlushHack);
        auto const onExit =
            MakeScopeExit([&, oldVal = self->mIsPerformingDwmFlushHack]() {
              self->mIsPerformingDwmFlushHack = oldVal;
            });
        self->mIsPerformingDwmFlushHack = true;

        MOZ_LOG(gWindowsLog, LogLevel::Debug,
                ("beginning DWM resize hack for HWND %08" PRIXPTR,
                 uintptr_t(hwnd)));
        ::MoveWindow(hwnd, x, y, width, height - 1, FALSE);
        ::MoveWindow(hwnd, x, y, width, height, TRUE);
        MOZ_LOG(gWindowsLog, LogLevel::Debug,
                ("concluded DWM resize hack for HWND %08" PRIXPTR,
                 uintptr_t(hwnd)));
      }));
}

void nsWindow::OnFullscreenChanged(nsSizeMode aOldSizeMode, bool aFullScreen) {
  MOZ_ASSERT((aOldSizeMode != nsSizeMode_Fullscreen) == aFullScreen);

  // HACK: Potentially flicker-resize the window, to force DWM to get the right
  // client-area information.
  if (aFullScreen) {
    TryDwmResizeHack();
  }

  // Hide chrome and reposition window. Note this will also cache dimensions for
  // restoration, so it should only be called once per fullscreen request.
  //
  // Don't do this when minimized, since our bounds make no sense then, nor when
  // coming back from that state.
  const bool toOrFromMinimized =
      mFrameState->GetSizeMode() == nsSizeMode_Minimized ||
      aOldSizeMode == nsSizeMode_Minimized;
  if (!toOrFromMinimized) {
    InfallibleMakeFullScreen(aFullScreen);
  }

  // Possibly notify the taskbar that we have changed our fullscreen mode.
  TaskbarConcealer::OnFullscreenChanged(this, aFullScreen);
}

nsresult nsWindow::MakeFullScreen(bool aFullScreen) {
  mFrameState->EnsureFullscreenMode(aFullScreen);
  return NS_OK;
}

/**************************************************************
 *
 * SECTION: Native data storage
 *
 * nsIWidget::GetNativeData
 * nsIWidget::FreeNativeData
 *
 * Set or clear native data based on a constant.
 *
 **************************************************************/

// Return some native data according to aDataType
void* nsWindow::GetNativeData(uint32_t aDataType) {
  switch (aDataType) {
    case NS_NATIVE_WIDGET:
    case NS_NATIVE_WINDOW:
    case NS_NATIVE_WINDOW_WEBRTC_DEVICE_ID:
      return (void*)mWnd;
    case NS_NATIVE_GRAPHIC:
      MOZ_ASSERT_UNREACHABLE("Not supported on Windows:");
      return nullptr;
    case NS_RAW_NATIVE_IME_CONTEXT: {
      void* pseudoIMEContext = GetPseudoIMEContext();
      if (pseudoIMEContext) {
        return pseudoIMEContext;
      }
      return IMEHandler::GetNativeData(this, aDataType);
    }
    default:
      break;
  }

  return nullptr;
}

// Free some native data according to aDataType
void nsWindow::FreeNativeData(void* data, uint32_t aDataType) {
  switch (aDataType) {
    case NS_NATIVE_GRAPHIC:
    case NS_NATIVE_WIDGET:
    case NS_NATIVE_WINDOW:
      break;
    default:
      break;
  }
}

/**************************************************************
 *
 * SECTION: nsIWidget::SetTitle
 *
 * Set the main windows title text.
 *
 **************************************************************/

nsresult nsWindow::SetTitle(const nsAString& aTitle) {
  const nsString& strTitle = PromiseFlatString(aTitle);
  AutoRestore<bool> sendingText(mSendingSetText);
  mSendingSetText = true;
  ::SendMessageW(mWnd, WM_SETTEXT, (WPARAM)0, (LPARAM)(LPCWSTR)strTitle.get());
  return NS_OK;
}

/**************************************************************
 *
 * SECTION: nsIWidget::SetIcon
 *
 * Set the main windows icon.
 *
 **************************************************************/

void nsWindow::SetBigIcon(HICON aIcon) {
  HICON icon =
      (HICON)::SendMessageW(mWnd, WM_SETICON, (WPARAM)ICON_BIG, (LPARAM)aIcon);
  if (icon) {
    ::DestroyIcon(icon);
  }

  mIconBig = aIcon;
}

void nsWindow::SetSmallIcon(HICON aIcon) {
  HICON icon = (HICON)::SendMessageW(mWnd, WM_SETICON, (WPARAM)ICON_SMALL,
                                     (LPARAM)aIcon);
  if (icon) {
    ::DestroyIcon(icon);
  }

  mIconSmall = aIcon;
}

void nsWindow::SetIcon(const nsAString& aIconSpec) {
  // Assume the given string is a local identifier for an icon file.

  nsCOMPtr<nsIFile> iconFile;
  ResolveIconName(aIconSpec, u".ico"_ns, getter_AddRefs(iconFile));
  if (!iconFile) return;

  nsAutoString iconPath;
  iconFile->GetPath(iconPath);

  // XXX this should use MZLU (see bug 239279)

  ::SetLastError(0);

  HICON bigIcon =
      (HICON)::LoadImageW(nullptr, (LPCWSTR)iconPath.get(), IMAGE_ICON,
                          ::GetSystemMetrics(SM_CXICON),
                          ::GetSystemMetrics(SM_CYICON), LR_LOADFROMFILE);
  HICON smallIcon =
      (HICON)::LoadImageW(nullptr, (LPCWSTR)iconPath.get(), IMAGE_ICON,
                          ::GetSystemMetrics(SM_CXSMICON),
                          ::GetSystemMetrics(SM_CYSMICON), LR_LOADFROMFILE);

  if (bigIcon) {
    SetBigIcon(bigIcon);
  }
#ifdef DEBUG_SetIcon
  else {
    NS_LossyConvertUTF16toASCII cPath(iconPath);
    MOZ_LOG(gWindowsLog, LogLevel::Info,
            ("\nIcon load error; icon=%s, rc=0x%08X\n\n", cPath.get(),
             ::GetLastError()));
  }
#endif
  if (smallIcon) {
    SetSmallIcon(smallIcon);
  }
#ifdef DEBUG_SetIcon
  else {
    NS_LossyConvertUTF16toASCII cPath(iconPath);
    MOZ_LOG(gWindowsLog, LogLevel::Info,
            ("\nSmall icon load error; icon=%s, rc=0x%08X\n\n", cPath.get(),
             ::GetLastError()));
  }
#endif
}

void nsWindow::SetBigIconNoData() {
  HICON bigIcon =
      ::LoadIconW(::GetModuleHandleW(nullptr), gStockApplicationIcon);
  SetBigIcon(bigIcon);
}

void nsWindow::SetSmallIconNoData() {
  HICON smallIcon =
      ::LoadIconW(::GetModuleHandleW(nullptr), gStockApplicationIcon);
  SetSmallIcon(smallIcon);
}

/**************************************************************
 *
 * SECTION: nsIWidget::WidgetToScreenOffset
 *
 * Return this widget's origin in screen coordinates.
 *
 **************************************************************/

LayoutDeviceIntPoint nsWindow::WidgetToScreenOffset() {
  POINT point;
  point.x = 0;
  point.y = 0;
  ::ClientToScreen(mWnd, &point);
  return LayoutDeviceIntPoint(point.x, point.y);
}

LayoutDeviceIntMargin nsWindow::NormalSizeModeClientToWindowMargin() {
  if (mWindowType == WindowType::Popup) {
    return {};
  }

  if (mCustomNonClient) {
    return NonClientSizeMargin(NormalWindowNonClientOffset());
  }

  // Just use a dummy 200x200 at (200, 200) client rect as the rect.
  RECT clientRect;
  clientRect.left = 200;
  clientRect.top = 200;
  clientRect.right = 400;
  clientRect.bottom = 400;

  auto ToRect = [](const RECT& aRect) -> LayoutDeviceIntRect {
    return {aRect.left, aRect.top, aRect.right - aRect.left,
            aRect.bottom - aRect.top};
  };

  RECT windowRect = clientRect;
  ::AdjustWindowRectEx(&windowRect, WindowStyle(), false, WindowExStyle());

  return ToRect(windowRect) - ToRect(clientRect);
}

/**************************************************************
 *
 * SECTION: nsIWidget::EnableDragDrop
 *
 * Enables/Disables drag and drop of files on this widget.
 *
 **************************************************************/

void nsWindow::EnableDragDrop(bool aEnable) {
  if (!mWnd) {
    // Return early if the window already closed
    return;
  }

  if (aEnable) {
    if (!mNativeDragTarget) {
      mNativeDragTarget = new nsNativeDragTarget(this);
      mNativeDragTarget->AddRef();
      ::RegisterDragDrop(mWnd, (LPDROPTARGET)mNativeDragTarget);
    }
  } else {
    if (mWnd && mNativeDragTarget) {
      ::RevokeDragDrop(mWnd);
      mNativeDragTarget->DragCancel();
      NS_RELEASE(mNativeDragTarget);
    }
  }
}

/**************************************************************
 *
 * SECTION: nsIWidget::CaptureMouse
 *
 * Enables/Disables system mouse capture.
 *
 **************************************************************/

void nsWindow::CaptureMouse(bool aCapture) {
  TRACKMOUSEEVENT mTrack;
  mTrack.cbSize = sizeof(TRACKMOUSEEVENT);
  mTrack.dwHoverTime = 0;
  mTrack.hwndTrack = mWnd;
  if (aCapture) {
    mTrack.dwFlags = TME_CANCEL | TME_LEAVE;
    ::SetCapture(mWnd);
  } else {
    mTrack.dwFlags = TME_LEAVE;
    ::ReleaseCapture();
  }
  sIsInMouseCapture = aCapture;
  TrackMouseEvent(&mTrack);
}

/**************************************************************
 *
 * SECTION: nsIWidget::CaptureRollupEvents
 *
 * Dealing with event rollup on destroy for popups. Enables &
 * Disables system capture of any and all events that would
 * cause a dropdown to be rolled up.
 *
 **************************************************************/

void nsWindow::CaptureRollupEvents(bool aDoCapture) {
  if (aDoCapture) {
    if (!sMsgFilterHook && !sCallProcHook && !sCallMouseHook) {
      RegisterSpecialDropdownHooks();
    }
    sProcessHook = true;
  } else {
    sProcessHook = false;
    UnregisterSpecialDropdownHooks();
  }
}

/**************************************************************
 *
 * SECTION: nsIWidget::GetAttention
 *
 * Bring this window to the user's attention.
 *
 **************************************************************/

// Draw user's attention to this window until it comes to foreground.
nsresult nsWindow::GetAttention(int32_t aCycleCount) {
  // Got window?
  if (!mWnd) return NS_ERROR_NOT_INITIALIZED;

  HWND flashWnd = WinUtils::GetTopLevelHWND(mWnd, false, false);
  HWND fgWnd = ::GetForegroundWindow();
  // Don't flash if the flash count is 0 or if the foreground window is our
  // window handle or that of our owned-most window.
  if (aCycleCount == 0 || flashWnd == fgWnd ||
      flashWnd == WinUtils::GetTopLevelHWND(fgWnd, false, false)) {
    return NS_OK;
  }

  DWORD defaultCycleCount = 0;
  ::SystemParametersInfo(SPI_GETFOREGROUNDFLASHCOUNT, 0, &defaultCycleCount, 0);

  FLASHWINFO flashInfo = {sizeof(FLASHWINFO), flashWnd, FLASHW_ALL,
                          aCycleCount > 0 ? aCycleCount : defaultCycleCount, 0};
  ::FlashWindowEx(&flashInfo);

  return NS_OK;
}

void nsWindow::StopFlashing() {
  HWND flashWnd = mWnd;
  while (HWND ownerWnd = ::GetWindow(flashWnd, GW_OWNER)) {
    flashWnd = ownerWnd;
  }

  FLASHWINFO flashInfo = {sizeof(FLASHWINFO), flashWnd, FLASHW_STOP, 0, 0};
  ::FlashWindowEx(&flashInfo);
}

/**************************************************************
 *
 * SECTION: nsIWidget::HasPendingInputEvent
 *
 * Ask whether there user input events pending.  All input events are
 * included, including those not targeted at this nsIwidget instance.
 *
 **************************************************************/

bool nsWindow::HasPendingInputEvent() {
  // If there is pending input or the user is currently
  // moving the window then return true.
  // Note: When the user is moving the window WIN32 spins
  // a separate event loop and input events are not
  // reported to the application.
  if (HIWORD(GetQueueStatus(QS_INPUT))) return true;
  GUITHREADINFO guiInfo;
  guiInfo.cbSize = sizeof(GUITHREADINFO);
  if (!GetGUIThreadInfo(GetCurrentThreadId(), &guiInfo)) return false;
  return GUI_INMOVESIZE == (guiInfo.flags & GUI_INMOVESIZE);
}

/**************************************************************
 *
 * SECTION: nsIWidget::GetWindowRenderer
 *
 * Get the window renderer associated with this widget.
 *
 **************************************************************/

WindowRenderer* nsWindow::GetWindowRenderer() {
  if (mWindowRenderer) {
    return mWindowRenderer;
  }

  if (!mLocalesChangedObserver) {
    mLocalesChangedObserver = new LocalesChangedObserver(this);
  }

  // Try OMTC first.
  if (!mWindowRenderer && ShouldUseOffMainThreadCompositing()) {
    gfxWindowsPlatform::GetPlatform()->UpdateRenderMode();
    CreateCompositor();
  }

  if (!mWindowRenderer) {
    MOZ_ASSERT(!mCompositorSession && !mCompositorBridgeChild);
    MOZ_ASSERT(!mCompositorWidgetDelegate);

    // Ensure we have a widget proxy even if we're not using the compositor,
    // since all our transparent window handling lives there.
    WinCompositorWidgetInitData initData(
        reinterpret_cast<uintptr_t>(mWnd),
        reinterpret_cast<uintptr_t>(static_cast<nsIWidget*>(this)),
        mTransparencyMode);
    // If we're not using the compositor, the options don't actually matter.
    CompositorOptions options(false, false);
    mBasicLayersSurface =
        new InProcessWinCompositorWidget(initData, options, this);
    mCompositorWidgetDelegate = mBasicLayersSurface;
    mWindowRenderer = CreateFallbackRenderer();
  }

  NS_ASSERTION(mWindowRenderer, "Couldn't provide a valid window renderer.");

  if (mWindowRenderer) {
    // Update the size constraints now that the layer manager has been
    // created.
    KnowsCompositor* knowsCompositor = mWindowRenderer->AsKnowsCompositor();
    if (knowsCompositor) {
      SizeConstraints c = mSizeConstraints;
      mMaxTextureSize = knowsCompositor->GetMaxTextureSize();
      c.mMaxSize.width = std::min(c.mMaxSize.width, mMaxTextureSize);
      c.mMaxSize.height = std::min(c.mMaxSize.height, mMaxTextureSize);
      nsBaseWidget::SetSizeConstraints(c);
    }
  }

  return mWindowRenderer;
}

/**************************************************************
 *
 * SECTION: nsBaseWidget::SetCompositorWidgetDelegate
 *
 * Called to connect the nsWindow to the delegate providing
 * platform compositing API access.
 *
 **************************************************************/

void nsWindow::SetCompositorWidgetDelegate(CompositorWidgetDelegate* delegate) {
  if (delegate) {
    mCompositorWidgetDelegate = delegate->AsPlatformSpecificDelegate();
    MOZ_ASSERT(mCompositorWidgetDelegate,
               "nsWindow::SetCompositorWidgetDelegate called with a "
               "non-PlatformCompositorWidgetDelegate");
  } else {
    mCompositorWidgetDelegate = nullptr;
  }
}

/**************************************************************
 *
 * SECTION: nsIWidget::OnDefaultButtonLoaded
 *
 * Called after the dialog is loaded and it has a default button.
 *
 **************************************************************/

nsresult nsWindow::OnDefaultButtonLoaded(
    const LayoutDeviceIntRect& aButtonRect) {
  if (aButtonRect.IsEmpty()) return NS_OK;

  // Don't snap when we are not active.
  HWND activeWnd = ::GetActiveWindow();
  if (activeWnd != ::GetForegroundWindow() ||
      WinUtils::GetTopLevelHWND(mWnd, true) !=
          WinUtils::GetTopLevelHWND(activeWnd, true)) {
    return NS_OK;
  }

  bool isAlwaysSnapCursor =
      Preferences::GetBool("ui.cursor_snapping.always_enabled", false);

  if (!isAlwaysSnapCursor) {
    BOOL snapDefaultButton;
    if (!::SystemParametersInfo(SPI_GETSNAPTODEFBUTTON, 0, &snapDefaultButton,
                                0) ||
        !snapDefaultButton)
      return NS_OK;
  }

  LayoutDeviceIntRect widgetRect = GetScreenBounds();
  LayoutDeviceIntRect buttonRect(aButtonRect + widgetRect.TopLeft());

  LayoutDeviceIntPoint centerOfButton(buttonRect.X() + buttonRect.Width() / 2,
                                      buttonRect.Y() + buttonRect.Height() / 2);
  // The center of the button can be outside of the widget.
  // E.g., it could be hidden by scrolling.
  if (!widgetRect.Contains(centerOfButton)) {
    return NS_OK;
  }

  if (!::SetCursorPos(centerOfButton.x, centerOfButton.y)) {
    NS_ERROR("SetCursorPos failed");
    return NS_ERROR_FAILURE;
  }
  return NS_OK;
}

uint32_t nsWindow::GetMaxTouchPoints() const {
  return WinUtils::GetMaxTouchPoints();
}

void nsWindow::SetIsEarlyBlankWindow(bool aIsEarlyBlankWindow) {
  if (mIsEarlyBlankWindow == aIsEarlyBlankWindow) {
    return;
  }
  mIsEarlyBlankWindow = aIsEarlyBlankWindow;
  if (!aIsEarlyBlankWindow) {
    // We skip processing WM_PAINT messages while we're the blank window;
    // ensure we get one to do any work we might have missed.
    MaybeInvalidateTranslucentRegion();
  }
}

/**************************************************************
 **************************************************************
 **
 ** BLOCK: Moz Events
 **
 ** Moz GUI event management.
 **
 **************************************************************
 **************************************************************/

/**************************************************************
 *
 * SECTION: Mozilla event initialization
 *
 * Helpers for initializing moz events.
 *
 **************************************************************/

// Event initialization
void nsWindow::InitEvent(WidgetGUIEvent& event, LayoutDeviceIntPoint* aPoint) {
  if (nullptr == aPoint) {  // use the point from the event
    // get the message position in client coordinates
    if (mWnd != nullptr) {
      DWORD pos = ::GetMessagePos();
      POINT cpos;

      cpos.x = GET_X_LPARAM(pos);
      cpos.y = GET_Y_LPARAM(pos);

      ::ScreenToClient(mWnd, &cpos);
      event.mRefPoint = LayoutDeviceIntPoint(cpos.x, cpos.y);
    } else {
      event.mRefPoint = LayoutDeviceIntPoint(0, 0);
    }
  } else {
    // use the point override if provided
    event.mRefPoint = *aPoint;
  }

  event.AssignEventTime(CurrentMessageWidgetEventTime());
}

WidgetEventTime nsWindow::CurrentMessageWidgetEventTime() const {
  LONG messageTime = ::GetMessageTime();
  return WidgetEventTime(GetMessageTimeStamp(messageTime));
}

/**************************************************************
 *
 * SECTION: Moz event dispatch helpers
 *
 * Helpers for dispatching different types of moz events.
 *
 **************************************************************/

// Main event dispatch. Invokes callback and ProcessEvent method on
// Event Listener object. Part of nsIWidget.
nsresult nsWindow::DispatchEvent(WidgetGUIEvent* event,
                                 nsEventStatus& aStatus) {
#ifdef WIDGET_DEBUG_OUTPUT
  debug_DumpEvent(stdout, event->mWidget, event, "something", (int32_t)mWnd);
#endif  // WIDGET_DEBUG_OUTPUT

  aStatus = nsEventStatus_eIgnore;

  // Top level windows can have a view attached which requires events be sent
  // to the underlying base window and the view. Added when we combined the
  // base chrome window with the main content child for nc client area (title
  // bar) rendering.
  if (mAttachedWidgetListener) {
    aStatus = mAttachedWidgetListener->HandleEvent(event, mUseAttachedEvents);
  } else if (mWidgetListener) {
    aStatus = mWidgetListener->HandleEvent(event, mUseAttachedEvents);
  }

  // the window can be destroyed during processing of seemingly innocuous events
  // like, say, mousedowns due to the magic of scripting. mousedowns will return
  // nsEventStatus_eIgnore, which causes problems with the deleted window.
  // therefore:
  if (mOnDestroyCalled) aStatus = nsEventStatus_eConsumeNoDefault;
  return NS_OK;
}

bool nsWindow::DispatchStandardEvent(EventMessage aMsg) {
  WidgetGUIEvent event(true, aMsg, this);
  InitEvent(event);

  bool result = DispatchWindowEvent(event);
  return result;
}

bool nsWindow::DispatchKeyboardEvent(WidgetKeyboardEvent* event) {
  nsEventStatus status = DispatchInputEvent(event).mContentStatus;
  return ConvertStatus(status);
}

bool nsWindow::DispatchContentCommandEvent(WidgetContentCommandEvent* aEvent) {
  nsEventStatus status;
  DispatchEvent(aEvent, status);
  return ConvertStatus(status);
}

bool nsWindow::DispatchWheelEvent(WidgetWheelEvent* aEvent) {
  nsEventStatus status =
      DispatchInputEvent(aEvent->AsInputEvent()).mContentStatus;
  return ConvertStatus(status);
}

// Recursively dispatch synchronous paints for nsIWidget
// descendants with invalidated rectangles.
BOOL CALLBACK nsWindow::DispatchStarvedPaints(HWND aWnd, LPARAM aMsg) {
  LONG_PTR proc = ::GetWindowLongPtrW(aWnd, GWLP_WNDPROC);
  if (proc == (LONG_PTR)&nsWindow::WindowProc) {
    // its one of our windows so check to see if it has a
    // invalidated rect. If it does. Dispatch a synchronous
    // paint.
    if (GetUpdateRect(aWnd, nullptr, FALSE)) VERIFY(::UpdateWindow(aWnd));
  }
  return TRUE;
}

// Check for pending paints and dispatch any pending paint
// messages for any nsIWidget which is a descendant of the
// top-level window that *this* window is embedded within.
//
// Note: We do not dispatch pending paint messages for non
// nsIWidget managed windows.
void nsWindow::DispatchPendingEvents() {
  // We need to ensure that reflow events do not get starved.
  // At the same time, we don't want to recurse through here
  // as that would prevent us from dispatching starved paints.
  static int recursionBlocker = 0;
  if (recursionBlocker++ == 0) {
    NS_ProcessPendingEvents(nullptr, PR_MillisecondsToInterval(100));
    --recursionBlocker;
  }

  // Quickly check to see if there are any paint events pending,
  // but only dispatch them if it has been long enough since the
  // last paint completed.
  if (::GetQueueStatus(QS_PAINT) &&
      ((TimeStamp::Now() - mLastPaintEndTime).ToMilliseconds() >= 50)) {
    // Find the top level window.
    HWND topWnd = WinUtils::GetTopLevelHWND(mWnd);

    // Dispatch pending paints for topWnd and all its descendant windows.
    // Note: EnumChildWindows enumerates all descendant windows not just
    // the children (but not the window itself).
    nsWindow::DispatchStarvedPaints(topWnd, 0);
    ::EnumChildWindows(topWnd, nsWindow::DispatchStarvedPaints, 0);
  }
}

void nsWindow::DispatchCustomEvent(const nsString& eventName) {
  if (Document* doc = GetDocument()) {
    if (nsPIDOMWindowOuter* win = doc->GetWindow()) {
      win->DispatchCustomEvent(eventName, ChromeOnlyDispatch::eYes);
    }
  }
}

bool nsWindow::TouchEventShouldStartDrag(EventMessage aEventMessage,
                                         LayoutDeviceIntPoint aEventPoint) {
  // Allow users to start dragging by double-tapping.
  if (aEventMessage == eMouseDoubleClick) {
    return true;
  }

  // In chrome UI, allow touchdownstartsdrag attributes
  // to cause any touchdown event to trigger a drag.
  if (aEventMessage == eMouseDown) {
    WidgetMouseEvent hittest(true, eMouseHitTest, this,
                             WidgetMouseEvent::eReal);
    hittest.mRefPoint = aEventPoint;
    hittest.mIgnoreRootScrollFrame = true;
    hittest.mInputSource = MouseEvent_Binding::MOZ_SOURCE_TOUCH;
    DispatchInputEvent(&hittest);

    if (EventTarget* target = hittest.GetDOMEventTarget()) {
      if (nsIContent* content = nsIContent::FromEventTarget(target)) {
        // Check if the element or any parent element has the
        // attribute we're looking for.
        for (Element* element = content->GetAsElementOrParentElement(); element;
             element = element->GetParentElement()) {
          nsAutoString startDrag;
          element->GetAttribute(u"touchdownstartsdrag"_ns, startDrag);
          if (!startDrag.IsEmpty()) {
            return true;
          }
        }
      }
    }
  }

  return false;
}

// Deal with all sort of mouse event
bool nsWindow::DispatchMouseEvent(EventMessage aEventMessage, WPARAM wParam,
                                  LPARAM lParam, bool aIsContextMenuKey,
                                  int16_t aButton, uint16_t aInputSource,
                                  WinPointerInfo* aPointerInfo,
                                  IsNonclient aIsNonclient) {
  ContextMenuPreventer contextMenuPreventer(this);
  bool result = false;

  UserActivity();

  if (!mWidgetListener) {
    return result;
  }

  LayoutDeviceIntPoint eventPoint(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
  LayoutDeviceIntPoint mpScreen = eventPoint + WidgetToScreenOffset();

  // Suppress mouse moves caused by widget creation. Make sure to do this early
  // so that we update sLastMouseMovePoint even for touch-induced mousemove
  // events.
  if (aEventMessage == eMouseMove) {
    if ((sLastMouseMovePoint.x == mpScreen.x.value) &&
        (sLastMouseMovePoint.y == mpScreen.y.value)) {
      return result;
    }
    sLastMouseMovePoint.x = mpScreen.x;
    sLastMouseMovePoint.y = mpScreen.y;
  }

  if (!bool(aIsNonclient) && WinUtils::GetIsMouseFromTouch(aEventMessage)) {
    if (mTouchWindow) {
      // If mTouchWindow is true, then we must have APZ enabled and be
      // feeding it raw touch events. In that case we only want to
      // send touch-generated mouse events to content if they should
      // start a touch-based drag-and-drop gesture, such as on
      // double-tapping or when tapping elements marked with the
      // touchdownstartsdrag attribute in chrome UI.
      MOZ_ASSERT(mAPZC);
      if (TouchEventShouldStartDrag(aEventMessage, eventPoint)) {
        aEventMessage = eMouseTouchDrag;
      } else {
        return result;
      }
    }
  }

  uint32_t pointerId =
      aPointerInfo ? aPointerInfo->pointerId : MOUSE_POINTERID();

  switch (aEventMessage) {
    case eMouseDown:
      // If the mouse was pressed down in the nonclient region, we do not
      // capture the mouse. (Doing so would cause Windows to start sending us
      // client-area mouse messages instead of nonclient-area messages.)
      if (!bool(aIsNonclient)) {
        CaptureMouse(true);
      }
      break;

    // eMouseMove and eMouseExitFromWidget are here because we need to make
    // sure capture flag isn't left on after a drag where we wouldn't see a
    // button up message (see bug 324131).
    case eMouseUp:
    case eMouseMove:
    case eMouseExitFromWidget:
      if (!(wParam & (MK_LBUTTON | MK_MBUTTON | MK_RBUTTON)) &&
          sIsInMouseCapture)
        CaptureMouse(false);
      break;

    default:
      break;

  }  // switch

  Maybe<WidgetPointerEvent> pointerEvent;
  Maybe<WidgetMouseEvent> mouseEvent;
  if (IsPointerEventMessage(aEventMessage)) {
    pointerEvent.emplace(true, aEventMessage, this,
                         aIsContextMenuKey ? WidgetMouseEvent::eContextMenuKey
                                           : WidgetMouseEvent::eNormal);
  } else {
    mouseEvent.emplace(true, aEventMessage, this, WidgetMouseEvent::eReal,
                       aIsContextMenuKey ? WidgetMouseEvent::eContextMenuKey
                                         : WidgetMouseEvent::eNormal);
  }
  WidgetMouseEvent& mouseOrPointerEvent =
      pointerEvent.isSome() ? pointerEvent.ref() : mouseEvent.ref();

  if (aEventMessage == eContextMenu && aIsContextMenuKey) {
    LayoutDeviceIntPoint zero(0, 0);
    InitEvent(mouseOrPointerEvent, &zero);
  } else {
    InitEvent(mouseOrPointerEvent, &eventPoint);
  }

  ModifierKeyState modifierKeyState;
  modifierKeyState.InitInputEvent(mouseOrPointerEvent);

  // eContextMenu with Shift state is special.  It won't fire "contextmenu"
  // event in the web content for blocking web content to prevent its default.
  // However, Shift+F10 is a standard shortcut key on Windows.  Therefore,
  // this should not block web page to prevent its default.  I.e., it should
  // behave same as ContextMenu key without Shift key.
  // XXX Should we allow to block web page to prevent its default with
  //     Ctrl+Shift+F10 or Alt+Shift+F10 instead?
  if (aEventMessage == eContextMenu && aIsContextMenuKey &&
      mouseOrPointerEvent.IsShift() &&
      NativeKey::LastKeyOrCharMSG().message == WM_SYSKEYDOWN &&
      NativeKey::LastKeyOrCharMSG().wParam == VK_F10) {
    mouseOrPointerEvent.mModifiers &= ~MODIFIER_SHIFT;
  }

  mouseOrPointerEvent.mButton = aButton;
  mouseOrPointerEvent.mInputSource = aInputSource;
  if (aPointerInfo) {
    // Mouse events from Windows WM_POINTER*. Fill more information in
    // WidgetMouseEvent.
    mouseOrPointerEvent.AssignPointerHelperData(*aPointerInfo);
    mouseOrPointerEvent.mPressure = aPointerInfo->mPressure;
    mouseOrPointerEvent.mButtons = aPointerInfo->mButtons;
  } else {
    // If we get here the mouse events must be from non-touch sources, so
    // convert it to pointer events as well
    mouseOrPointerEvent.convertToPointer = true;
    mouseOrPointerEvent.pointerId = pointerId;
  }

  if (aEventMessage == eContextMenu &&
      aInputSource == MouseEvent_Binding::MOZ_SOURCE_TOUCH) {
    MOZ_ASSERT(!aIsContextMenuKey);
    mouseOrPointerEvent.mContextMenuTrigger = WidgetMouseEvent::eNormal;
  }

  // Static variables used to distinguish simple-, double- and triple-clicks.
  static POINT sLastMousePoint = {0};
  static LONG sLastMouseDownTime = 0L;
  static LONG sLastClickCount = 0L;
  static BYTE sLastMouseButton = 0;

  bool insideMovementThreshold =
      (DeprecatedAbs(sLastMousePoint.x - eventPoint.x.value) <
       (short)::GetSystemMetrics(SM_CXDOUBLECLK)) &&
      (DeprecatedAbs(sLastMousePoint.y - eventPoint.y.value) <
       (short)::GetSystemMetrics(SM_CYDOUBLECLK));

  BYTE eventButton;
  switch (aButton) {
    case MouseButton::ePrimary:
      eventButton = VK_LBUTTON;
      break;
    case MouseButton::eMiddle:
      eventButton = VK_MBUTTON;
      break;
    case MouseButton::eSecondary:
      eventButton = VK_RBUTTON;
      break;
    default:
      eventButton = 0;
      break;
  }

  // Doubleclicks are used to set the click count, then changed to mousedowns
  // We're going to time double-clicks from mouse *up* to next mouse *down*
  LONG curMsgTime = ::GetMessageTime();

  switch (aEventMessage) {
    case eMouseDoubleClick:
      mouseOrPointerEvent.mMessage = eMouseDown;
      mouseOrPointerEvent.mButton = aButton;
      sLastClickCount = 2;
      sLastMouseDownTime = curMsgTime;
      break;
    case eMouseUp:
      // remember when this happened for the next mouse down
      sLastMousePoint.x = eventPoint.x;
      sLastMousePoint.y = eventPoint.y;
      sLastMouseButton = eventButton;
      break;
    case eMouseDown:
      // now look to see if we want to convert this to a double- or triple-click
      if (((curMsgTime - sLastMouseDownTime) < (LONG)::GetDoubleClickTime()) &&
          insideMovementThreshold && eventButton == sLastMouseButton) {
        sLastClickCount++;
      } else {
        // reset the click count, to count *this* click
        sLastClickCount = 1;
      }
      // Set last Click time on MouseDown only
      sLastMouseDownTime = curMsgTime;
      break;
    case eMouseMove:
      if (!insideMovementThreshold) {
        sLastClickCount = 0;
      }
      break;
    case eMouseExitFromWidget:
      mouseOrPointerEvent.mExitFrom =
          Some(IsTopLevelMouseExit(mWnd) ? WidgetMouseEvent::ePlatformTopLevel
                                         : WidgetMouseEvent::ePlatformChild);
      break;
    default:
      break;
  }
  mouseOrPointerEvent.mClickCount = sLastClickCount;

#ifdef NS_DEBUG_XX
  MOZ_LOG(gWindowsLog, LogLevel::Info,
          ("Msg Time: %d Click Count: %d\n", curMsgTime,
           mouseOrPointerEvent.mClickCount));
#endif

  // call the event callback
  if (mWidgetListener) {
    if (aEventMessage == eMouseMove) {
      LayoutDeviceIntRect rect = GetBounds();
      rect.MoveTo(0, 0);

      if (rect.Contains(mouseOrPointerEvent.mRefPoint)) {
        if (sCurrentWindow == nullptr || sCurrentWindow != this) {
          if ((nullptr != sCurrentWindow) && (!sCurrentWindow->mInDtor)) {
            LPARAM pos = sCurrentWindow->lParamToClient(lParamToScreen(lParam));
            sCurrentWindow->DispatchMouseEvent(
                eMouseExitFromWidget, wParam, pos, false, MouseButton::ePrimary,
                aInputSource, aPointerInfo);
          }
          sCurrentWindow = this;
          if (!mInDtor) {
            LPARAM pos = sCurrentWindow->lParamToClient(lParamToScreen(lParam));
            sCurrentWindow->DispatchMouseEvent(
                eMouseEnterIntoWidget, wParam, pos, false,
                MouseButton::ePrimary, aInputSource, aPointerInfo);
          }
        }
      }
    } else if (aEventMessage == eMouseExitFromWidget) {
      if (sCurrentWindow == this) {
        sCurrentWindow = nullptr;
      }
    }

    nsIWidget::ContentAndAPZEventStatus eventStatus =
        DispatchInputEvent(&mouseOrPointerEvent);
    contextMenuPreventer.Update(mouseOrPointerEvent, eventStatus);
    return ConvertStatus(eventStatus.mContentStatus);
  }

  return result;
}

HWND nsWindow::GetTopLevelForFocus(HWND aCurWnd) {
  // retrieve the toplevel window or dialogue
  HWND toplevelWnd = nullptr;
  while (aCurWnd) {
    toplevelWnd = aCurWnd;
    nsWindow* win = WinUtils::GetNSWindowPtr(aCurWnd);
    if (win) {
      if (win->mWindowType == WindowType::TopLevel ||
          win->mWindowType == WindowType::Dialog) {
        break;
      }
    }

    aCurWnd = ::GetParent(aCurWnd);  // Parent or owner (if has no parent)
  }
  return toplevelWnd;
}

void nsWindow::DispatchFocusToTopLevelWindow(bool aIsActivate) {
  if (aIsActivate && mPickerDisplayCount) {
    // We disable the root window when a picker opens. See PickerOpen. When the
    // picker closes (but before PickerClosed is called), our window will get
    // focus, but it will still be disabled. This confuses the focus system.
    // Therefore, we ignore this focus and explicitly call this function once
    // we re-enable the window. Rarely, the picker seems to re-enable our root
    // window before we do, but for simplicity, we always ignore focus before
    // the final call to PickerClosed. See bug 1883568 for further details.
    return;
  }

  if (aIsActivate) {
    sJustGotActivate = false;
  }
  sJustGotDeactivate = false;
  mLastKillFocusWindow = nullptr;

  HWND toplevelWnd = GetTopLevelForFocus(mWnd);

  if (toplevelWnd) {
    nsWindow* win = WinUtils::GetNSWindowPtr(toplevelWnd);
    if (win && win->mWidgetListener) {
      if (aIsActivate) {
        win->mWidgetListener->WindowActivated();
      } else {
        win->mWidgetListener->WindowDeactivated();
      }
    }
  }
}

HWND nsWindow::WindowAtMouse() {
  DWORD pos = ::GetMessagePos();
  POINT mp;
  mp.x = GET_X_LPARAM(pos);
  mp.y = GET_Y_LPARAM(pos);
  return ::WindowFromPoint(mp);
}

bool nsWindow::IsTopLevelMouseExit(HWND aWnd) {
  HWND mouseWnd = WindowAtMouse();

  // WinUtils::GetTopLevelHWND() will return a HWND for the window frame
  // (which includes the non-client area).  If the mouse has moved into
  // the non-client area, we should treat it as a top-level exit.
  HWND mouseTopLevel = WinUtils::GetTopLevelHWND(mouseWnd);
  if (mouseWnd == mouseTopLevel) return true;

  return WinUtils::GetTopLevelHWND(aWnd) != mouseTopLevel;
}

/**************************************************************
 *
 * SECTION: IPC
 *
 * IPC related helpers.
 *
 **************************************************************/

// static
bool nsWindow::IsAsyncResponseEvent(UINT aMsg, LRESULT& aResult) {
  switch (aMsg) {
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
    case WM_ENABLE:
    case WM_WINDOWPOSCHANGING:
    case WM_WINDOWPOSCHANGED:
    case WM_PARENTNOTIFY:
    case WM_ACTIVATEAPP:
    case WM_NCACTIVATE:
    case WM_ACTIVATE:
    case WM_CHILDACTIVATE:
    case WM_IME_SETCONTEXT:
    case WM_IME_NOTIFY:
    case WM_SHOWWINDOW:
    case WM_CANCELMODE:
    case WM_MOUSEACTIVATE:
    case WM_CONTEXTMENU:
      aResult = 0;
      return true;

    case WM_SETTINGCHANGE:
    case WM_SETCURSOR:
      return false;
  }

#ifdef DEBUG
  char szBuf[200];
  sprintf(szBuf,
          "An unhandled ISMEX_SEND message was received during spin loop! (%X)",
          aMsg);
  NS_WARNING(szBuf);
#endif

  return false;
}

void nsWindow::IPCWindowProcHandler(UINT& msg, WPARAM& wParam, LPARAM& lParam) {
  MOZ_ASSERT_IF(
      msg != WM_GETOBJECT,
      !mozilla::ipc::MessageChannel::IsPumpingMessages() ||
          mozilla::ipc::SuppressedNeuteringRegion::IsNeuteringSuppressed());

  // Modal UI being displayed in windowless plugins.
  if (mozilla::ipc::MessageChannel::IsSpinLoopActive() &&
      (InSendMessageEx(nullptr) & (ISMEX_REPLIED | ISMEX_SEND)) == ISMEX_SEND) {
    LRESULT res;
    if (IsAsyncResponseEvent(msg, res)) {
      ReplyMessage(res);
    }
    return;
  }

  // Handle certain sync plugin events sent to the parent which
  // trigger ipc calls that result in deadlocks.

  DWORD dwResult = 0;
  bool handled = false;

  switch (msg) {
    // Windowless flash sending WM_ACTIVATE events to the main window
    // via calls to ShowWindow.
    case WM_ACTIVATE:
      if (lParam != 0 && LOWORD(wParam) == WA_ACTIVE &&
          IsWindow((HWND)lParam)) {
        // Check for Adobe Reader X sync activate message from their
        // helper window and ignore. Fixes an annoying focus problem.
        if ((InSendMessageEx(nullptr) & (ISMEX_REPLIED | ISMEX_SEND)) ==
            ISMEX_SEND) {
          wchar_t szClass[10];
          HWND focusWnd = (HWND)lParam;
          if (IsWindowVisible(focusWnd) &&
              GetClassNameW(focusWnd, szClass,
                            sizeof(szClass) / sizeof(char16_t)) &&
              !wcscmp(szClass, L"Edit") &&
              !WinUtils::IsOurProcessWindow(focusWnd)) {
            break;
          }
        }
        handled = true;
      }
      break;
    // Plugins taking or losing focus triggering focus app messages.
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
    // Windowed plugins that pass sys key events to defwndproc generate
    // WM_SYSCOMMAND events to the main window.
    case WM_SYSCOMMAND:
    // Windowed plugins that fire context menu selection events to parent
    // windows.
    case WM_CONTEXTMENU:
    // IME events fired as a result of synchronous focus changes
    case WM_IME_SETCONTEXT:
      handled = true;
      break;
  }

  if (handled &&
      (InSendMessageEx(nullptr) & (ISMEX_REPLIED | ISMEX_SEND)) == ISMEX_SEND) {
    ReplyMessage(dwResult);
  }
}

/**************************************************************
 **************************************************************
 **
 ** BLOCK: Native events
 **
 ** Main Windows message handlers and OnXXX handlers for
 ** Windows event handling.
 **
 **************************************************************
 **************************************************************/

/**************************************************************
 *
 * SECTION: Wind proc.
 *
 * The main Windows event procedures and associated
 * message processing methods.
 *
 **************************************************************/

static bool DisplaySystemMenu(HWND hWnd, nsSizeMode sizeMode, bool isRtl,
                              int32_t x, int32_t y) {
  HMENU hMenu = GetSystemMenu(hWnd, FALSE);
  if (NS_WARN_IF(!hMenu)) {
    return false;
  }

  MENUITEMINFO mii;
  mii.cbSize = sizeof(MENUITEMINFO);
  mii.fMask = MIIM_STATE;
  mii.fType = 0;

  // update the options
  mii.fState = MF_ENABLED;
  SetMenuItemInfo(hMenu, SC_RESTORE, FALSE, &mii);
  SetMenuItemInfo(hMenu, SC_SIZE, FALSE, &mii);
  SetMenuItemInfo(hMenu, SC_MOVE, FALSE, &mii);
  SetMenuItemInfo(hMenu, SC_MAXIMIZE, FALSE, &mii);
  SetMenuItemInfo(hMenu, SC_MINIMIZE, FALSE, &mii);

  mii.fState = MF_GRAYED;
  switch (sizeMode) {
    case nsSizeMode_Fullscreen:
      // intentional fall through
    case nsSizeMode_Maximized:
      SetMenuItemInfo(hMenu, SC_SIZE, FALSE, &mii);
      SetMenuItemInfo(hMenu, SC_MOVE, FALSE, &mii);
      SetMenuItemInfo(hMenu, SC_MAXIMIZE, FALSE, &mii);
      break;
    case nsSizeMode_Minimized:
      SetMenuItemInfo(hMenu, SC_MINIMIZE, FALSE, &mii);
      break;
    case nsSizeMode_Normal:
      SetMenuItemInfo(hMenu, SC_RESTORE, FALSE, &mii);
      break;
    case nsSizeMode_Invalid:
      NS_ASSERTION(false, "Did the argument come from invalid IPC?");
      break;
    default:
      MOZ_ASSERT_UNREACHABLE("Unhnalded nsSizeMode value detected");
      break;
  }
  LPARAM cmd = TrackPopupMenu(hMenu,
                              TPM_LEFTBUTTON | TPM_RIGHTBUTTON | TPM_RETURNCMD |
                                  TPM_TOPALIGN |
                                  (isRtl ? TPM_RIGHTALIGN : TPM_LEFTALIGN),
                              x, y, 0, hWnd, nullptr);
  if (!cmd) {
    return false;
  }
  PostMessage(hWnd, WM_SYSCOMMAND, cmd, 0);
  return true;
}

// The WndProc procedure for all nsWindows in this toolkit. This merely catches
// SEH exceptions and passes the real work to WindowProcInternal. See bug 587406
// and http://msdn.microsoft.com/en-us/library/ms633573%28VS.85%29.aspx
LRESULT CALLBACK nsWindow::WindowProc(HWND hWnd, UINT msg, WPARAM wParam,
                                      LPARAM lParam) {
  mozilla::ipc::CancelCPOWs();

  BackgroundHangMonitor().NotifyActivity();

  return mozilla::CallWindowProcCrashProtected(WindowProcInternal, hWnd, msg,
                                               wParam, lParam);
}

LRESULT CALLBACK nsWindow::WindowProcInternal(HWND hWnd, UINT msg,
                                              WPARAM wParam, LPARAM lParam) {
  if (::GetWindowLongPtrW(hWnd, GWLP_ID) == eFakeTrackPointScrollableID) {
    // This message was sent to the FAKETRACKPOINTSCROLLABLE.
    if (msg == WM_HSCROLL) {
      // Route WM_HSCROLL messages to the main window.
      hWnd = ::GetParent(::GetParent(hWnd));
    } else {
      // Handle all other messages with its original window procedure.
      WNDPROC prevWindowProc = (WNDPROC)::GetWindowLongPtr(hWnd, GWLP_USERDATA);
      return ::CallWindowProcW(prevWindowProc, hWnd, msg, wParam, lParam);
    }
  }

  // Get the window which caused the event and ask it to process the message
  nsWindow* targetWindow = WinUtils::GetNSWindowPtr(hWnd);
  NS_ASSERTION(targetWindow, "nsWindow* is null!");
  if (!targetWindow) return ::DefWindowProcW(hWnd, msg, wParam, lParam);

  // Hold the window for the life of this method, in case it gets
  // destroyed during processing, unless we're in the dtor already.
  nsCOMPtr<nsIWidget> kungFuDeathGrip;
  if (!targetWindow->mInDtor) kungFuDeathGrip = targetWindow;

  targetWindow->IPCWindowProcHandler(msg, wParam, lParam);

  // Create this here so that we store the last rolled up popup until after
  // the event has been processed.
  nsAutoRollup autoRollup;

  LRESULT popupHandlingResult;
  if (DealWithPopups(hWnd, msg, wParam, lParam, &popupHandlingResult)) {
    return popupHandlingResult;
  }

  // Call ProcessMessage
  LRESULT retValue;
  if (targetWindow->ProcessMessage(msg, wParam, lParam, &retValue)) {
    return retValue;
  }

  LRESULT res = ::CallWindowProcW(targetWindow->GetPrevWindowProc(), hWnd, msg,
                                  wParam, lParam);

  return res;
}

const char16_t* GetQuitType() {
  if (Preferences::GetBool(PREF_WIN_REGISTER_APPLICATION_RESTART, false)) {
    DWORD cchCmdLine = 0;
    HRESULT rc = ::GetApplicationRestartSettings(::GetCurrentProcess(), nullptr,
                                                 &cchCmdLine, nullptr);
    if (rc == S_OK) {
      return u"os-restart";
    }
  }
  return nullptr;
}

bool nsWindow::ExternalHandlerProcessMessage(UINT aMessage, WPARAM& aWParam,
                                             LPARAM& aLParam,
                                             MSGResult& aResult) {
  if (mWindowHook.Notify(mWnd, aMessage, aWParam, aLParam, aResult)) {
    return true;
  }

  if (IMEHandler::ProcessMessage(this, aMessage, aWParam, aLParam, aResult)) {
    return true;
  }

  if (MouseScrollHandler::ProcessMessage(this, aMessage, aWParam, aLParam,
                                         aResult)) {
    return true;
  }

  return false;
}

// The main windows message processing method. Wraps ProcessMessageInternal so
// we can log aRetValue.
bool nsWindow::ProcessMessage(UINT msg, WPARAM& wParam, LPARAM& lParam,
                              LRESULT* aRetValue) {
  // For some events we might change the parameter values, so log
  // before and after we process them.
  NativeEventLogger eventLogger("nsWindow", mWnd, msg, wParam, lParam);
  bool result = ProcessMessageInternal(msg, wParam, lParam, aRetValue);
  eventLogger.SetResult(*aRetValue, result);

  return result;
}

// The main windows message processing method. Called by ProcessMessage.
bool nsWindow::ProcessMessageInternal(UINT msg, WPARAM& wParam, LPARAM& lParam,
                                      LRESULT* aRetValue) {
  MSGResult msgResult(aRetValue);
  if (ExternalHandlerProcessMessage(msg, wParam, lParam, msgResult)) {
    return (msgResult.mConsumed || !mWnd);
  }

  bool result = false;  // call the default nsWindow proc
  *aRetValue = 0;

  // The DWM resize hack (see bug 1763981) causes us to process a number of
  // messages, notably including some WM_WINDOWPOSCHANG{ING,ED} messages which
  // would ordinarily result in a whole lot of internal state being updated.
  //
  // Since we're supposed to end in the same state we started in (and since the
  // content shouldn't know about any of this nonsense), just discard any
  // messages synchronously dispatched from within the hack.
  if (MOZ_UNLIKELY(mIsPerformingDwmFlushHack)) {
    return true;
  }

  // Glass hit testing w/custom transparent margins.
  //
  // FIXME(emilio): is this needed? We deal with titlebar buttons non-natively
  // now.
  LRESULT dwmHitResult;
  if (mCustomNonClient &&
      DwmDefWindowProc(mWnd, msg, wParam, lParam, &dwmHitResult)) {
    *aRetValue = dwmHitResult;
    return true;
  }

  // The preference whether to use a different keyboard layout for each
  // window is cached, and updating it will not take effect until the
  // next restart. We read the preference here and not upon WM_ACTIVATE to make
  // sure that this behavior is consistent. Otherwise, if the user changed the
  // preference before having ever lowered the window, the preference would take
  // effect immediately.
  static const bool sSwitchKeyboardLayout =
      Preferences::GetBool("intl.keyboard.per_window_layout", false);
  AppShutdownReason shutdownReason = AppShutdownReason::Unknown;

  // (Large blocks of code should be broken out into OnEvent handlers.)
  switch (msg) {
    // WM_QUERYENDSESSION must be handled by all windows.
    // Otherwise Windows thinks the window can just be killed at will.
    case WM_QUERYENDSESSION: {
      // Ask around if it's ok to quit.
      nsCOMPtr<nsIObserverService> obsServ =
          mozilla::services::GetObserverService();
      nsCOMPtr<nsISupportsPRBool> cancelQuitWrapper =
          do_CreateInstance(NS_SUPPORTS_PRBOOL_CONTRACTID);
      cancelQuitWrapper->SetData(false);

      const char16_t* quitType = GetQuitType();
      obsServ->NotifyObservers(cancelQuitWrapper, "quit-application-requested",
                               quitType);

      bool shouldCancelQuit;
      cancelQuitWrapper->GetData(&shouldCancelQuit);
      *aRetValue = !shouldCancelQuit;
      result = true;
    } break;

    case MOZ_WM_STARTA11Y:
#if defined(ACCESSIBILITY)
      Unused << GetAccessible();
      result = true;
#else
      result = false;
#endif
      break;

    case WM_ENDSESSION: {
      // For WM_ENDSESSION, wParam indicates whether we need to shutdown
      // (TRUE) or not (FALSE).
      if (!wParam) {
        result = true;
        break;
      }

      // According to WM_ENDSESSION lParam documentation:
      //   0 -> OS shutdown or restart (no way to distinguish)
      //   ENDSESSION_LOGOFF -> User is logging off
      //   ENDSESSION_CLOSEAPP -> Application must shutdown
      //   ENDSESSION_CRITICAL -> Application is forced to shutdown
      // The difference of the last two is not very clear.
      if (lParam == 0) {
        shutdownReason = AppShutdownReason::OSShutdown;
      } else if (lParam & ENDSESSION_LOGOFF) {
        shutdownReason = AppShutdownReason::OSSessionEnd;
      } else if (lParam & (ENDSESSION_CLOSEAPP | ENDSESSION_CRITICAL)) {
        shutdownReason = AppShutdownReason::OSForceClose;
      } else {
        MOZ_DIAGNOSTIC_CRASH("Received WM_ENDSESSION with unknown flags.");
        shutdownReason = AppShutdownReason::OSForceClose;
      }

      // Let's fake a shutdown sequence without actually closing windows etc.
      // to avoid Windows killing us in the middle. A proper shutdown would
      // require having a chance to pump some messages. Unfortunately
      // Windows won't let us do that. Bug 212316.
      nsCOMPtr<nsIObserverService> obsServ =
          mozilla::services::GetObserverService();
      const char16_t* syncShutdown = u"syncShutdown";
      const char16_t* quitType = GetQuitType();

      AppShutdown::Init(AppShutdownMode::Normal, 0, shutdownReason);

      obsServ->NotifyObservers(nullptr, "quit-application-granted",
                               syncShutdown);
      obsServ->NotifyObservers(nullptr, "quit-application-forced", nullptr);

      AppShutdown::OnShutdownConfirmed();

      AppShutdown::AdvanceShutdownPhase(ShutdownPhase::AppShutdownConfirmed,
                                        quitType);
      AppShutdown::AdvanceShutdownPhase(ShutdownPhase::AppShutdownNetTeardown,
                                        nullptr);
      AppShutdown::AdvanceShutdownPhase(ShutdownPhase::AppShutdownTeardown,
                                        nullptr);
      AppShutdown::AdvanceShutdownPhase(ShutdownPhase::AppShutdown, nullptr);
      AppShutdown::AdvanceShutdownPhase(ShutdownPhase::AppShutdownQM, nullptr);
      AppShutdown::AdvanceShutdownPhase(ShutdownPhase::AppShutdownTelemetry,
                                        nullptr);

      AppShutdown::DoImmediateExit();
      MOZ_ASSERT_UNREACHABLE("Our process was supposed to exit.");
    } break;

    case WM_THEMECHANGED: {
      // Update non-client margin offsets
      UpdateNonClientMargins();
      // Invalidate the window so that the repaint will pick up the new theme.
      Invalidate(true, true, true);
    } break;

    case WM_WTSSESSION_CHANGE: {
      switch (wParam) {
        case WTS_CONSOLE_CONNECT:
        case WTS_REMOTE_CONNECT:
        case WTS_SESSION_UNLOCK:
          // When a session becomes visible, we should invalidate.
          Invalidate(true, true, true);
          break;
        default:
          break;
      }
    } break;

    case WM_NCCALCSIZE: {
      // NOTE: the following block is mirrored in PreXULSkeletonUI.cpp, and
      // will need to be kept in sync.
      if (mCustomNonClient) {
        // If `wParam` is `FALSE`, `lParam` points to a `RECT` that contains
        // the proposed window rectangle for our window.  During our
        // processing of the `WM_NCCALCSIZE` message, we are expected to
        // modify the `RECT` that `lParam` points to, so that its value upon
        // our return is the new client area.  We must return 0 if `wParam`
        // is `FALSE`.
        //
        // If `wParam` is `TRUE`, `lParam` points to a `NCCALCSIZE_PARAMS`
        // struct.  This struct contains an array of 3 `RECT`s, the first of
        // which has the exact same meaning as the `RECT` that is pointed to
        // by `lParam` when `wParam` is `FALSE`.  The remaining `RECT`s, in
        // conjunction with our return value, can
        // be used to specify portions of the source and destination window
        // rectangles that are valid and should be preserved.  We opt not to
        // implement an elaborate client-area preservation technique, and
        // simply return 0, which means "preserve the entire old client area
        // and align it with the upper-left corner of our new client area".
        RECT* clientRect =
            wParam ? &(reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam))->rgrc[0]
                   : (reinterpret_cast<RECT*>(lParam));
        auto margin = NonClientSizeMargin();
        clientRect->top += margin.top;
        clientRect->left += margin.left;
        clientRect->right -= margin.right;
        clientRect->bottom -= margin.bottom;
        // Make client rect's width and height more than 0 to
        // avoid problems of webrender and angle.
        clientRect->right = std::max(clientRect->right, clientRect->left + 1);
        clientRect->bottom = std::max(clientRect->bottom, clientRect->top + 1);

        result = true;
        *aRetValue = 0;
      }
      break;
    }

    case WM_GETTITLEBARINFOEX: {
      if (!mCustomNonClient) {
        break;
      }
      auto* info = reinterpret_cast<TITLEBARINFOEX*>(lParam);
      const LayoutDeviceIntPoint origin = WidgetToScreenOffset();
      auto GeckoClientToWinScreenRect =
          [&origin](LayoutDeviceIntRect aRect) -> RECT {
        aRect.MoveBy(origin);
        return WinUtils::ToWinRect(aRect);
      };
      auto SetButton = [&](size_t aIndex, WindowButtonType aType) {
        info->rgrect[aIndex] =
            GeckoClientToWinScreenRect(mWindowBtnRect[aType]);
        DWORD& state = info->rgstate[aIndex];
        if (mWindowBtnRect[aType].IsEmpty()) {
          state = STATE_SYSTEM_INVISIBLE;
        } else {
          state = STATE_SYSTEM_FOCUSABLE;
        }
      };
      info->rgrect[0] = info->rcTitleBar =
          GeckoClientToWinScreenRect(mDraggableRegion.GetBounds());
      info->rgstate[0] = 0;
      SetButton(2, WindowButtonType::Minimize);
      SetButton(3, WindowButtonType::Maximize);
      SetButton(5, WindowButtonType::Close);
      // We don't have a help button.
      info->rgstate[4] = STATE_SYSTEM_INVISIBLE;
      info->rgrect[4] = {0, 0, 0, 0};
      result = true;
    } break;

    case WM_NCHITTEST: {
      if (mInputRegion.mFullyTransparent) {
        // Treat this window as transparent.
        *aRetValue = HTTRANSPARENT;
        result = true;
        break;
      }

      if (mInputRegion.mMargin) {
        const LayoutDeviceIntPoint screenPoint(GET_X_LPARAM(lParam),
                                               GET_Y_LPARAM(lParam));
        LayoutDeviceIntRect screenRect = GetScreenBounds();
        screenRect.Deflate(mInputRegion.mMargin);
        if (!screenRect.Contains(screenPoint)) {
          *aRetValue = HTTRANSPARENT;
          result = true;
          break;
        }
      }

      /*
       * If an nc client area margin has been moved, we are responsible
       * for calculating where the resize margins are and returning the
       * appropriate set of hit test constants. DwmDefWindowProc (above)
       * will handle hit testing on it's command buttons if we are on a
       * composited desktop.
       */

      if (!mCustomNonClient) {
        break;
      }

      *aRetValue =
          ClientMarginHitTestPoint(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
      result = true;
      break;
    }

    case WM_SETTEXT:
      /*
       * WM_SETTEXT paints the titlebar area. Avoid this if we have a
       * custom titlebar we paint ourselves, or if we're the ones
       * sending the message with an updated title
       */

      if (mSendingSetText || !mCustomNonClient) {
        break;
      }

      {
        // From msdn, the way around this is to disable the visible state
        // temporarily. We need the text to be set but we don't want the
        // redraw to occur. However, we need to make sure that we don't
        // do this at the same time that a Present is happening.
        //
        // To do this we take mPresentLock in nsWindow::PreRender and
        // if that lock is taken we wait before doing WM_SETTEXT
        if (mCompositorWidgetDelegate) {
          mCompositorWidgetDelegate->EnterPresentLock();
        }
        DWORD style = GetWindowLong(mWnd, GWL_STYLE);
        SetWindowLong(mWnd, GWL_STYLE, style & ~WS_VISIBLE);
        *aRetValue =
            CallWindowProcW(GetPrevWindowProc(), mWnd, msg, wParam, lParam);
        SetWindowLong(mWnd, GWL_STYLE, style);
        if (mCompositorWidgetDelegate) {
          mCompositorWidgetDelegate->LeavePresentLock();
        }

        return true;
      }

    case WM_NCACTIVATE: {
      if (!mCustomNonClient) {
        break;
      }

      // There is a case that rendered result is not kept. Bug 1237617
      if (wParam == TRUE && !gfxEnv::MOZ_DISABLE_FORCE_PRESENT()) {
        NS_DispatchToMainThread(NewRunnableMethod(
            "nsWindow::ForcePresent", this, &nsWindow::ForcePresent));
      }

      // ::DefWindowProc would paint nc areas. Avoid this, since we just want
      // dwm to take care of re-displaying the glass effect if any. Quoting the
      // docs[1]:
      //
      //     If this parameter is set to -1, DefWindowProc does not repaint the
      //     nonclient area to reflect the state change.
      //
      // [1]:
      // https://learn.microsoft.com/en-us/windows/win32/winmsg/wm-ncactivate
      lParam = -1;
    } break;

    case WM_NCPAINT: {
      // ClearType changes often don't send a WM_SETTINGCHANGE message. But
      // they do seem to always send a WM_NCPAINT message, so let's update on
      // that.
      gfxDWriteFont::UpdateSystemTextVars();
    } break;

    case WM_POWERBROADCAST:
      switch (wParam) {
        case PBT_APMSUSPEND:
          PostSleepWakeNotification(true);
          break;
        case PBT_APMRESUMEAUTOMATIC:
        case PBT_APMRESUMECRITICAL:
        case PBT_APMRESUMESUSPEND:
          PostSleepWakeNotification(false);
          break;
      }
      break;

    case WM_CLOSE:  // close request
      if (mWidgetListener) mWidgetListener->RequestWindowClose(this);
      result = true;  // abort window closure
      break;

    case WM_DESTROY:
      // clean up.
      DestroyLayerManager();
      OnDestroy();
      result = true;
      break;

    case WM_PAINT:
      *aRetValue = (int)OnPaint();
      result = true;
      break;

    case WM_HOTKEY:
      result = OnHotKey(wParam, lParam);
      break;

    case WM_SYSCHAR:
    case WM_CHAR: {
      MSG nativeMsg = WinUtils::InitMSG(msg, wParam, lParam, mWnd);
      result = ProcessCharMessage(nativeMsg, nullptr);
      DispatchPendingEvents();
    } break;

    case WM_SYSKEYUP:
    case WM_KEYUP: {
      MSG nativeMsg = WinUtils::InitMSG(msg, wParam, lParam, mWnd);
      nativeMsg.time = ::GetMessageTime();
      result = ProcessKeyUpMessage(nativeMsg, nullptr);
      DispatchPendingEvents();
    } break;

    case WM_SYSKEYDOWN:
    case WM_KEYDOWN: {
      MSG nativeMsg = WinUtils::InitMSG(msg, wParam, lParam, mWnd);
      result = ProcessKeyDownMessage(nativeMsg, nullptr);
      DispatchPendingEvents();
    } break;

    // Say we've dealt with erasing the background. (This is actually handled in
    // WM_PAINT or at window-creation time, as necessary.)
    case WM_ERASEBKGND: {
      *aRetValue = 1;
      result = true;
    } break;

    case WM_MOUSEMOVE: {
      LPARAM lParamScreen = lParamToScreen(lParam);
      mSimulatedClientArea = IsSimulatedClientArea(GET_X_LPARAM(lParamScreen),
                                                   GET_Y_LPARAM(lParamScreen));

      if (!mMousePresent && !sIsInMouseCapture) {
        // First MOUSEMOVE over the client area. Ask for MOUSELEAVE
        TRACKMOUSEEVENT mTrack;
        mTrack.cbSize = sizeof(TRACKMOUSEEVENT);
        mTrack.dwFlags = TME_LEAVE;
        mTrack.dwHoverTime = 0;
        mTrack.hwndTrack = mWnd;
        TrackMouseEvent(&mTrack);
      }
      mMousePresent = true;

      // Suppress dispatch of pending events
      // when mouse moves are generated by widget
      // creation instead of user input.
      POINT mp;
      mp.x = GET_X_LPARAM(lParamScreen);
      mp.y = GET_Y_LPARAM(lParamScreen);
      bool userMovedMouse = false;
      if ((sLastMouseMovePoint.x != mp.x) || (sLastMouseMovePoint.y != mp.y)) {
        userMovedMouse = true;
      }

      if (userMovedMouse) {
        result = DispatchMouseEvent(
            eMouseMove, wParam, lParam, false, MouseButton::ePrimary,
            MOUSE_INPUT_SOURCE(),
            mPointerEvents.GetCachedPointerInfo(msg, wParam));
        DispatchPendingEvents();
      }
    } break;

    case WM_NCMOUSEMOVE: {
      LPARAM lParamClient = lParamToClient(lParam);
      if (IsSimulatedClientArea(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam))) {
        if (!sIsInMouseCapture) {
          TRACKMOUSEEVENT mTrack;
          mTrack.cbSize = sizeof(TRACKMOUSEEVENT);
          mTrack.dwFlags = TME_LEAVE | TME_NONCLIENT;
          mTrack.dwHoverTime = 0;
          mTrack.hwndTrack = mWnd;
          TrackMouseEvent(&mTrack);
        }
        // If we noticed the mouse moving in our draggable region, forward the
        // message as a normal WM_MOUSEMOVE.
        SendMessage(mWnd, WM_MOUSEMOVE, 0, lParamClient);
      } else {
        // We've transitioned from a draggable area to somewhere else within
        // the non-client area - perhaps one of the edges of the window for
        // resizing.
        mSimulatedClientArea = false;
      }

      if (mMousePresent && !sIsInMouseCapture && !mSimulatedClientArea) {
        SendMessage(mWnd, WM_MOUSELEAVE, 0, 0);
      }
    } break;

    case WM_LBUTTONDOWN: {
      result =
          DispatchMouseEvent(eMouseDown, wParam, lParam, false,
                             MouseButton::ePrimary, MOUSE_INPUT_SOURCE(),
                             mPointerEvents.GetCachedPointerInfo(msg, wParam));
      DispatchPendingEvents();
    } break;

    case WM_LBUTTONUP: {
      result =
          DispatchMouseEvent(eMouseUp, wParam, lParam, false,
                             MouseButton::ePrimary, MOUSE_INPUT_SOURCE(),
                             mPointerEvents.GetCachedPointerInfo(msg, wParam));
      DispatchPendingEvents();
    } break;

    case WM_NCMOUSELEAVE: {
      mSimulatedClientArea = false;

      if (EventIsInsideWindow(this)) {
        // If we're handling WM_NCMOUSELEAVE and the mouse is still over the
        // window, then by process of elimination, the mouse has moved from the
        // non-client to client area, so no need to fall-through to the
        // WM_MOUSELEAVE handler. We also need to re-register for the
        // WM_MOUSELEAVE message, since according to the documentation at [1],
        // all tracking requested via TrackMouseEvent is cleared once
        // WM_NCMOUSELEAVE or WM_MOUSELEAVE fires.
        // [1]:
        // https://docs.microsoft.com/en-us/windows/desktop/api/winuser/nf-winuser-trackmouseevent
        TRACKMOUSEEVENT mTrack;
        mTrack.cbSize = sizeof(TRACKMOUSEEVENT);
        mTrack.dwFlags = TME_LEAVE;
        mTrack.dwHoverTime = 0;
        mTrack.hwndTrack = mWnd;
        TrackMouseEvent(&mTrack);
        break;
      }
      // We've transitioned from non-client to outside of the window, so
      // fall-through to the WM_MOUSELEAVE handler.
      [[fallthrough]];
    }
    case WM_MOUSELEAVE: {
      if (!mMousePresent) break;
      if (mSimulatedClientArea) break;
      mMousePresent = false;

      // Check if the mouse is over the fullscreen transition window, if so
      // clear sLastMouseMovePoint. This way the WM_MOUSEMOVE we get after the
      // transition window disappears will not be ignored, even if the mouse
      // hasn't moved.
      if (mTransitionWnd && WindowAtMouse() == mTransitionWnd) {
        sLastMouseMovePoint = {0};
      }

      // We need to check mouse button states and put them in for
      // wParam.
      WPARAM mouseState = (GetKeyState(VK_LBUTTON) ? MK_LBUTTON : 0) |
                          (GetKeyState(VK_MBUTTON) ? MK_MBUTTON : 0) |
                          (GetKeyState(VK_RBUTTON) ? MK_RBUTTON : 0);
      // Synthesize an event position because we don't get one from
      // WM_MOUSELEAVE.
      LPARAM pos = lParamToClient(::GetMessagePos());
      DispatchMouseEvent(eMouseExitFromWidget, mouseState, pos, false,
                         MouseButton::ePrimary, MOUSE_INPUT_SOURCE());
    } break;

    case WM_CONTEXTMENU: {
      // If the context menu is brought up by a touch long-press, then
      // the APZ code is responsible for dealing with this, so we don't
      // need to do anything.
      if (mTouchWindow &&
          MOUSE_INPUT_SOURCE() == MouseEvent_Binding::MOZ_SOURCE_TOUCH) {
        MOZ_ASSERT(mAPZC);  // since mTouchWindow is true, APZ must be enabled
        result = true;
        break;
      }

      // If this WM_CONTEXTMENU is triggered by a mouse's secondary button up
      // event in overscroll gutter, we shouldn't open context menu.
      if (MOUSE_INPUT_SOURCE() == MouseEvent_Binding::MOZ_SOURCE_MOUSE &&
          mNeedsToPreventContextMenu) {
        result = true;
        break;
      }

      // if the context menu is brought up from the keyboard, |lParam|
      // will be -1.
      LPARAM pos;
      bool contextMenukey = false;
      if (lParam == -1) {
        contextMenukey = true;
        pos = lParamToClient(GetMessagePos());
      } else {
        pos = lParamToClient(lParam);
      }

      uint16_t inputSource = MOUSE_INPUT_SOURCE();
      int16_t button =
          (contextMenukey ||
           inputSource == dom::MouseEvent_Binding::MOZ_SOURCE_TOUCH)
              ? MouseButton::ePrimary
              : MouseButton::eSecondary;

      result = DispatchMouseEvent(eContextMenu, wParam, pos, contextMenukey,
                                  button, inputSource);
      if (lParam != -1 && !result && mCustomNonClient &&
          mDraggableRegion.Contains(GET_X_LPARAM(pos), GET_Y_LPARAM(pos))) {
        // Blank area hit, throw up the system menu.
        DisplaySystemMenu(mWnd, mFrameState->GetSizeMode(), mIsRTL,
                          GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        result = true;
      }
    } break;

    case WM_POINTERLEAVE:
    case WM_POINTERDOWN:
    case WM_POINTERUP:
    case WM_POINTERUPDATE:
      result = OnPointerEvents(msg, wParam, lParam);
      if (result) {
        DispatchPendingEvents();
      }
      break;

    case DM_POINTERHITTEST:
      if (mDmOwner) {
        UINT contactId = GET_POINTERID_WPARAM(wParam);
        POINTER_INPUT_TYPE pointerType;
        if (mPointerEvents.GetPointerType(contactId, &pointerType) &&
            pointerType == PT_TOUCHPAD) {
          mDmOwner->SetContact(contactId);
        }
      }
      break;

    case WM_LBUTTONDBLCLK:
      result = DispatchMouseEvent(eMouseDoubleClick, wParam, lParam, false,
                                  MouseButton::ePrimary, MOUSE_INPUT_SOURCE());
      DispatchPendingEvents();
      break;

    case WM_MBUTTONDOWN:
      result = DispatchMouseEvent(eMouseDown, wParam, lParam, false,
                                  MouseButton::eMiddle, MOUSE_INPUT_SOURCE());
      DispatchPendingEvents();
      break;

    case WM_MBUTTONUP:
      result = DispatchMouseEvent(eMouseUp, wParam, lParam, false,
                                  MouseButton::eMiddle, MOUSE_INPUT_SOURCE());
      DispatchPendingEvents();
      break;

    case WM_MBUTTONDBLCLK:
      result = DispatchMouseEvent(eMouseDoubleClick, wParam, lParam, false,
                                  MouseButton::eMiddle, MOUSE_INPUT_SOURCE());
      DispatchPendingEvents();
      break;

    case WM_NCMBUTTONDOWN:
      result = DispatchMouseEvent(eMouseDown, 0, lParamToClient(lParam), false,
                                  MouseButton::eMiddle, MOUSE_INPUT_SOURCE());
      DispatchPendingEvents();
      break;

    case WM_NCMBUTTONUP:
      result = DispatchMouseEvent(eMouseUp, 0, lParamToClient(lParam), false,
                                  MouseButton::eMiddle, MOUSE_INPUT_SOURCE());
      DispatchPendingEvents();
      break;

    case WM_NCMBUTTONDBLCLK:
      result =
          DispatchMouseEvent(eMouseDoubleClick, 0, lParamToClient(lParam),
                             false, MouseButton::eMiddle, MOUSE_INPUT_SOURCE());
      DispatchPendingEvents();
      break;

    case WM_RBUTTONDOWN:
      result =
          DispatchMouseEvent(eMouseDown, wParam, lParam, false,
                             MouseButton::eSecondary, MOUSE_INPUT_SOURCE(),
                             mPointerEvents.GetCachedPointerInfo(msg, wParam));
      DispatchPendingEvents();
      break;

    case WM_RBUTTONUP:
      result =
          DispatchMouseEvent(eMouseUp, wParam, lParam, false,
                             MouseButton::eSecondary, MOUSE_INPUT_SOURCE(),
                             mPointerEvents.GetCachedPointerInfo(msg, wParam));
      DispatchPendingEvents();
      break;

    case WM_RBUTTONDBLCLK:
      result =
          DispatchMouseEvent(eMouseDoubleClick, wParam, lParam, false,
                             MouseButton::eSecondary, MOUSE_INPUT_SOURCE());
      DispatchPendingEvents();
      break;

    case WM_NCRBUTTONDOWN:
      result =
          DispatchMouseEvent(eMouseDown, 0, lParamToClient(lParam), false,
                             MouseButton::eSecondary, MOUSE_INPUT_SOURCE());
      DispatchPendingEvents();
      break;

    case WM_NCRBUTTONUP:
      result =
          DispatchMouseEvent(eMouseUp, 0, lParamToClient(lParam), false,
                             MouseButton::eSecondary, MOUSE_INPUT_SOURCE());
      DispatchPendingEvents();
      break;

    case WM_NCRBUTTONDBLCLK:
      result = DispatchMouseEvent(eMouseDoubleClick, 0, lParamToClient(lParam),
                                  false, MouseButton::eSecondary,
                                  MOUSE_INPUT_SOURCE());
      DispatchPendingEvents();
      break;

    // Windows doesn't provide to customize the behavior of 4th nor 5th button
    // of mouse.  If 5-button mouse works with standard mouse deriver of
    // Windows, users cannot disable 4th button (browser back) nor 5th button
    // (browser forward).  We should allow to do it with our prefs since we can
    // prevent Windows to generate WM_APPCOMMAND message if WM_XBUTTONUP
    // messages are not sent to DefWindowProc.
    case WM_XBUTTONDOWN:
    case WM_XBUTTONUP:
    case WM_NCXBUTTONDOWN:
    case WM_NCXBUTTONUP:
      *aRetValue = TRUE;
      switch (GET_XBUTTON_WPARAM(wParam)) {
        case XBUTTON1:
          result = !Preferences::GetBool("mousebutton.4th.enabled", true);
          break;
        case XBUTTON2:
          result = !Preferences::GetBool("mousebutton.5th.enabled", true);
          break;
        default:
          break;
      }
      break;

    case WM_SIZING: {
      if (mAspectRatio > 0) {
        LPRECT rect = (LPRECT)lParam;
        int32_t newWidth, newHeight;

        // The following conditions and switch statement borrow heavily from the
        // Chromium source code from
        // https://chromium.googlesource.com/chromium/src/+/456d6e533cfb4531995e0ef52c279d4b5aa8a352/ui/views/window/window_resize_utils.cc#45
        if (wParam == WMSZ_LEFT || wParam == WMSZ_RIGHT ||
            wParam == WMSZ_TOPLEFT || wParam == WMSZ_BOTTOMLEFT) {
          newWidth = rect->right - rect->left;
          newHeight = newWidth / mAspectRatio;
          if (newHeight < mSizeConstraints.mMinSize.height) {
            newHeight = mSizeConstraints.mMinSize.height;
            newWidth = newHeight * mAspectRatio;
          } else if (newHeight > mSizeConstraints.mMaxSize.height) {
            newHeight = mSizeConstraints.mMaxSize.height;
            newWidth = newHeight * mAspectRatio;
          }
        } else {
          newHeight = rect->bottom - rect->top;
          newWidth = newHeight * mAspectRatio;
          if (newWidth < mSizeConstraints.mMinSize.width) {
            newWidth = mSizeConstraints.mMinSize.width;
            newHeight = newWidth / mAspectRatio;
          } else if (newWidth > mSizeConstraints.mMaxSize.width) {
            newWidth = mSizeConstraints.mMaxSize.width;
            newHeight = newWidth / mAspectRatio;
          }
        }

        switch (wParam) {
          case WMSZ_RIGHT:
          case WMSZ_BOTTOM:
            rect->right = newWidth + rect->left;
            rect->bottom = rect->top + newHeight;
            break;
          case WMSZ_TOP:
            rect->right = newWidth + rect->left;
            rect->top = rect->bottom - newHeight;
            break;
          case WMSZ_LEFT:
          case WMSZ_TOPLEFT:
            rect->left = rect->right - newWidth;
            rect->top = rect->bottom - newHeight;
            break;
          case WMSZ_TOPRIGHT:
            rect->right = rect->left + newWidth;
            rect->top = rect->bottom - newHeight;
            break;
          case WMSZ_BOTTOMLEFT:
            rect->left = rect->right - newWidth;
            rect->bottom = rect->top + newHeight;
            break;
          case WMSZ_BOTTOMRIGHT:
            rect->right = rect->left + newWidth;
            rect->bottom = rect->top + newHeight;
            break;
        }
      }

      // When we get WM_ENTERSIZEMOVE we don't know yet if we're in a live
      // resize or move event. Instead we wait for first VM_SIZING message
      // within a ENTERSIZEMOVE to consider this a live resize event.
      if (mResizeState == IN_SIZEMOVE) {
        mResizeState = RESIZING;
        NotifyLiveResizeStarted();
      }
      break;
    }

    case WM_MOVING:
      FinishLiveResizing(MOVING);
      if (WinUtils::IsPerMonitorDPIAware()) {
        // Sometimes, we appear to miss a WM_DPICHANGED message while moving
        // a window around. Therefore, call ChangedDPI and ResetLayout here
        // if it appears that the window's scaling is not what we expect.
        // This causes the prescontext and appshell window management code to
        // check the appUnitsPerDevPixel value and current widget size, and
        // refresh them if necessary. If nothing has changed, these calls will
        // return without actually triggering any extra reflow or painting.
        if (WinUtils::LogToPhysFactor(mWnd) != mDefaultScale) {
          ChangedDPI();
          ResetLayout();
        }
      }
      break;

    case WM_ENTERSIZEMOVE: {
      if (mResizeState == NOT_RESIZING) {
        mResizeState = IN_SIZEMOVE;
      }
      break;
    }

    case WM_EXITSIZEMOVE: {
      FinishLiveResizing(NOT_RESIZING);

      if (!sIsInMouseCapture) {
        NotifySizeMoveDone();
      }

      // Windows spins a separate hidden event loop when moving a window so we
      // don't hear mouse events during this time and WM_EXITSIZEMOVE is fired
      // when the hidden event loop exits. We set mDraggingWindowWithMouse to
      // true in WM_NCLBUTTONDOWN when we started moving the window with the
      // mouse so we know that if mDraggingWindowWithMouse is true, we can send
      // a mouse up event.
      if (mDraggingWindowWithMouse) {
        mDraggingWindowWithMouse = false;
        result = DispatchMouseEvent(
            eMouseUp, wParam, lParam, false, MouseButton::ePrimary,
            MOUSE_INPUT_SOURCE(),
            mPointerEvents.GetCachedPointerInfo(msg, wParam));
      }

      break;
    }

    case WM_NCLBUTTONDBLCLK:
      DispatchMouseEvent(eMouseDoubleClick, 0, lParamToClient(lParam), false,
                         MouseButton::ePrimary, MOUSE_INPUT_SOURCE());
      result = DispatchMouseEvent(eMouseUp, 0, lParamToClient(lParam), false,
                                  MouseButton::ePrimary, MOUSE_INPUT_SOURCE());
      DispatchPendingEvents();
      break;

    case WM_NCLBUTTONDOWN: {
      // TODO(rkraesig): do we really need this? It should be the same as
      // wParam, and when it's not we probably don't want it here.
      auto const hitTest =
          ClientMarginHitTestPoint(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));

      // Dispatch a custom event when this happens in the draggable region, so
      // that non-popup-based panels can react to it. This doesn't send an
      // actual mousedown event because that would break dragging or interfere
      // with other mousedown handling in the caption area.
      if (hitTest == HTCAPTION) {
        DispatchCustomEvent(u"draggableregionleftmousedown"_ns);
        mDraggingWindowWithMouse = true;
      }

      if (IsWindowButton(int32_t(wParam)) && mCustomNonClient) {
        DispatchMouseEvent(eMouseDown, wParamFromGlobalMouseState(),
                           lParamToClient(lParam), false, MouseButton::ePrimary,
                           MOUSE_INPUT_SOURCE(), nullptr, IsNonclient::Yes);
        DispatchPendingEvents();
        result = true;
      }
      break;
    }

    case WM_NCLBUTTONUP: {
      if (mCustomNonClient) {
        result = DispatchMouseEvent(eMouseUp, wParamFromGlobalMouseState(),
                                    lParamToClient(lParam), false,
                                    MouseButton::ePrimary, MOUSE_INPUT_SOURCE(),
                                    nullptr, IsNonclient::Yes);
        DispatchPendingEvents();
      } else {
        result = false;
      }
      break;
    }

    case WM_APPCOMMAND: {
      MSG nativeMsg = WinUtils::InitMSG(msg, wParam, lParam, mWnd);
      result = HandleAppCommandMsg(nativeMsg, aRetValue);
      break;
    }

    // The WM_ACTIVATE event is fired when a window is raised or lowered,
    // and the loword of wParam specifies which. But we don't want to tell
    // the focus system about this until the WM_SETFOCUS or WM_KILLFOCUS
    // events are fired. Instead, set either the sJustGotActivate or
    // gJustGotDeactivate flags and activate/deactivate once the focus
    // events arrive.
    case WM_ACTIVATE: {
      int32_t fActive = LOWORD(wParam);
      if (!mWidgetListener) {
        break;
      }
      if (WA_INACTIVE == fActive) {
        // when minimizing a window, the deactivation and focus events will
        // be fired in the reverse order. Instead, just deactivate right away.
        // This can also happen when a modal system dialog is opened, so check
        // if the last window to receive the WM_KILLFOCUS message was this one
        // or a child of this one.
        if (HIWORD(wParam) ||
            (mLastKillFocusWindow &&
             (GetTopLevelForFocus(mLastKillFocusWindow) == mWnd))) {
          DispatchFocusToTopLevelWindow(false);
        } else {
          sJustGotDeactivate = true;
        }
        if (IsTopLevelWidget()) {
          mLastKeyboardLayout = KeyboardLayout::GetLayout();
        }
      } else {
        StopFlashing();

        sJustGotActivate = true;
        WidgetMouseEvent event(true, eMouseActivate, this,
                               WidgetMouseEvent::eReal);
        InitEvent(event);
        ModifierKeyState modifierKeyState;
        modifierKeyState.InitInputEvent(event);
        DispatchInputEvent(&event);
        if (sSwitchKeyboardLayout && mLastKeyboardLayout) {
          ActivateKeyboardLayout(mLastKeyboardLayout, 0);
        }

#ifdef ACCESSIBILITY
        a11y::LazyInstantiator::ResetUiaDetectionCache();
#endif
      }
    } break;

    case WM_ACTIVATEAPP: {
      // Bug 1851991: Sometimes this can be called before gfxPlatform::Init
      // when a window is created very early. In that case we just forego
      // setting this and accept the GPU process might briefly run at a lower
      // priority.
      if (GPUProcessManager::Get()) {
        GPUProcessManager::Get()->SetAppInForeground(wParam);
      }
    } break;

    case WM_MOUSEACTIVATE:
      // A popup with a parent owner should not be activated when clicked but
      // should still allow the mouse event to be fired, so the return value
      // is set to MA_NOACTIVATE. But if the owner isn't the frontmost window,
      // just use default processing so that the window is activated.
      if (IsPopup() && IsOwnerForegroundWindow()) {
        *aRetValue = MA_NOACTIVATE;
        result = true;
      }
      break;

    case WM_WINDOWPOSCHANGING: {
      LPWINDOWPOS info = (LPWINDOWPOS)lParam;
      OnWindowPosChanging(info);
      result = true;
    } break;

    // Workaround for race condition in explorer.exe.
    case MOZ_WM_FULLSCREEN_STATE_UPDATE: {
      TaskbarConcealer::OnAsyncStateUpdateRequest(mWnd);
      result = true;
    } break;

    case WM_GETMINMAXINFO: {
      MINMAXINFO* mmi = (MINMAXINFO*)lParam;
      // Set the constraints. The minimum size should also be constrained to the
      // default window maximum size so that it fits on screen.
      mmi->ptMinTrackSize.x =
          std::min((int32_t)mmi->ptMaxTrackSize.x,
                   std::max((int32_t)mmi->ptMinTrackSize.x,
                            mSizeConstraints.mMinSize.width));
      mmi->ptMinTrackSize.y =
          std::min((int32_t)mmi->ptMaxTrackSize.y,
                   std::max((int32_t)mmi->ptMinTrackSize.y,
                            mSizeConstraints.mMinSize.height));
      mmi->ptMaxTrackSize.x = std::min((int32_t)mmi->ptMaxTrackSize.x,
                                       mSizeConstraints.mMaxSize.width);
      mmi->ptMaxTrackSize.y = std::min((int32_t)mmi->ptMaxTrackSize.y,
                                       mSizeConstraints.mMaxSize.height);
    } break;

    case WM_SETFOCUS: {
      WndProcUrgentInvocation::Marker _marker;

      // If previous focused window isn't ours, it must have received the
      // redirected message.  So, we should forget it.
      if (!WinUtils::IsOurProcessWindow(HWND(wParam))) {
        RedirectedKeyDownMessageManager::Forget();
      }
      if (sJustGotActivate) {
        DispatchFocusToTopLevelWindow(true);
      }
      TaskbarConcealer::OnFocusAcquired(this);
    } break;

    case WM_KILLFOCUS:
      if (sJustGotDeactivate) {
        DispatchFocusToTopLevelWindow(false);
      } else {
        mLastKillFocusWindow = mWnd;
      }
      break;

    case WM_WINDOWPOSCHANGED: {
      WINDOWPOS* wp = (LPWINDOWPOS)lParam;
      OnWindowPosChanged(wp);
      TaskbarConcealer::OnWindowPosChanged(this);
      result = true;
    } break;

    case WM_INPUTLANGCHANGEREQUEST:
      *aRetValue = TRUE;
      result = false;
      break;

    case WM_INPUTLANGCHANGE:
      KeyboardLayout::GetInstance()->OnLayoutChange(
          reinterpret_cast<HKL>(lParam));
      nsBidiKeyboard::OnLayoutChange();
      result = false;  // always pass to child window
      break;

    case WM_DESTROYCLIPBOARD: {
      nsIClipboard* clipboard;
      nsresult rv = CallGetService(kCClipboardCID, &clipboard);
      if (NS_SUCCEEDED(rv)) {
        clipboard->EmptyClipboard(nsIClipboard::kGlobalClipboard);
        NS_RELEASE(clipboard);
      }
    } break;

#ifdef ACCESSIBILITY
    case WM_GETOBJECT: {
      *aRetValue = 0;
      // Do explicit casting to make it working on 64bit systems (see bug 649236
      // for details).
      int32_t objId = static_cast<DWORD>(lParam);
      if (objId == OBJID_CLIENT) {  // oleacc.dll will be loaded dynamically
        RefPtr<IAccessible> root(
            a11y::LazyInstantiator::GetRootAccessible(mWnd));
        if (root) {
          *aRetValue = LresultFromObject(IID_IAccessible, wParam, root);
          a11y::LazyInstantiator::EnableBlindAggregation(mWnd);
          result = true;
        }
      } else if (objId == UiaRootObjectId) {
        if (RefPtr<IRawElementProviderSimple> root =
                a11y::LazyInstantiator::GetRootUia(mWnd)) {
          *aRetValue = UiaReturnRawElementProvider(mWnd, wParam, lParam, root);
          a11y::LazyInstantiator::EnableBlindAggregation(mWnd);
          result = true;
        }
      }
    } break;
#endif

    case WM_SYSCOMMAND: {
      WPARAM const filteredWParam = (wParam & 0xFFF0);

      // SC_CLOSE may trigger a synchronous confirmation prompt. If we're in the
      // middle of something important, put off responding to it.
      if (filteredWParam == SC_CLOSE && WndProcUrgentInvocation::IsActive()) {
        ::PostMessageW(mWnd, msg, wParam, lParam);
        result = true;
        break;
      }

      if (mFrameState->GetSizeMode() == nsSizeMode_Fullscreen &&
          filteredWParam == SC_RESTORE &&
          GetCurrentShowCmd(mWnd) != SW_SHOWMINIMIZED) {
        mFrameState->EnsureFullscreenMode(false);
        result = true;
      }

      if (filteredWParam == SC_KEYMENU && lParam == VK_SPACE) {
        const auto sizeMode = mFrameState->GetSizeMode();
        // Handle the system menu manually when we're in full screen mode or
        // with custom titlebar so we can set the appropriate options.
        if (sizeMode == nsSizeMode_Fullscreen || mCustomNonClient) {
          // Historically on fullscreen windows we've used this offset from the
          // top left as our context menu position. Note that if the point we
          // supply is offscreen, Windows will still try to put our menu in the
          // right place.
          constexpr LayoutDeviceIntPoint offset(20, 20);
          auto pos = GetScreenBounds().TopLeft() + offset;
          DisplaySystemMenu(mWnd, sizeMode, mIsRTL, pos.x, pos.y);
          result = true;
        }
      }
    } break;

    case WM_DPICHANGED: {
      LPRECT rect = (LPRECT)lParam;
      OnDPIChanged(rect->left, rect->top, rect->right - rect->left,
                   rect->bottom - rect->top);
      break;
    }

    /* Gesture support events */
    case WM_TABLET_QUERYSYSTEMGESTURESTATUS:
      // According to MS samples, this must be handled to enable
      // rotational support in multi-touch drivers.
      result = true;
      *aRetValue = TABLET_ROTATE_GESTURE_ENABLE;
      break;

    case WM_TOUCH:
      result = OnTouch(wParam, lParam);
      if (result) {
        *aRetValue = 0;
      }
      break;

    case WM_GESTURE:
      result = OnGesture(wParam, lParam);
      break;

    case WM_GESTURENOTIFY: {
      // A GestureNotify event is dispatched to decide which single-finger
      // panning direction should be active (including none) and if pan
      // feedback should be displayed. Java and plugin windows can make their
      // own calls.

      GESTURENOTIFYSTRUCT* gestureinfo = (GESTURENOTIFYSTRUCT*)lParam;
      nsPointWin touchPoint;
      touchPoint = gestureinfo->ptsLocation;
      touchPoint.ScreenToClient(mWnd);
      WidgetGestureNotifyEvent gestureNotifyEvent(true, eGestureNotify, this);
      gestureNotifyEvent.mRefPoint =
          LayoutDeviceIntPoint::FromUnknownPoint(touchPoint);
      nsEventStatus status;
      DispatchEvent(&gestureNotifyEvent, status);
      mDisplayPanFeedback = gestureNotifyEvent.mDisplayPanFeedback;
      if (!mTouchWindow) {
        mGesture.SetWinGestureSupport(mWnd, gestureNotifyEvent.mPanDirection);
      }
      result = false;  // should always bubble to DefWindowProc
    } break;

    case WM_CLEAR: {
      WidgetContentCommandEvent command(true, eContentCommandDelete, this);
      DispatchWindowEvent(command);
      result = true;
    } break;

    case WM_CUT: {
      WidgetContentCommandEvent command(true, eContentCommandCut, this);
      DispatchWindowEvent(command);
      result = true;
    } break;

    case WM_COPY: {
      WidgetContentCommandEvent command(true, eContentCommandCopy, this);
      DispatchWindowEvent(command);
      result = true;
    } break;

    case WM_PASTE: {
      WidgetContentCommandEvent command(true, eContentCommandPaste, this);
      DispatchWindowEvent(command);
      result = true;
    } break;

    case EM_UNDO: {
      WidgetContentCommandEvent command(true, eContentCommandUndo, this);
      DispatchWindowEvent(command);
      *aRetValue = (LRESULT)(command.mSucceeded && command.mIsEnabled);
      result = true;
    } break;

    case EM_REDO: {
      WidgetContentCommandEvent command(true, eContentCommandRedo, this);
      DispatchWindowEvent(command);
      *aRetValue = (LRESULT)(command.mSucceeded && command.mIsEnabled);
      result = true;
    } break;

    case EM_CANPASTE: {
      // Support EM_CANPASTE message only when wParam isn't specified or
      // is plain text format.
      if (wParam == 0 || wParam == CF_TEXT || wParam == CF_UNICODETEXT) {
        WidgetContentCommandEvent command(true, eContentCommandPaste, this,
                                          true);
        DispatchWindowEvent(command);
        *aRetValue = (LRESULT)(command.mSucceeded && command.mIsEnabled);
        result = true;
      }
    } break;

    case EM_CANUNDO: {
      WidgetContentCommandEvent command(true, eContentCommandUndo, this, true);
      DispatchWindowEvent(command);
      *aRetValue = (LRESULT)(command.mSucceeded && command.mIsEnabled);
      result = true;
    } break;

    case EM_CANREDO: {
      WidgetContentCommandEvent command(true, eContentCommandRedo, this, true);
      DispatchWindowEvent(command);
      *aRetValue = (LRESULT)(command.mSucceeded && command.mIsEnabled);
      result = true;
    } break;

    case MOZ_WM_SKEWFIX: {
      TimeStamp skewStamp;
      if (CurrentWindowsTimeGetter::GetAndClearBackwardsSkewStamp(wParam,
                                                                  &skewStamp)) {
        TimeConverter().CompensateForBackwardsSkew(::GetMessageTime(),
                                                   skewStamp);
      }
    } break;

    default: {
      if (msg == nsAppShell::GetTaskbarButtonCreatedMessage()) {
        SetHasTaskbarIconBeenCreated();
      }
    } break;
  }

  //*aRetValue = result;
  if (mWnd) {
    return result;
  } else {
    // Events which caused mWnd destruction and aren't consumed
    // will crash during the Windows default processing.
    return true;
  }
}

void nsWindow::FinishLiveResizing(ResizeState aNewState) {
  if (mResizeState == RESIZING) {
    NotifyLiveResizeStopped();
  }
  mResizeState = aNewState;
  ForcePresent();
}

/**************************************************************
 *
 * SECTION: Event processing helpers
 *
 * Special processing for certain event types and
 * synthesized events.
 *
 **************************************************************/

LayoutDeviceIntMargin nsWindow::NonClientSizeMargin(
    const LayoutDeviceIntMargin& aNonClientOffset) const {
  return mCustomNonClientMetrics.DefaultMargins() - aNonClientOffset;
}

int32_t nsWindow::ClientMarginHitTestPoint(int32_t aX, int32_t aY) {
  const nsSizeMode sizeMode = mFrameState->GetSizeMode();
  if (sizeMode == nsSizeMode_Minimized || sizeMode == nsSizeMode_Fullscreen) {
    return HTCLIENT;
  }

  // Calculations are done in screen coords
  const LayoutDeviceIntRect winRect = GetScreenBounds();
  const LayoutDeviceIntPoint point(aX, aY);

  // hit return constants:
  // HTBORDER                     - non-resizable border
  // HTBOTTOM, HTLEFT, HTRIGHT, HTTOP - resizable border
  // HTBOTTOMLEFT, HTBOTTOMRIGHT  - resizable corner
  // HTTOPLEFT, HTTOPRIGHT        - resizable corner
  // HTCAPTION                    - general title bar area
  // HTCLIENT                     - area considered the client
  // HTCLOSE                      - hovering over the close button
  // HTMAXBUTTON                  - maximize button
  // HTMINBUTTON                  - minimize button

  int32_t testResult = HTCLIENT;
  const bool isResizable =
      sizeMode != nsSizeMode_Maximized &&
      (mBorderStyle &
       (BorderStyle::All | BorderStyle::ResizeH | BorderStyle::Default));

  LayoutDeviceIntMargin nonClientSizeMargin = NonClientSizeMargin();

  // Ensure being accessible to borders of window.  Even if contents are in
  // this area, the area must behave as border.
  nonClientSizeMargin.EnsureAtLeast(
      LayoutDeviceIntMargin(kResizableBorderMinSize, kResizableBorderMinSize,
                            kResizableBorderMinSize, kResizableBorderMinSize));

  LayoutDeviceIntRect clientRect = winRect;
  clientRect.Deflate(nonClientSizeMargin);

  const bool allowContentOverride =
      sizeMode == nsSizeMode_Maximized || clientRect.Contains(point);

  // The border size.  If there is no content under mouse cursor, the border
  // size should be larger than the values in system settings.  Otherwise,
  // contents under the mouse cursor should be able to override the behavior.
  // E.g., user must expect that Firefox button always opens the popup menu
  // even when the user clicks on the above edge of it.
  LayoutDeviceIntMargin borderSize = nonClientSizeMargin;
  borderSize.EnsureAtLeast(mCustomNonClientMetrics.ResizeMargins());
  // If we have a custom resize margin, check for it too.
  if (mCustomResizeMargin) {
    borderSize.EnsureAtLeast(
        LayoutDeviceIntMargin(mCustomResizeMargin, mCustomResizeMargin,
                              mCustomResizeMargin, mCustomResizeMargin));
  }

  bool top = false;
  bool bottom = false;
  bool left = false;
  bool right = false;

  if (point.y >= winRect.y && point.y < winRect.y + borderSize.top) {
    top = true;
  } else if (point.y <= winRect.YMost() &&
             point.y > winRect.YMost() - borderSize.bottom) {
    bottom = true;
  }

  // (the 2x case here doubles the resize area for corners)
  int multiplier = (top || bottom) ? 2 : 1;
  if (point.x >= winRect.x &&
      point.x < winRect.x + (multiplier * borderSize.left)) {
    left = true;
  } else if (point.x <= winRect.XMost() &&
             point.x > winRect.XMost() - (multiplier * borderSize.right)) {
    right = true;
  }

  bool inResizeRegion = false;
  if (isResizable) {
    if (top) {
      testResult = HTTOP;
      if (left) {
        testResult = HTTOPLEFT;
      } else if (right) {
        testResult = HTTOPRIGHT;
      }
    } else if (bottom) {
      testResult = HTBOTTOM;
      if (left) {
        testResult = HTBOTTOMLEFT;
      } else if (right) {
        testResult = HTBOTTOMRIGHT;
      }
    } else {
      if (left) {
        testResult = HTLEFT;
      }
      if (right) {
        testResult = HTRIGHT;
      }
    }
    inResizeRegion = (testResult != HTCLIENT);
  } else {
    if (top) {
      testResult = HTCAPTION;
    } else if (bottom || left || right) {
      testResult = HTBORDER;
    }
  }

  if (sIsInMouseCapture || !allowContentOverride) {
    return testResult;
  }

  {
    POINT pt = {aX, aY};
    ::ScreenToClient(mWnd, &pt);

    if (pt.x == mCachedHitTestPoint.x.value &&
        pt.y == mCachedHitTestPoint.y.value &&
        TimeStamp::Now() - mCachedHitTestTime <
            TimeDuration::FromMilliseconds(HITTEST_CACHE_LIFETIME_MS)) {
      return mCachedHitTestResult;
    }

    mCachedHitTestPoint = {pt.x, pt.y};
    mCachedHitTestTime = TimeStamp::Now();
  }

  auto pt = mCachedHitTestPoint;

  if (mWindowBtnRect[WindowButtonType::Minimize].Contains(pt)) {
    testResult = HTMINBUTTON;
  } else if (mWindowBtnRect[WindowButtonType::Maximize].Contains(pt)) {
#ifdef ACCESSIBILITY
    a11y::Compatibility::SuppressA11yForSnapLayouts();
#endif
    testResult = HTMAXBUTTON;
  } else if (mWindowBtnRect[WindowButtonType::Close].Contains(pt)) {
    testResult = HTCLOSE;
  } else if (!inResizeRegion) {
    // If we're in the resize region, avoid overriding that with either a
    // drag or a client result; resize takes priority over either (but not
    // over the window controls, which is why we check this after those).
    if (mDraggableRegion.Contains(pt)) {
      testResult = HTCAPTION;
    } else {
      testResult = HTCLIENT;
    }
  }

  mCachedHitTestResult = testResult;

  return testResult;
}

bool nsWindow::IsSimulatedClientArea(int32_t screenX, int32_t screenY) {
  int32_t testResult = ClientMarginHitTestPoint(screenX, screenY);
  return testResult == HTCAPTION || IsWindowButton(testResult);
}

bool nsWindow::IsWindowButton(int32_t hitTestResult) {
  return hitTestResult == HTMINBUTTON || hitTestResult == HTMAXBUTTON ||
         hitTestResult == HTCLOSE;
}

TimeStamp nsWindow::GetMessageTimeStamp(LONG aEventTime) const {
  CurrentWindowsTimeGetter getCurrentTime(mWnd);
  return TimeConverter().GetTimeStampFromSystemTime(aEventTime, getCurrentTime);
}

void nsWindow::PostSleepWakeNotification(const bool aIsSleepMode) {
  // Retain the previous mode that was notified to observers
  static bool sWasSleepMode = false;

  // Only notify observers if mode changed
  if (aIsSleepMode == sWasSleepMode) return;

  sWasSleepMode = aIsSleepMode;

  nsCOMPtr<nsIObserverService> observerService =
      mozilla::services::GetObserverService();
  if (observerService)
    observerService->NotifyObservers(nullptr,
                                     aIsSleepMode
                                         ? NS_WIDGET_SLEEP_OBSERVER_TOPIC
                                         : NS_WIDGET_WAKE_OBSERVER_TOPIC,
                                     nullptr);
}

LRESULT nsWindow::ProcessCharMessage(const MSG& aMsg, bool* aEventDispatched) {
  if (IMEHandler::IsComposingOn(this)) {
    IMEHandler::NotifyIME(this, REQUEST_TO_COMMIT_COMPOSITION);
  }
  // These must be checked here too as a lone WM_CHAR could be received
  // if a child window didn't handle it (for example Alt+Space in a content
  // window)
  ModifierKeyState modKeyState;
  NativeKey nativeKey(this, aMsg, modKeyState);
  return static_cast<LRESULT>(nativeKey.HandleCharMessage(aEventDispatched));
}

LRESULT nsWindow::ProcessKeyUpMessage(const MSG& aMsg, bool* aEventDispatched) {
  ModifierKeyState modKeyState;
  NativeKey nativeKey(this, aMsg, modKeyState);
  bool result = nativeKey.HandleKeyUpMessage(aEventDispatched);
  if (aMsg.wParam == VK_F10) {
    // Bug 1382199: Windows default behavior will trigger the System menu bar
    // when F10 is released. Among other things, this causes the System menu bar
    // to appear when a web page overrides the contextmenu event. We *never*
    // want this default behavior, so eat this key (never pass it to Windows).
    return true;
  }
  return result;
}

LRESULT nsWindow::ProcessKeyDownMessage(const MSG& aMsg,
                                        bool* aEventDispatched) {
  // If this method doesn't call NativeKey::HandleKeyDownMessage(), this method
  // must clean up the redirected message information itself.  For more
  // information, see above comment of
  // RedirectedKeyDownMessageManager::AutoFlusher class definition in
  // KeyboardLayout.h.
  RedirectedKeyDownMessageManager::AutoFlusher redirectedMsgFlusher(this, aMsg);

  ModifierKeyState modKeyState;

  NativeKey nativeKey(this, aMsg, modKeyState);
  LRESULT result =
      static_cast<LRESULT>(nativeKey.HandleKeyDownMessage(aEventDispatched));
  // HandleKeyDownMessage cleaned up the redirected message information
  // itself, so, we should do nothing.
  redirectedMsgFlusher.Cancel();

  if (aMsg.wParam == VK_MENU ||
      (aMsg.wParam == VK_F10 && !modKeyState.IsShift())) {
    // We need to let Windows handle this keypress,
    // by returning false, if there's a native menu
    // bar somewhere in our containing window hierarchy.
    // Otherwise we handle the keypress and don't pass
    // it on to Windows, by returning true.
    bool hasNativeMenu = false;
    HWND hWnd = mWnd;
    while (hWnd) {
      if (::GetMenu(hWnd)) {
        hasNativeMenu = true;
        break;
      }
      hWnd = ::GetParent(hWnd);
    }
    result = !hasNativeMenu;
  }

  return result;
}

nsresult nsWindow::SynthesizeNativeKeyEvent(
    int32_t aNativeKeyboardLayout, int32_t aNativeKeyCode,
    uint32_t aModifierFlags, const nsAString& aCharacters,
    const nsAString& aUnmodifiedCharacters, nsIObserver* aObserver) {
  AutoObserverNotifier notifier(aObserver, "keyevent");

  KeyboardLayout* keyboardLayout = KeyboardLayout::GetInstance();
  return keyboardLayout->SynthesizeNativeKeyEvent(
      this, aNativeKeyboardLayout, aNativeKeyCode, aModifierFlags, aCharacters,
      aUnmodifiedCharacters);
}

nsresult nsWindow::SynthesizeNativeMouseEvent(
    LayoutDeviceIntPoint aPoint, NativeMouseMessage aNativeMessage,
    MouseButton aButton, nsIWidget::Modifiers aModifierFlags,
    nsIObserver* aObserver) {
  AutoObserverNotifier notifier(aObserver, "mouseevent");

  INPUT input;
  memset(&input, 0, sizeof(input));

  // TODO (bug 1693240):
  // Now, we synthesize native mouse events asynchronously since we want to
  // synthesize the event on the front window at the point. However, Windows
  // does not provide a way to set modifier only while a mouse message is
  // being handled, and MOUSEEVENTF_MOVE may be coalesced by Windows.  So, we
  // need a trick for handling it.

  switch (aNativeMessage) {
    case NativeMouseMessage::Move:
      input.mi.dwFlags = MOUSEEVENTF_MOVE;
      // Reset sLastMouseMovePoint so that even if we're moving the mouse
      // to the position it's already at, we still dispatch a mousemove
      // event, because the callers of this function expect that.
      sLastMouseMovePoint = {0};
      break;
    case NativeMouseMessage::ButtonDown:
    case NativeMouseMessage::ButtonUp: {
      const bool isDown = aNativeMessage == NativeMouseMessage::ButtonDown;
      switch (aButton) {
        case MouseButton::ePrimary:
          input.mi.dwFlags = isDown ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
          break;
        case MouseButton::eMiddle:
          input.mi.dwFlags =
              isDown ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP;
          break;
        case MouseButton::eSecondary:
          input.mi.dwFlags =
              isDown ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP;
          break;
        case MouseButton::eX1:
          input.mi.dwFlags = isDown ? MOUSEEVENTF_XDOWN : MOUSEEVENTF_XUP;
          input.mi.mouseData = XBUTTON1;
          break;
        case MouseButton::eX2:
          input.mi.dwFlags = isDown ? MOUSEEVENTF_XDOWN : MOUSEEVENTF_XUP;
          input.mi.mouseData = XBUTTON2;
          break;
        default:
          return NS_ERROR_INVALID_ARG;
      }
      break;
    }
    case NativeMouseMessage::EnterWindow:
    case NativeMouseMessage::LeaveWindow:
      MOZ_ASSERT_UNREACHABLE("Non supported mouse event on Windows");
      return NS_ERROR_INVALID_ARG;
  }

  input.type = INPUT_MOUSE;
  ::SetCursorPos(aPoint.x, aPoint.y);
  ::SendInput(1, &input, sizeof(INPUT));

  return NS_OK;
}

nsresult nsWindow::SynthesizeNativeMouseScrollEvent(
    LayoutDeviceIntPoint aPoint, uint32_t aNativeMessage, double aDeltaX,
    double aDeltaY, double aDeltaZ, uint32_t aModifierFlags,
    uint32_t aAdditionalFlags, nsIObserver* aObserver) {
  AutoObserverNotifier notifier(aObserver, "mousescrollevent");
  return MouseScrollHandler::SynthesizeNativeMouseScrollEvent(
      this, aPoint, aNativeMessage,
      (aNativeMessage == WM_MOUSEWHEEL || aNativeMessage == WM_VSCROLL)
          ? static_cast<int32_t>(aDeltaY)
          : static_cast<int32_t>(aDeltaX),
      aModifierFlags, aAdditionalFlags);
}

nsresult nsWindow::SynthesizeNativeTouchpadPan(TouchpadGesturePhase aEventPhase,
                                               LayoutDeviceIntPoint aPoint,
                                               double aDeltaX, double aDeltaY,
                                               int32_t aModifierFlags,
                                               nsIObserver* aObserver) {
  AutoObserverNotifier notifier(aObserver, "touchpadpanevent");
  DirectManipulationOwner::SynthesizeNativeTouchpadPan(
      this, aEventPhase, aPoint, aDeltaX, aDeltaY, aModifierFlags);
  return NS_OK;
}

static void MaybeLogPosChanged(HWND aWnd, WINDOWPOS* wp) {
#ifdef WINSTATE_DEBUG_OUTPUT
  if (aWnd == WinUtils::GetTopLevelHWND(aWnd)) {
    MOZ_LOG(gWindowsLog, LogLevel::Info, ("*** OnWindowPosChanged: [  top] "));
  } else {
    MOZ_LOG(gWindowsLog, LogLevel::Info, ("*** OnWindowPosChanged: [child] "));
  }
  MOZ_LOG(gWindowsLog, LogLevel::Info, ("WINDOWPOS flags:"));
  if (wp->flags & SWP_FRAMECHANGED) {
    MOZ_LOG(gWindowsLog, LogLevel::Info, ("SWP_FRAMECHANGED "));
  }
  if (wp->flags & SWP_SHOWWINDOW) {
    MOZ_LOG(gWindowsLog, LogLevel::Info, ("SWP_SHOWWINDOW "));
  }
  if (wp->flags & SWP_NOSIZE) {
    MOZ_LOG(gWindowsLog, LogLevel::Info, ("SWP_NOSIZE "));
  }
  if (wp->flags & SWP_HIDEWINDOW) {
    MOZ_LOG(gWindowsLog, LogLevel::Info, ("SWP_HIDEWINDOW "));
  }
  if (wp->flags & SWP_NOZORDER) {
    MOZ_LOG(gWindowsLog, LogLevel::Info, ("SWP_NOZORDER "));
  }
  if (wp->flags & SWP_NOACTIVATE) {
    MOZ_LOG(gWindowsLog, LogLevel::Info, ("SWP_NOACTIVATE "));
  }
  MOZ_LOG(gWindowsLog, LogLevel::Info, ("\n"));
#endif
}

/**************************************************************
 *
 * SECTION: OnXXX message handlers
 *
 * For message handlers that need to be broken out or
 * implemented in specific platform code.
 *
 **************************************************************/

void nsWindow::OnWindowPosChanged(WINDOWPOS* wp) {
  if (!wp) {
    return;
  }

  MaybeLogPosChanged(mWnd, wp);

  // Handle window size mode changes
  if (wp->flags & SWP_FRAMECHANGED) {
    // Bug 566135 - Windows theme code calls show window on SW_SHOWMINIMIZED
    // windows when fullscreen games disable desktop composition. If we're
    // minimized and not being activated, ignore the event and let windows
    // handle it.
    if (mFrameState->GetSizeMode() == nsSizeMode_Minimized &&
        (wp->flags & SWP_NOACTIVATE)) {
      return;
    }

    mFrameState->OnFrameChanged();

    if (mFrameState->GetSizeMode() == nsSizeMode_Minimized) {
      // Skip window size change events below on minimization.
      return;
    }
  }

  // Notify visibility change when window is activated.
  if (!(wp->flags & SWP_NOACTIVATE) && NeedsToTrackWindowOcclusionState()) {
    WinWindowOcclusionTracker::Get()->OnWindowVisibilityChanged(
        this, mFrameState->GetSizeMode() != nsSizeMode_Minimized);
  }

  // Handle window position changes
  if (!(wp->flags & SWP_NOMOVE)) {
    mBounds.MoveTo(wp->x, wp->y);
    NotifyWindowMoved(wp->x, wp->y);
  }

  // Handle window size changes
  if (!(wp->flags & SWP_NOSIZE)) {
    RECT r;
    int32_t newWidth, newHeight;

    ::GetWindowRect(mWnd, &r);

    newWidth = r.right - r.left;
    newHeight = r.bottom - r.top;

    if (newWidth > mLastSize.width) {
      RECT drect;

      // getting wider
      drect.left = wp->x + mLastSize.width;
      drect.top = wp->y;
      drect.right = drect.left + (newWidth - mLastSize.width);
      drect.bottom = drect.top + newHeight;

      ::RedrawWindow(mWnd, &drect, nullptr,
                     RDW_INVALIDATE | RDW_NOERASE | RDW_NOINTERNALPAINT |
                         RDW_ERASENOW | RDW_ALLCHILDREN);
    }
    if (newHeight > mLastSize.height) {
      RECT drect;

      // getting taller
      drect.left = wp->x;
      drect.top = wp->y + mLastSize.height;
      drect.right = drect.left + newWidth;
      drect.bottom = drect.top + (newHeight - mLastSize.height);

      ::RedrawWindow(mWnd, &drect, nullptr,
                     RDW_INVALIDATE | RDW_NOERASE | RDW_NOINTERNALPAINT |
                         RDW_ERASENOW | RDW_ALLCHILDREN);
    }

    mBounds.SizeTo(newWidth, newHeight);
    mLastSize.width = newWidth;
    mLastSize.height = newHeight;

#ifdef WINSTATE_DEBUG_OUTPUT
    MOZ_LOG(gWindowsLog, LogLevel::Info,
            ("*** Resize window: %d x %d x %d x %d\n", wp->x, wp->y, newWidth,
             newHeight));
#endif

    if (mAspectRatio > 0) {
      // It's possible (via Windows Aero Snap) that the size of the window
      // has changed such that it violates the aspect ratio constraint. If so,
      // queue up an event to enforce the aspect ratio constraint and repaint.
      // When resized with Windows Aero Snap, we are in the NOT_RESIZING state.
      float newAspectRatio = (float)newWidth / newHeight;
      if (mResizeState == NOT_RESIZING && mAspectRatio != newAspectRatio) {
        // Hold a reference to self alive and pass it into the lambda to make
        // sure this nsIWidget stays alive long enough to run this function.
        nsCOMPtr<nsIWidget> self(this);
        NS_DispatchToMainThread(NS_NewRunnableFunction(
            "EnforceAspectRatio", [self, this, newWidth]() -> void {
              if (mWnd) {
                Resize(newWidth, newWidth / mAspectRatio, true);
              }
            }));
      }
    }

    // If a maximized window is resized, recalculate the non-client margins.
    if (mFrameState->GetSizeMode() == nsSizeMode_Maximized) {
      if (UpdateNonClientMargins(true)) {
        // gecko resize event already sent by UpdateNonClientMargins.
        return;
      }
    }
  }

  // Notify the widget listener for size change of client area for gecko
  // events. This needs to be done when either window size is changed,
  // or window frame is changed. They may not happen together.
  // However, we don't invoke that for popup when window frame changes,
  // because popups may trigger frame change before size change via
  // {Set,Clear}ThemeRegion they invoke in Resize. That would make the
  // code below call OnResize with a wrong client size first, which can
  // lead to flickerling for some popups.
  if (!(wp->flags & SWP_NOSIZE) ||
      ((wp->flags & SWP_FRAMECHANGED) && !IsPopup())) {
    RECT r;
    LayoutDeviceIntSize clientSize;
    if (::GetClientRect(mWnd, &r)) {
      clientSize = WinUtils::ToIntRect(r).Size();
    } else {
      clientSize = mBounds.Size();
    }
    // Send a gecko resize event
    OnResize(clientSize);
  }
}

void nsWindow::OnWindowPosChanging(WINDOWPOS* info) {
  // Update non-client margins if the frame size is changing, and let the
  // browser know we are changing size modes, so alternative css can kick in.
  // If we're going into fullscreen mode, ignore this, since it'll reset
  // margins to normal mode.
  if (info->flags & SWP_FRAMECHANGED && !(info->flags & SWP_NOSIZE)) {
    mFrameState->OnFrameChanging();
  }

  // Force fullscreen. This works around a bug in Windows 10 1809 where
  // using fullscreen when a window is "snapped" causes a spurious resize
  // smaller than the full screen, see bug 1482920.
  if (mFrameState->GetSizeMode() == nsSizeMode_Fullscreen &&
      !(info->flags & SWP_NOMOVE) && !(info->flags & SWP_NOSIZE)) {
    nsCOMPtr<nsIScreenManager> screenmgr =
        do_GetService(sScreenManagerContractID);
    if (screenmgr) {
      LayoutDeviceIntRect bounds(info->x, info->y, info->cx, info->cy);
      DesktopIntRect deskBounds =
          RoundedToInt(bounds / GetDesktopToDeviceScale());
      nsCOMPtr<nsIScreen> screen;
      screenmgr->ScreenForRect(deskBounds.X(), deskBounds.Y(),
                               deskBounds.Width(), deskBounds.Height(),
                               getter_AddRefs(screen));

      if (screen) {
        auto rect = screen->GetRect();
        info->x = rect.x;
        info->y = rect.y;
        info->cx = rect.width;
        info->cy = rect.height;
      }
    }
  }

  // When waking from sleep or switching out of tablet mode, Windows 10
  // Version 1809 will reopen popup windows that should be hidden. Detect
  // this case and refuse to show the window.
  static bool sDWMUnhidesPopups = IsWin10Sep2018UpdateOrLater();
  if (sDWMUnhidesPopups && (info->flags & SWP_SHOWWINDOW) &&
      mWindowType == WindowType::Popup && mWidgetListener &&
      mWidgetListener->ShouldNotBeVisible()) {
    info->flags &= ~SWP_SHOWWINDOW;
  }
}

void nsWindow::UserActivity() {
  // Check if we have the idle service, if not we try to get it.
  if (!mIdleService) {
    mIdleService = do_GetService("@mozilla.org/widget/useridleservice;1");
  }

  // Check that we now have the idle service.
  if (mIdleService) {
    mIdleService->ResetIdleTimeOut(0);
  }
}

// Helper function for TouchDeviceNeedsPanGestureConversion(PTOUCHINPUT,
// uint32_t).
static bool TouchDeviceNeedsPanGestureConversion(HANDLE aSource) {
  std::string deviceName;
  UINT dataSize = 0;
  // The first call just queries how long the name string will be.
  GetRawInputDeviceInfoA(aSource, RIDI_DEVICENAME, nullptr, &dataSize);
  if (!dataSize || dataSize > 0x10000) {
    return false;
  }
  deviceName.resize(dataSize);
  // The second call actually populates the string.
  UINT result = GetRawInputDeviceInfoA(aSource, RIDI_DEVICENAME, &deviceName[0],
                                       &dataSize);
  if (result == UINT_MAX) {
    return false;
  }
  // The affected device name is "\\?\VIRTUAL_DIGITIZER", but each backslash
  // needs to be escaped with another one.
  std::string expectedDeviceName = "\\\\?\\VIRTUAL_DIGITIZER";
  // For some reason, the dataSize returned by the first call is double the
  // actual length of the device name (as if it were returning the size of a
  // wide-character string in bytes) even though we are using the narrow
  // version of the API. For the comparison against the expected device name
  // to pass, we truncate the buffer to be no longer tha the expected device
  // name.
  if (deviceName.substr(0, expectedDeviceName.length()) != expectedDeviceName) {
    return false;
  }

  RID_DEVICE_INFO deviceInfo;
  deviceInfo.cbSize = sizeof(deviceInfo);
  dataSize = sizeof(deviceInfo);
  result =
      GetRawInputDeviceInfoA(aSource, RIDI_DEVICEINFO, &deviceInfo, &dataSize);
  if (result == UINT_MAX) {
    return false;
  }
  // The device identifiers that we check for here come from bug 1355162
  // comment 1 (see also bug 1511901 comment 35).
  return deviceInfo.dwType == RIM_TYPEHID && deviceInfo.hid.dwVendorId == 0 &&
         deviceInfo.hid.dwProductId == 0 &&
         deviceInfo.hid.dwVersionNumber == 1 &&
         deviceInfo.hid.usUsagePage == 13 && deviceInfo.hid.usUsage == 4;
}

// Determine if the touch device that originated |aOSEvent| needs to have
// touch events representing a two-finger gesture converted to pan
// gesture events.
// We only do this for touch devices with a specific name and identifiers.
static bool TouchDeviceNeedsPanGestureConversion(PTOUCHINPUT aOSEvent,
                                                 uint32_t aTouchCount) {
  if (!StaticPrefs::apz_windows_check_for_pan_gesture_conversion()) {
    return false;
  }
  if (aTouchCount == 0) {
    return false;
  }
  HANDLE source = aOSEvent[0].hSource;

  // Cache the result of this computation for each touch device.
  // Touch devices are identified by the HANDLE stored in the hSource
  // field of TOUCHINPUT.
  static std::map<HANDLE, bool> sResultCache;
  auto [iter, inserted] = sResultCache.emplace(source, false);
  if (inserted) {
    iter->second = TouchDeviceNeedsPanGestureConversion(source);
  }
  return iter->second;
}

Maybe<PanGestureInput> nsWindow::ConvertTouchToPanGesture(
    const MultiTouchInput& aTouchInput, PTOUCHINPUT aOSEvent) {
  // Checks if the touch device that originated the touch event is one
  // for which we want to convert the touch events to pang gesture events.
  bool shouldConvert = TouchDeviceNeedsPanGestureConversion(
      aOSEvent, aTouchInput.mTouches.Length());
  if (!shouldConvert) {
    return Nothing();
  }

  // Only two-finger gestures need conversion.
  if (aTouchInput.mTouches.Length() != 2) {
    return Nothing();
  }

  PanGestureInput::PanGestureType eventType = PanGestureInput::PANGESTURE_PAN;
  if (aTouchInput.mType == MultiTouchInput::MULTITOUCH_START) {
    eventType = PanGestureInput::PANGESTURE_START;
  } else if (aTouchInput.mType == MultiTouchInput::MULTITOUCH_END) {
    eventType = PanGestureInput::PANGESTURE_END;
  } else if (aTouchInput.mType == MultiTouchInput::MULTITOUCH_CANCEL) {
    eventType = PanGestureInput::PANGESTURE_CANCELLED;
  }

  // Use the midpoint of the two touches as the start point of the pan gesture.
  ScreenPoint focusPoint = (aTouchInput.mTouches[0].mScreenPoint +
                            aTouchInput.mTouches[1].mScreenPoint) /
                           2;
  // To compute the displacement of the pan gesture, we keep track of the
  // location of the previous event.
  ScreenPoint displacement = (eventType == PanGestureInput::PANGESTURE_START)
                                 ? ScreenPoint(0, 0)
                                 : (focusPoint - mLastPanGestureFocus);
  mLastPanGestureFocus = focusPoint;

  // We need to negate the displacement because for a touch event, moving the
  // fingers down results in scrolling up, but for a touchpad gesture, we want
  // moving the fingers down to result in scrolling down.
  PanGestureInput result(eventType, aTouchInput.mTimeStamp, focusPoint,
                         -displacement, aTouchInput.modifiers);
  result.mSimulateMomentum = true;

  return Some(result);
}

// Dispatch an event that originated as an OS touch event.
// Usually, we want to dispatch it as a touch event, but some touchpads
// produce touch events for two-finger scrolling, which need to be converted
// to pan gesture events for correct behaviour.
void nsWindow::DispatchTouchOrPanGestureInput(MultiTouchInput& aTouchInput,
                                              PTOUCHINPUT aOSEvent) {
  if (Maybe<PanGestureInput> panInput =
          ConvertTouchToPanGesture(aTouchInput, aOSEvent)) {
    DispatchPanGestureInput(*panInput);
    return;
  }

  DispatchTouchInput(aTouchInput);
}

bool nsWindow::OnTouch(WPARAM wParam, LPARAM lParam) {
  uint32_t cInputs = LOWORD(wParam);
  PTOUCHINPUT pInputs = new TOUCHINPUT[cInputs];

  if (GetTouchInputInfo((HTOUCHINPUT)lParam, cInputs, pInputs,
                        sizeof(TOUCHINPUT))) {
    MultiTouchInput touchInput, touchEndInput;

    // Walk across the touch point array processing each contact point.
    for (uint32_t i = 0; i < cInputs; i++) {
      bool addToEvent = false, addToEndEvent = false;

      // N.B.: According with MS documentation
      // https://msdn.microsoft.com/en-us/library/windows/desktop/dd317334(v=vs.85).aspx
      // TOUCHEVENTF_DOWN cannot be combined with TOUCHEVENTF_MOVE or
      // TOUCHEVENTF_UP.  Possibly, it means that TOUCHEVENTF_MOVE and
      // TOUCHEVENTF_UP can be combined together.

      if (pInputs[i].dwFlags & (TOUCHEVENTF_DOWN | TOUCHEVENTF_MOVE)) {
        if (touchInput.mTimeStamp.IsNull()) {
          // Initialize a touch event to send.
          touchInput.mType = MultiTouchInput::MULTITOUCH_MOVE;
          touchInput.mTimeStamp = GetMessageTimeStamp(::GetMessageTime());
          ModifierKeyState modifierKeyState;
          touchInput.modifiers = modifierKeyState.GetModifiers();
        }
        // Pres shell expects this event to be a eTouchStart
        // if any new contact points have been added since the last event sent.
        if (pInputs[i].dwFlags & TOUCHEVENTF_DOWN) {
          touchInput.mType = MultiTouchInput::MULTITOUCH_START;
        }
        addToEvent = true;
      }
      if (pInputs[i].dwFlags & TOUCHEVENTF_UP) {
        // Pres shell expects removed contacts points to be delivered in a
        // separate eTouchEnd event containing only the contact points that were
        // removed.
        if (touchEndInput.mTimeStamp.IsNull()) {
          // Initialize a touch event to send.
          touchEndInput.mType = MultiTouchInput::MULTITOUCH_END;
          touchEndInput.mTimeStamp = GetMessageTimeStamp(::GetMessageTime());
          ModifierKeyState modifierKeyState;
          touchEndInput.modifiers = modifierKeyState.GetModifiers();
        }
        addToEndEvent = true;
      }
      if (!addToEvent && !addToEndEvent) {
        // Filter out spurious Windows events we don't understand, like palm
        // contact.
        continue;
      }

      // Setup the touch point we'll append to the touch event array.
      nsPointWin touchPoint;
      touchPoint.x = TOUCH_COORD_TO_PIXEL(pInputs[i].x);
      touchPoint.y = TOUCH_COORD_TO_PIXEL(pInputs[i].y);
      touchPoint.ScreenToClient(mWnd);

      // Initialize the touch data.
      SingleTouchData touchData(
          pInputs[i].dwID,                               // aIdentifier
          ScreenIntPoint::FromUnknownPoint(touchPoint),  // aScreenPoint
          // The contact area info cannot be trusted even when
          // TOUCHINPUTMASKF_CONTACTAREA is set when the input source is pen,
          // which somehow violates the API docs. (bug 1710509) Ultimately the
          // dwFlags check will become redundant since we want to migrate to
          // WM_POINTER for pens. (bug 1707075)
          (pInputs[i].dwMask & TOUCHINPUTMASKF_CONTACTAREA) &&
                  !(pInputs[i].dwFlags & TOUCHEVENTF_PEN)
              ? ScreenSize(TOUCH_COORD_TO_PIXEL(pInputs[i].cxContact) / 2,
                           TOUCH_COORD_TO_PIXEL(pInputs[i].cyContact) / 2)
              : ScreenSize(1, 1),  // aRadius
          0.0f,                    // aRotationAngle
          0.0f);                   // aForce

      // Append touch data to the appropriate event.
      if (addToEvent) {
        touchInput.mTouches.AppendElement(touchData);
      }
      if (addToEndEvent) {
        touchEndInput.mTouches.AppendElement(touchData);
      }
    }

    // Dispatch touch start and touch move event if we have one.
    if (!touchInput.mTimeStamp.IsNull()) {
      DispatchTouchOrPanGestureInput(touchInput, pInputs);
    }
    // Dispatch touch end event if we have one.
    if (!touchEndInput.mTimeStamp.IsNull()) {
      DispatchTouchOrPanGestureInput(touchEndInput, pInputs);
    }
  }

  delete[] pInputs;
  CloseTouchInputHandle((HTOUCHINPUT)lParam);
  return true;
}

// Gesture event processing. Handles WM_GESTURE events.
bool nsWindow::OnGesture(WPARAM wParam, LPARAM lParam) {
  // Treatment for pan events which translate into scroll events:
  if (mGesture.IsPanEvent(lParam)) {
    if (!mGesture.ProcessPanMessage(mWnd, wParam, lParam))
      return false;  // ignore

    nsEventStatus status;

    WidgetWheelEvent wheelEvent(true, eWheel, this);

    ModifierKeyState modifierKeyState;
    modifierKeyState.InitInputEvent(wheelEvent);

    wheelEvent.mButton = 0;
    wheelEvent.mTimeStamp = GetMessageTimeStamp(::GetMessageTime());
    wheelEvent.mInputSource = MouseEvent_Binding::MOZ_SOURCE_TOUCH;

    bool endFeedback = true;

    if (mGesture.PanDeltaToPixelScroll(wheelEvent)) {
      DispatchEvent(&wheelEvent, status);
    }

    if (mDisplayPanFeedback) {
      mGesture.UpdatePanFeedbackX(
          mWnd, DeprecatedAbs(RoundDown(wheelEvent.mOverflowDeltaX)),
          endFeedback);
      mGesture.UpdatePanFeedbackY(
          mWnd, DeprecatedAbs(RoundDown(wheelEvent.mOverflowDeltaY)),
          endFeedback);
      mGesture.PanFeedbackFinalize(mWnd, endFeedback);
    }

    CloseGestureInfoHandle((HGESTUREINFO)lParam);

    return true;
  }

  // Other gestures translate into simple gesture events:
  WidgetSimpleGestureEvent event(true, eVoidEvent, this);
  if (!mGesture.ProcessGestureMessage(mWnd, wParam, lParam, event)) {
    return false;  // fall through to DefWndProc
  }

  // Polish up and send off the new event
  ModifierKeyState modifierKeyState;
  modifierKeyState.InitInputEvent(event);
  event.mButton = 0;
  event.mTimeStamp = GetMessageTimeStamp(::GetMessageTime());
  event.mInputSource = MouseEvent_Binding::MOZ_SOURCE_TOUCH;

  nsEventStatus status;
  DispatchEvent(&event, status);
  if (status == nsEventStatus_eIgnore) {
    return false;  // Ignored, fall through
  }

  // Only close this if we process and return true.
  CloseGestureInfoHandle((HGESTUREINFO)lParam);

  return true;  // Handled
}

// WM_DESTROY event handler
void nsWindow::OnDestroy() {
  mOnDestroyCalled = true;

  // If this is a toplevel window, notify the taskbar concealer to clean up any
  // relevant state.
  if (!mParent) {
    TaskbarConcealer::OnWindowDestroyed(mWnd);
  }

  // Make sure we don't get destroyed in the process of tearing down.
  nsCOMPtr<nsIWidget> kungFuDeathGrip(this);

  // Dispatch the destroy notification.
  if (!mInDtor) NotifyWindowDestroyed();

  // Prevent the widget from sending additional events.
  mWidgetListener = nullptr;
  mAttachedWidgetListener = nullptr;

  DestroyDirectManipulation();

  if (mWnd == mLastKillFocusWindow) {
    mLastKillFocusWindow = nullptr;
  }
  // Unregister notifications from terminal services
  ::WTSUnRegisterSessionNotification(mWnd);

  // We will stop receiving native events after dissociating from our native
  // window. We will also disappear from the output of WinUtils::GetNSWindowPtr
  // for that window.
  DissociateFromNativeWindow();

  // Once mWidgetListener is cleared and the subclass is reset, sCurrentWindow
  // can be cleared. (It's used in tracking windows for mouse events.)
  if (sCurrentWindow == this) sCurrentWindow = nullptr;

  // Disconnects us from our parent, will call our GetParent().
  nsBaseWidget::Destroy();

  // Release references to children, device context, toolkit, and app shell.
  nsBaseWidget::OnDestroy();

  // We have to destroy the native drag target before we null out our window
  // pointer.
  EnableDragDrop(false);

  // If we're going away and for some reason we're still the rollup widget,
  // rollup and turn off capture.
  nsIRollupListener* rollupListener = nsBaseWidget::GetActiveRollupListener();
  nsCOMPtr<nsIWidget> rollupWidget;
  if (rollupListener) {
    rollupWidget = rollupListener->GetRollupWidget();
  }
  if (this == rollupWidget) {
    rollupListener->Rollup({});
    CaptureRollupEvents(false);
  }

  IMEHandler::OnDestroyWindow(this);

  // Destroy any custom cursor resources.
  if (mCursor.IsCustom()) {
    SetCursor(Cursor{eCursor_standard});
  }

  if (mCompositorWidgetDelegate) {
    mCompositorWidgetDelegate->OnDestroyWindow();
  }
  mBasicLayersSurface = nullptr;

  // Finalize panning feedback to possibly restore window displacement
  mGesture.PanFeedbackFinalize(mWnd, true);

  // Clear the main HWND.
  mWnd = nullptr;
}

// Send a resize message to the listener
bool nsWindow::OnResize(const LayoutDeviceIntSize& aSize) {
  if (mCompositorWidgetDelegate &&
      !mCompositorWidgetDelegate->OnWindowResize(aSize)) {
    return false;
  }

  bool result = false;
  if (mWidgetListener) {
    result = mWidgetListener->WindowResized(this, aSize.width, aSize.height);
  }

  // If there is an attached view, inform it as well as the normal widget
  // listener.
  if (mAttachedWidgetListener) {
    return mAttachedWidgetListener->WindowResized(this, aSize.width,
                                                  aSize.height);
  }

  return result;
}

void nsWindow::OnSizeModeChange() {
  const nsSizeMode mode = mFrameState->GetSizeMode();

  MOZ_LOG(gWindowsLog, LogLevel::Info,
          ("nsWindow::OnSizeModeChange() sizeMode %d", mode));

  if (NeedsToTrackWindowOcclusionState()) {
    WinWindowOcclusionTracker::Get()->OnWindowVisibilityChanged(
        this, mode != nsSizeMode_Minimized);
  }

  if (mWidgetListener) {
    mWidgetListener->SizeModeChanged(mode);
  }
}

bool nsWindow::OnHotKey(WPARAM wParam, LPARAM lParam) { return true; }

bool nsWindow::IsPopup() { return mWindowType == WindowType::Popup; }

bool nsWindow::ShouldUseOffMainThreadCompositing() {
  if (mWindowType == WindowType::Popup && mPopupType == PopupType::Tooltip) {
    return false;
  }
  return nsBaseWidget::ShouldUseOffMainThreadCompositing();
}

void nsWindow::WindowUsesOMTC() {
  ULONG_PTR style = ::GetClassLongPtr(mWnd, GCL_STYLE);
  if (!style) {
    NS_WARNING("Could not get window class style");
    return;
  }
  style |= CS_HREDRAW | CS_VREDRAW;
  DebugOnly<ULONG_PTR> result = ::SetClassLongPtr(mWnd, GCL_STYLE, style);
  NS_WARNING_ASSERTION(result, "Could not reset window class style");
}

void nsWindow::OnDPIChanged(int32_t x, int32_t y, int32_t width,
                            int32_t height) {
  // Don't try to handle WM_DPICHANGED for popup windows (see bug 1239353);
  // they remain tied to their original parent's resolution.
  if (mWindowType == WindowType::Popup) {
    return;
  }
  if (StaticPrefs::layout_css_devPixelsPerPx() > 0.0) {
    return;
  }
  mDefaultScale = -1.0;  // force recomputation of scale factor

  if (mResizeState != RESIZING &&
      mFrameState->GetSizeMode() == nsSizeMode_Normal) {
    if (nsCOMPtr<nsIScreenManager> sm =
            do_GetService(sScreenManagerContractID)) {
      // Before getting the screen which will contain this window, we need to
      // refresh the screens because WM_DPICHANGED is sent before
      // WM_DISPLAYCHANGE.
      ScreenHelperWin::RefreshScreens();
      // Limit the position (if not in the middle of a drag-move) & size,
      // if it would overflow the destination screen
      nsCOMPtr<nsIScreen> screen;
      sm->ScreenForRect(x, y, width, height, getter_AddRefs(screen));
      if (screen) {
        int32_t availLeft, availTop, availWidth, availHeight;
        screen->GetAvailRect(&availLeft, &availTop, &availWidth, &availHeight);
        if (mResizeState != MOVING) {
          x = std::max(x, availLeft);
          y = std::max(y, availTop);
        }
        width = std::min(width, availWidth);
        height = std::min(height, availHeight);
      }
    }

    Resize(x, y, width, height, true);
  }
  UpdateNonClientMargins();
  ChangedDPI();
  ResetLayout();
}

// Callback to generate OnCloakChanged pseudo-events.
/* static */
void nsWindow::OnCloakEvent(HWND aWnd, bool aCloaked) {
  MOZ_ASSERT(NS_IsMainThread());

  const char* const kEventName = aCloaked ? "CLOAKED" : "UNCLOAKED";
  nsWindow* pWin = WinUtils::GetNSWindowPtr(aWnd);
  if (!pWin) {
    MOZ_LOG(
        sCloakingLog, LogLevel::Debug,
        ("Received %s event for HWND %p (not an nsWindow)", kEventName, aWnd));
    return;
  }

  const char* const kWasCloakedStr = pWin->mIsCloaked ? "cloaked" : "uncloaked";
  if (mozilla::IsCloaked(aWnd) == pWin->mIsCloaked) {
    MOZ_LOG(sCloakingLog, LogLevel::Debug,
            ("Received redundant %s event for %s HWND %p; discarding",
             kEventName, kWasCloakedStr, aWnd));
    return;
  }

  MOZ_LOG(
      sCloakingLog, LogLevel::Info,
      ("Received %s event for %s HWND %p", kEventName, kWasCloakedStr, aWnd));

  // Cloaking events like the one we've just received are sent asynchronously.
  // Rather than process them one-by-one, we jump the gun a bit and perform
  // updates on all newly cloaked/uncloaked nsWindows at once. This also lets us
  // batch operations that consider more than one window's state.
  struct Item {
    nsWindow* win;
    bool nowCloaked;
  };
  nsTArray<Item> changedWindows;

  mozilla::EnumerateThreadWindows([&](HWND hwnd) {
    nsWindow* pWin = WinUtils::GetNSWindowPtr(hwnd);
    if (!pWin) {
      return;
    }

    const bool isCloaked = mozilla::IsCloaked(hwnd);
    if (isCloaked != pWin->mIsCloaked) {
      changedWindows.AppendElement(Item{pWin, isCloaked});
    }
  });

  if (changedWindows.IsEmpty()) {
    return;
  }

  for (const Item& item : changedWindows) {
    item.win->OnCloakChanged(item.nowCloaked);
  }

  nsWindow::TaskbarConcealer::OnCloakChanged();
}

void nsWindow::OnCloakChanged(bool aCloaked) {
  MOZ_LOG(sCloakingLog, LogLevel::Info,
          ("Calling OnCloakChanged(): HWND %p, aCloaked %s", mWnd,
           aCloaked ? "true" : "false"));
  mIsCloaked = aCloaked;
}

/**************************************************************
 **************************************************************
 **
 ** BLOCK: IME management and accessibility
 **
 ** Handles managing IME input and accessibility.
 **
 **************************************************************
 **************************************************************/

void nsWindow::SetInputContext(const InputContext& aContext,
                               const InputContextAction& aAction) {
  InputContext newInputContext = aContext;
  IMEHandler::SetInputContext(this, newInputContext, aAction);
  mInputContext = newInputContext;
}

InputContext nsWindow::GetInputContext() {
  mInputContext.mIMEState.mOpen = IMEState::CLOSED;
  if (WinUtils::IsIMEEnabled(mInputContext) && IMEHandler::GetOpenState(this)) {
    mInputContext.mIMEState.mOpen = IMEState::OPEN;
  } else {
    mInputContext.mIMEState.mOpen = IMEState::CLOSED;
  }
  return mInputContext;
}

TextEventDispatcherListener* nsWindow::GetNativeTextEventDispatcherListener() {
  return IMEHandler::GetNativeTextEventDispatcherListener();
}

#ifdef ACCESSIBILITY
#  ifdef DEBUG
#    define NS_LOG_WMGETOBJECT(aWnd, aHwnd, aAcc)                            \
      if (a11y::logging::IsEnabled(a11y::logging::ePlatforms)) {             \
        printf(                                                              \
            "Get the window:\n  {\n     HWND: %p, parent HWND: %p, wndobj: " \
            "%p,\n",                                                         \
            aHwnd, ::GetParent(aHwnd), aWnd);                                \
        printf("     acc: %p", aAcc);                                        \
        if (aAcc) {                                                          \
          nsAutoString name;                                                 \
          aAcc->Name(name);                                                  \
          printf(", accname: %s", NS_ConvertUTF16toUTF8(name).get());        \
        }                                                                    \
        printf("\n }\n");                                                    \
      }

#  else
#    define NS_LOG_WMGETOBJECT(aWnd, aHwnd, aAcc)
#  endif

a11y::LocalAccessible* nsWindow::GetAccessible() {
  // If the pref was ePlatformIsDisabled, return null here, disabling a11y.
  if (a11y::PlatformDisabledState() == a11y::ePlatformIsDisabled)
    return nullptr;

  if (mInDtor || mOnDestroyCalled) {
    return nullptr;
  }

  // In case of popup window return a popup accessible.
  nsView* view = nsView::GetViewFor(this);
  if (view) {
    nsIFrame* frame = view->GetFrame();
    if (frame && nsLayoutUtils::IsPopup(frame)) {
      nsAccessibilityService* accService = GetOrCreateAccService();
      if (accService) {
        a11y::DocAccessible* docAcc =
            GetAccService()->GetDocAccessible(frame->PresShell());
        if (docAcc) {
          NS_LOG_WMGETOBJECT(
              this, mWnd,
              docAcc->GetAccessibleOrDescendant(frame->GetContent()));
          return docAcc->GetAccessibleOrDescendant(frame->GetContent());
        }
      }
    }
  }

  // otherwise root document accessible.
  NS_LOG_WMGETOBJECT(this, mWnd, GetRootAccessible());
  return GetRootAccessible();
}
#endif

void nsWindow::SetTransparencyMode(TransparencyMode aMode) {
  if (aMode == mTransparencyMode || DestroyCalled()) {
    return;
  }

  MOZ_ASSERT(WinUtils::GetTopLevelHWND(mWnd, true) == mWnd);
  MOZ_ASSERT(GetTopLevelWindow(true) == this);

  mTransparencyMode = aMode;

  UpdateOpaqueRegionInternal();

  if (mCompositorWidgetDelegate) {
    mCompositorWidgetDelegate->UpdateTransparency(aMode);
  }
}

void nsWindow::UpdateOpaqueRegion(const LayoutDeviceIntRegion& aRegion) {
  if (aRegion == mOpaqueRegion || IsPopup()) {
    // Popups don't track opaque region changes since our opaque region
    // tracking is, let's say, suboptimal (see bug 1933952).
    return;
  }
  mOpaqueRegion = aRegion;
  UpdateOpaqueRegionInternal();
}

LayoutDeviceIntRegion nsWindow::GetTranslucentRegion() {
  if (mTransparencyMode != TransparencyMode::Transparent) {
    return {};
  }
  const auto clientRect =
      LayoutDeviceIntRect(LayoutDeviceIntPoint(), GetClientSize());
  LayoutDeviceIntRegion translucentRegion{clientRect};
  translucentRegion.SubOut(mOpaqueRegion);
  return translucentRegion;
}

void nsWindow::MaybeInvalidateTranslucentRegion() {
  if (mTransparencyMode != TransparencyMode::Transparent) {
    return;
  }
  const auto translucent = GetTranslucentRegion();
  if (translucent.IsEmpty() || mClearedRegion.Contains(translucent)) {
    return;
  }
  // We need to clear some part of the window that isn't cleared already, make
  // sure we trigger a WM_PAINT message.
  //
  // NOTE(emilio): we could provide a finer grained region here (i.e., only
  // invalidate the translucent region, or even only the bits that are not yet
  // cleared), but we don't do much with that region in OnPaint message so this
  // seems fine for now.
  ::RedrawWindow(mWnd, nullptr, nullptr, RDW_INVALIDATE | RDW_INTERNALPAINT);
}

void nsWindow::UpdateOpaqueRegionInternal() {
  MARGINS margins{0};
  if (mTransparencyMode == TransparencyMode::Transparent) {
    // If there is no opaque region, set margins to support a full sheet of
    // glass. Comments in MSDN indicate all values must be set to -1 to get a
    // full sheet of glass.
    margins = {-1, -1, -1, -1};
    if (!mOpaqueRegion.IsEmpty()) {
      LayoutDeviceIntRect clientBounds = GetClientBounds();
      // Find the largest rectangle and use that to calculate the inset.
      LayoutDeviceIntRect largest = mOpaqueRegion.GetLargestRectangle();
      margins.cxLeftWidth = largest.X();
      margins.cxRightWidth = clientBounds.Width() - largest.XMost();
      margins.cyBottomHeight = clientBounds.Height() - largest.YMost();
      margins.cyTopHeight = largest.Y();

      auto ncmargin = NonClientSizeMargin();
      margins.cxLeftWidth += ncmargin.left;
      margins.cyTopHeight += ncmargin.top;
      margins.cxRightWidth += ncmargin.right;
      margins.cyBottomHeight += ncmargin.bottom;
    }
  }
  DwmExtendFrameIntoClientArea(mWnd, &margins);
  MaybeInvalidateTranslucentRegion();
}

/**************************************************************
 **************************************************************
 **
 ** BLOCK: Popup rollup hooks
 **
 ** Deals with CaptureRollup on popup windows.
 **
 **************************************************************
 **************************************************************/

// Schedules a timer for a window, so we can rollup after processing the hook
// event
void nsWindow::ScheduleHookTimer(HWND aWnd, UINT aMsgId) {
  // In some cases multiple hooks may be scheduled
  // so ignore any other requests once one timer is scheduled
  if (sHookTimerId == 0) {
    // Remember the window handle and the message ID to be used later
    sRollupMsgId = aMsgId;
    sRollupMsgWnd = aWnd;
    // Schedule native timer for doing the rollup after
    // this event is done being processed
    sHookTimerId = ::SetTimer(nullptr, 0, 0, (TIMERPROC)HookTimerForPopups);
    NS_ASSERTION(sHookTimerId, "Timer couldn't be created.");
  }
}

#ifdef POPUP_ROLLUP_DEBUG_OUTPUT
int gLastMsgCode = 0;
extern MSGFEventMsgInfo gMSGFEvents[];
#endif

// Process Menu messages, rollup when popup is clicked.
LRESULT CALLBACK nsWindow::MozSpecialMsgFilter(int code, WPARAM wParam,
                                               LPARAM lParam) {
#ifdef POPUP_ROLLUP_DEBUG_OUTPUT
  if (sProcessHook) {
    MSG* pMsg = (MSG*)lParam;

    int inx = 0;
    while (gMSGFEvents[inx].mId != code && gMSGFEvents[inx].mStr != nullptr) {
      inx++;
    }
    if (code != gLastMsgCode) {
      if (gMSGFEvents[inx].mId == code) {
#  ifdef DEBUG
        MOZ_LOG(gWindowsLog, LogLevel::Info,
                ("MozSpecialMessageProc - code: 0x%X  - %s  hw: %p\n", code,
                 gMSGFEvents[inx].mStr, pMsg->hwnd));
#  endif
      } else {
#  ifdef DEBUG
        MOZ_LOG(gWindowsLog, LogLevel::Info,
                ("MozSpecialMessageProc - code: 0x%X  - %d  hw: %p\n", code,
                 gMSGFEvents[inx].mId, pMsg->hwnd));
#  endif
      }
      gLastMsgCode = code;
    }
    PrintEvent(pMsg->message, FALSE, FALSE);
  }
#endif  // #ifdef POPUP_ROLLUP_DEBUG_OUTPUT

  if (sProcessHook && code == MSGF_MENU) {
    MSG* pMsg = (MSG*)lParam;
    ScheduleHookTimer(pMsg->hwnd, pMsg->message);
  }

  return ::CallNextHookEx(sMsgFilterHook, code, wParam, lParam);
}

// Process all mouse messages. Roll up when a click is in a native window
// that doesn't have an nsIWidget.
LRESULT CALLBACK nsWindow::MozSpecialMouseProc(int code, WPARAM wParam,
                                               LPARAM lParam) {
  if (sProcessHook) {
    switch (wParam) {
      case WM_LBUTTONDOWN:
      case WM_RBUTTONDOWN:
      case WM_MBUTTONDOWN:
      case WM_MOUSEWHEEL:
      case WM_MOUSEHWHEEL: {
        MOUSEHOOKSTRUCT* ms = (MOUSEHOOKSTRUCT*)lParam;
        nsIWidget* mozWin = WinUtils::GetNSWindowPtr(ms->hwnd);
        if (!mozWin) {
          ScheduleHookTimer(ms->hwnd, (UINT)wParam);
        }
        break;
      }
    }
  }
  return ::CallNextHookEx(sCallMouseHook, code, wParam, lParam);
}

// Process all messages. Roll up when the window is moving, or
// is resizing or when maximized or mininized.
LRESULT CALLBACK nsWindow::MozSpecialWndProc(int code, WPARAM wParam,
                                             LPARAM lParam) {
#ifdef POPUP_ROLLUP_DEBUG_OUTPUT
  if (sProcessHook) {
    CWPSTRUCT* cwpt = (CWPSTRUCT*)lParam;
    PrintEvent(cwpt->message, FALSE, FALSE);
  }
#endif

  if (sProcessHook) {
    CWPSTRUCT* cwpt = (CWPSTRUCT*)lParam;
    if (cwpt->message == WM_MOVING || cwpt->message == WM_SIZING ||
        cwpt->message == WM_GETMINMAXINFO) {
      ScheduleHookTimer(cwpt->hwnd, (UINT)cwpt->message);
    }
  }

  return ::CallNextHookEx(sCallProcHook, code, wParam, lParam);
}

// Register the special "hooks" for dropdown processing.
void nsWindow::RegisterSpecialDropdownHooks() {
  NS_ASSERTION(!sMsgFilterHook, "sMsgFilterHook must be NULL!");
  NS_ASSERTION(!sCallProcHook, "sCallProcHook must be NULL!");

  DISPLAY_NMM_PRT("***************** Installing Msg Hooks ***************\n");

  // Install msg hook for moving the window and resizing
  if (!sMsgFilterHook) {
    DISPLAY_NMM_PRT("***** Hooking sMsgFilterHook!\n");
    sMsgFilterHook = SetWindowsHookEx(WH_MSGFILTER, MozSpecialMsgFilter,
                                      nullptr, GetCurrentThreadId());
#ifdef POPUP_ROLLUP_DEBUG_OUTPUT
    if (!sMsgFilterHook) {
      MOZ_LOG(gWindowsLog, LogLevel::Info,
              ("***** SetWindowsHookEx is NOT installed for WH_MSGFILTER!\n"));
    }
#endif
  }

  // Install msg hook for menus
  if (!sCallProcHook) {
    DISPLAY_NMM_PRT("***** Hooking sCallProcHook!\n");
    sCallProcHook = SetWindowsHookEx(WH_CALLWNDPROC, MozSpecialWndProc, nullptr,
                                     GetCurrentThreadId());
#ifdef POPUP_ROLLUP_DEBUG_OUTPUT
    if (!sCallProcHook) {
      MOZ_LOG(
          gWindowsLog, LogLevel::Info,
          ("***** SetWindowsHookEx is NOT installed for WH_CALLWNDPROC!\n"));
    }
#endif
  }

  // Install msg hook for the mouse
  if (!sCallMouseHook) {
    DISPLAY_NMM_PRT("***** Hooking sCallMouseHook!\n");
    sCallMouseHook = SetWindowsHookEx(WH_MOUSE, MozSpecialMouseProc, nullptr,
                                      GetCurrentThreadId());
#ifdef POPUP_ROLLUP_DEBUG_OUTPUT
    if (!sCallMouseHook) {
      MOZ_LOG(gWindowsLog, LogLevel::Info,
              ("***** SetWindowsHookEx is NOT installed for WH_MOUSE!\n"));
    }
#endif
  }
}

// Unhook special message hooks for dropdowns.
void nsWindow::UnregisterSpecialDropdownHooks() {
  DISPLAY_NMM_PRT(
      "***************** De-installing Msg Hooks ***************\n");

  if (sCallProcHook) {
    DISPLAY_NMM_PRT("***** Unhooking sCallProcHook!\n");
    if (!::UnhookWindowsHookEx(sCallProcHook)) {
      DISPLAY_NMM_PRT("***** UnhookWindowsHookEx failed for sCallProcHook!\n");
    }
    sCallProcHook = nullptr;
  }

  if (sMsgFilterHook) {
    DISPLAY_NMM_PRT("***** Unhooking sMsgFilterHook!\n");
    if (!::UnhookWindowsHookEx(sMsgFilterHook)) {
      DISPLAY_NMM_PRT("***** UnhookWindowsHookEx failed for sMsgFilterHook!\n");
    }
    sMsgFilterHook = nullptr;
  }

  if (sCallMouseHook) {
    DISPLAY_NMM_PRT("***** Unhooking sCallMouseHook!\n");
    if (!::UnhookWindowsHookEx(sCallMouseHook)) {
      DISPLAY_NMM_PRT("***** UnhookWindowsHookEx failed for sCallMouseHook!\n");
    }
    sCallMouseHook = nullptr;
  }
}

// This timer is designed to only fire one time at most each time a "hook"
// function is used to rollup the dropdown. In some cases, the timer may be
// scheduled from the hook, but that hook event or a subsequent event may roll
// up the dropdown before this timer function is executed.
//
// For example, if an MFC control takes focus, the combobox will lose focus and
// rollup before this function fires.
VOID CALLBACK nsWindow::HookTimerForPopups(HWND hwnd, UINT uMsg, UINT idEvent,
                                           DWORD dwTime) {
  if (sHookTimerId != 0) {
    // if the window is nullptr then we need to use the ID to kill the timer
    DebugOnly<BOOL> status = ::KillTimer(nullptr, sHookTimerId);
    NS_ASSERTION(status, "Hook Timer was not killed.");
    sHookTimerId = 0;
  }

  if (sRollupMsgId != 0) {
    // Note: DealWithPopups does the check to make sure that the rollup widget
    // is set.
    LRESULT popupHandlingResult;
    nsAutoRollup autoRollup;
    DealWithPopups(sRollupMsgWnd, sRollupMsgId, 0, 0, &popupHandlingResult);
    sRollupMsgId = 0;
    sRollupMsgWnd = nullptr;
  }
}

static bool IsDifferentThreadWindow(HWND aWnd) {
  return ::GetCurrentThreadId() != ::GetWindowThreadProcessId(aWnd, nullptr);
}

// static
bool nsWindow::EventIsInsideWindow(nsWindow* aWindow,
                                   Maybe<POINT> aEventPoint) {
  RECT r;
  ::GetWindowRect(aWindow->mWnd, &r);
  POINT mp;
  if (aEventPoint) {
    mp = *aEventPoint;
  } else {
    DWORD pos = ::GetMessagePos();
    mp.x = GET_X_LPARAM(pos);
    mp.y = GET_Y_LPARAM(pos);
  }

  auto margin = aWindow->mInputRegion.mMargin;
  if (margin > 0) {
    r.top += margin;
    r.bottom -= margin;
    r.left += margin;
    r.right -= margin;
  }

  // was the event inside this window?
  return static_cast<bool>(::PtInRect(&r, mp));
}

// static
bool nsWindow::GetPopupsToRollup(nsIRollupListener* aRollupListener,
                                 uint32_t* aPopupsToRollup,
                                 Maybe<POINT> aEventPoint) {
  // If we're dealing with menus, we probably have submenus and we don't want
  // to rollup some of them if the click is in a parent menu of the current
  // submenu.
  *aPopupsToRollup = UINT32_MAX;
  AutoTArray<nsIWidget*, 5> widgetChain;
  uint32_t sameTypeCount = aRollupListener->GetSubmenuWidgetChain(&widgetChain);
  for (uint32_t i = 0; i < widgetChain.Length(); ++i) {
    nsIWidget* widget = widgetChain[i];
    if (EventIsInsideWindow(static_cast<nsWindow*>(widget), aEventPoint)) {
      // Don't roll up if the mouse event occurred within a menu of the
      // same type. If the mouse event occurred in a menu higher than that,
      // roll up, but pass the number of popups to Rollup so that only those
      // of the same type close up.
      if (i < sameTypeCount) {
        return false;
      }

      *aPopupsToRollup = sameTypeCount;
      break;
    }
  }
  return true;
}

static bool IsTouchSupportEnabled(HWND aWnd) {
  nsWindow* topWindow =
      WinUtils::GetNSWindowPtr(WinUtils::GetTopLevelHWND(aWnd, true));
  return topWindow ? topWindow->IsTouchWindow() : false;
}

static Maybe<POINT> GetSingleTouch(WPARAM wParam, LPARAM lParam) {
  Maybe<POINT> ret;
  uint32_t cInputs = LOWORD(wParam);
  if (cInputs != 1) {
    return ret;
  }
  TOUCHINPUT input;
  if (GetTouchInputInfo((HTOUCHINPUT)lParam, cInputs, &input,
                        sizeof(TOUCHINPUT))) {
    ret.emplace();
    ret->x = TOUCH_COORD_TO_PIXEL(input.x);
    ret->y = TOUCH_COORD_TO_PIXEL(input.y);
  }
  // Note that we don't call CloseTouchInputHandle here because we need
  // to read the touch input info again in OnTouch later.
  return ret;
}

// static
bool nsWindow::DealWithPopups(HWND aWnd, UINT aMessage, WPARAM aWParam,
                              LPARAM aLParam, LRESULT* aResult) {
  NS_ASSERTION(aResult, "Bad outResult");

  // XXX Why do we use the return value of WM_MOUSEACTIVATE for all messages?
  *aResult = MA_NOACTIVATE;

  if (!::IsWindowVisible(aWnd)) {
    return false;
  }

  if (MOZ_UNLIKELY(aMessage == WM_KILLFOCUS)) {
    // NOTE: We deal with this here rather than on the switch below because we
    // want to do this even if there are no menus to rollup (tooltips don't set
    // the rollup listener etc).
    if (RefPtr pm = nsXULPopupManager::GetInstance()) {
      pm->RollupTooltips();
    }
  }

  nsIRollupListener* rollupListener = nsBaseWidget::GetActiveRollupListener();
  NS_ENSURE_TRUE(rollupListener, false);

  nsCOMPtr<nsIWidget> popup = rollupListener->GetRollupWidget();
  if (!popup) {
    return false;
  }

  uint32_t popupsToRollup = UINT32_MAX;

  bool consumeRollupEvent = false;
  Maybe<POINT> touchPoint;  // In screen coords.

  // If we rollup with animations but get occluded right away, we might not
  // advance the refresh driver enough for the animation to finish.
  auto allowAnimations = nsIRollupListener::AllowAnimations::Yes;
  nsWindow* popupWindow = static_cast<nsWindow*>(popup.get());
  switch (aMessage) {
    case WM_TOUCH:
      if (!IsTouchSupportEnabled(aWnd)) {
        // If APZ is disabled, don't allow touch inputs to dismiss popups. The
        // compatibility mouse events will do it instead.
        return false;
      }
      touchPoint = GetSingleTouch(aWParam, aLParam);
      if (!touchPoint) {
        return false;
      }
      [[fallthrough]];
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN:
    case WM_NCLBUTTONDOWN:
    case WM_NCRBUTTONDOWN:
    case WM_NCMBUTTONDOWN:
      if (aMessage != WM_TOUCH && IsTouchSupportEnabled(aWnd) &&
          MOUSE_INPUT_SOURCE() == MouseEvent_Binding::MOZ_SOURCE_TOUCH) {
        // If any of these mouse events are really compatibility events that
        // Windows is sending for touch inputs, then don't allow them to dismiss
        // popups when APZ is enabled (instead we do the dismissing as part of
        // WM_TOUCH handling which is more correct).
        // If we don't do this, then when the user lifts their finger after a
        // long-press, the WM_RBUTTONDOWN compatibility event that Windows sends
        // us will dismiss the contextmenu popup that we displayed as part of
        // handling the long-tap-up.
        return false;
      }
      if (!EventIsInsideWindow(popupWindow, touchPoint) &&
          GetPopupsToRollup(rollupListener, &popupsToRollup, touchPoint)) {
        break;
      }
      return false;
    case WM_POINTERDOWN: {
      WinPointerEvents pointerEvents;
      if (!pointerEvents.ShouldRollupOnPointerEvent(aMessage, aWParam)) {
        return false;
      }
      POINT pt;
      pt.x = GET_X_LPARAM(aLParam);
      pt.y = GET_Y_LPARAM(aLParam);
      if (!GetPopupsToRollup(rollupListener, &popupsToRollup, Some(pt))) {
        return false;
      }
      if (EventIsInsideWindow(popupWindow, Some(pt))) {
        // Don't roll up if the event is inside the popup window.
        return false;
      }
    } break;
    case MOZ_WM_DMANIP: {
      POINT pt;
      ::GetCursorPos(&pt);
      if (!GetPopupsToRollup(rollupListener, &popupsToRollup, Some(pt))) {
        return false;
      }
      if (EventIsInsideWindow(popupWindow, Some(pt))) {
        // Don't roll up if the event is inside the popup window
        return false;
      }
    } break;
    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL:
      // We need to check if the popup thinks that it should cause closing
      // itself when mouse wheel events are fired outside the rollup widget.
      if (!EventIsInsideWindow(popupWindow)) {
        // Check if we should consume this event even if we don't roll-up:
        consumeRollupEvent = rollupListener->ShouldConsumeOnMouseWheelEvent();
        *aResult = MA_ACTIVATE;
        if (rollupListener->ShouldRollupOnMouseWheelEvent() &&
            GetPopupsToRollup(rollupListener, &popupsToRollup)) {
          break;
        }
      }
      return consumeRollupEvent;

    case WM_ACTIVATEAPP:
      allowAnimations = nsIRollupListener::AllowAnimations::No;
      break;

    case WM_ACTIVATE: {
      // This marker should be useless nowadays, but kept just for safety, see
      // the discussion in D210302. See also bug 1842170.
      WndProcUrgentInvocation::Marker _marker;

      nsWindow* window = WinUtils::GetNSWindowPtr(aWnd);
      nsWindow* prevWindow =
          WinUtils::GetNSWindowPtr(reinterpret_cast<HWND>(aLParam));
      // Don't rollup popups for WM_ACTIVATE from/to a popup.
      // When we click on a popup (WA_CLICKACTIVE) we don't want to do it.
      // WA_ACTIVE/WA_INACTIVE shouldn't really happen, but some old
      // pre-windows-10 drivers used to do this, see bug 953146.
      // It might be the case that is no longer needed tho, and we can move
      // this to the WA_CLICKACTIVE condition.
      if ((window && window->IsPopup()) ||
          (prevWindow && prevWindow->IsPopup())) {
        return false;
      }
      if (LOWORD(aWParam) == WA_CLICKACTIVE &&
          !GetPopupsToRollup(rollupListener, &popupsToRollup)) {
        return false;
      }
      allowAnimations = nsIRollupListener::AllowAnimations::No;
    } break;

    case WM_MOUSEACTIVATE:
      if (!EventIsInsideWindow(popupWindow) &&
          GetPopupsToRollup(rollupListener, &popupsToRollup)) {
        // WM_MOUSEACTIVATE may be caused by moving the mouse (e.g., X-mouse
        // of TweakUI is enabled. Then, check if the popup should be rolled up
        // with rollup listener. If not, just consume the message.
        if (HIWORD(aLParam) == WM_MOUSEMOVE &&
            !rollupListener->ShouldRollupOnMouseActivate()) {
          return true;
        }
        // Otherwise, it should be handled by wndproc.
        return false;
      }

      // Prevent the click inside the popup from causing a change in window
      // activation. Since the popup is shown non-activated, we need to eat any
      // requests to activate the window while it is displayed. Windows will
      // automatically activate the popup on the mousedown otherwise.
      return true;

    case WM_SHOWWINDOW:
      // If the window is being minimized, close popups.
      if (aLParam == SW_PARENTCLOSING) {
        allowAnimations = nsIRollupListener::AllowAnimations::No;
        break;
      }
      return false;

    case WM_KILLFOCUS:
      // If focus moves to other window created in different process/thread,
      // e.g., a plugin window, popups should be rolled up.
      if (IsDifferentThreadWindow(reinterpret_cast<HWND>(aWParam))) {
        allowAnimations = nsIRollupListener::AllowAnimations::No;
        break;
      }
      return false;

    case WM_MOVING:
    case WM_MENUSELECT:
      break;

    default:
      return false;
  }

  // Only need to deal with the last rollup for left mouse down events.
  NS_ASSERTION(!nsAutoRollup::GetLastRollup(), "last rollup is null");

  nsIRollupListener::RollupOptions rollupOptions{
      popupsToRollup,
      nsIRollupListener::FlushViews::Yes,
      /* mPoint = */ nullptr,
      allowAnimations,
  };

  if (aMessage == WM_TOUCH || aMessage == WM_LBUTTONDOWN ||
      aMessage == WM_POINTERDOWN) {
    LayoutDeviceIntPoint pos;
    if (aMessage == WM_TOUCH) {
      pos.x = touchPoint->x;
      pos.y = touchPoint->y;
    } else {
      POINT pt;
      pt.x = GET_X_LPARAM(aLParam);
      pt.y = GET_Y_LPARAM(aLParam);
      // POINTERDOWN is already in screen coords.
      if (aMessage == WM_LBUTTONDOWN) {
        ::ClientToScreen(aWnd, &pt);
      }
      pos = LayoutDeviceIntPoint(pt.x, pt.y);
    }

    rollupOptions.mPoint = &pos;
    nsIContent* lastRollup = nullptr;
    consumeRollupEvent = rollupListener->Rollup(rollupOptions, &lastRollup);
    nsAutoRollup::SetLastRollup(lastRollup);
  } else {
    consumeRollupEvent = rollupListener->Rollup(rollupOptions);
  }

  // Tell hook to stop processing messages
  sProcessHook = false;
  sRollupMsgId = 0;
  sRollupMsgWnd = nullptr;

  // If we are NOT supposed to be consuming events, let it go through
  if (consumeRollupEvent && aMessage != WM_RBUTTONDOWN) {
    *aResult = MA_ACTIVATE;
    return true;
  }

  return false;
}

/**************************************************************
 **************************************************************
 **
 ** BLOCK: Misc. utility methods and functions.
 **
 ** General use.
 **
 **************************************************************
 **************************************************************/

// Note that the result of GetTopLevelWindow method can be different from the
// result of WinUtils::GetTopLevelHWND().  The result can be non-floating
// window.  Because our top level window may be contained in another window
// which is not managed by us.
nsWindow* nsWindow::GetTopLevelWindow(bool aStopOnDialogOrPopup) {
  nsWindow* curWindow = this;

  while (true) {
    if (aStopOnDialogOrPopup) {
      switch (curWindow->mWindowType) {
        case WindowType::Dialog:
        case WindowType::Popup:
          return curWindow;
        default:
          break;
      }
    }

    // Retrieve the top level parent or owner window
    nsWindow* parentWindow = curWindow->GetParentWindow(true);

    if (!parentWindow) return curWindow;

    curWindow = parentWindow;
  }
}

// Set a flag if hwnd is a (non-popup) visible window from this process,
// and bail out of the enumeration. Otherwise leave the flag unmodified
// and continue the enumeration.
// lParam must be a bool* pointing at the flag to be set.
static BOOL CALLBACK EnumVisibleWindowsProc(HWND hwnd, LPARAM lParam) {
  DWORD pid;
  ::GetWindowThreadProcessId(hwnd, &pid);
  if (pid == ::GetCurrentProcessId() && ::IsWindowVisible(hwnd)) {
    // Don't count popups as visible windows, since they don't take focus,
    // in case we only have a popup visible (see bug 1554490 where the gfx
    // test window is an offscreen popup).
    nsWindow* window = WinUtils::GetNSWindowPtr(hwnd);
    if (!window || !window->IsPopup()) {
      bool* windowsVisible = reinterpret_cast<bool*>(lParam);
      *windowsVisible = true;
      return FALSE;
    }
  }
  return TRUE;
}

// Determine if it would be ok to activate a window, taking focus.
// We want to avoid stealing focus from another app (bug 225305).
bool nsWindow::CanTakeFocus() {
  HWND fgWnd = ::GetForegroundWindow();
  if (!fgWnd) {
    // There is no foreground window, so don't worry about stealing focus.
    return true;
  }
  // We can take focus if the current foreground window is already from
  // this process.
  DWORD pid;
  ::GetWindowThreadProcessId(fgWnd, &pid);
  if (pid == ::GetCurrentProcessId()) {
    return true;
  }

  bool windowsVisible = false;
  ::EnumWindows(EnumVisibleWindowsProc,
                reinterpret_cast<LPARAM>(&windowsVisible));

  if (!windowsVisible) {
    // We're probably creating our first visible window, allow that to
    // take focus.
    return true;
  }
  return false;
}

static const wchar_t* GetMainWindowClass() {
  static const wchar_t* sMainWindowClass = nullptr;
  if (!sMainWindowClass) {
    nsAutoString className;
    Preferences::GetString("ui.window_class_override", className);
    if (!className.IsEmpty()) {
      sMainWindowClass = wcsdup(className.get());
    } else {
      sMainWindowClass = kClassNameGeneral;
    }
  }
  return sMainWindowClass;
}

LPARAM nsWindow::lParamToScreen(LPARAM lParam) {
  POINT pt;
  pt.x = GET_X_LPARAM(lParam);
  pt.y = GET_Y_LPARAM(lParam);
  ::ClientToScreen(mWnd, &pt);
  return MAKELPARAM(pt.x, pt.y);
}

LPARAM nsWindow::lParamToClient(LPARAM lParam) {
  POINT pt;
  pt.x = GET_X_LPARAM(lParam);
  pt.y = GET_Y_LPARAM(lParam);
  ::ScreenToClient(mWnd, &pt);
  return MAKELPARAM(pt.x, pt.y);
}

WPARAM nsWindow::wParamFromGlobalMouseState() {
  WPARAM result = 0;

  if (!!::GetKeyState(VK_CONTROL)) {
    result |= MK_CONTROL;
  }

  if (!!::GetKeyState(VK_SHIFT)) {
    result |= MK_SHIFT;
  }

  if (!!::GetKeyState(VK_LBUTTON)) {
    result |= MK_LBUTTON;
  }

  if (!!::GetKeyState(VK_MBUTTON)) {
    result |= MK_MBUTTON;
  }

  if (!!::GetKeyState(VK_RBUTTON)) {
    result |= MK_RBUTTON;
  }

  if (!!::GetKeyState(VK_XBUTTON1)) {
    result |= MK_XBUTTON1;
  }

  if (!!::GetKeyState(VK_XBUTTON2)) {
    result |= MK_XBUTTON2;
  }

  return result;
}

// WORKAROUND FOR UNDOCUMENTED BEHAVIOR: `IFileDialog::Show` disables the
// top-level ancestor of its provided owner-window. If the modal window's
// container process crashes, it will never get a chance to undo that.
//
// For simplicity's sake we simply unconditionally perform both the disabling
// and reenabling here, synchronously, on the main thread, rather than leaving
// it to happen in our asynchronously-operated IFileDialog.

void nsWindow::PickerOpen() {
  AssertIsOnMainThread();

  // Disable the root-level window synchronously before any file-dialogs get a
  // chance to fight over doing it asynchronously.
  if (!mPickerDisplayCount) {
    ::EnableWindow(::GetAncestor(GetWindowHandle(), GA_ROOT), FALSE);
  }

  mPickerDisplayCount++;
}

void nsWindow::PickerClosed() {
  AssertIsOnMainThread();
  NS_ASSERTION(mPickerDisplayCount > 0, "mPickerDisplayCount out of sync!");
  if (!mPickerDisplayCount) return;
  mPickerDisplayCount--;

  // Once all the file-dialogs are gone, reenable the root-level window.
  if (!mPickerDisplayCount) {
    ::EnableWindow(::GetAncestor(GetWindowHandle(), GA_ROOT), TRUE);
    DispatchFocusToTopLevelWindow(true);
  }

  if (!mPickerDisplayCount && mDestroyCalled) {
    Destroy();
  }
}

bool nsWindow::WidgetTypeSupportsAcceleration() { return true; }

bool nsWindow::DispatchTouchEventFromWMPointer(
    UINT msg, LPARAM aLParam, const WinPointerInfo& aPointerInfo,
    mozilla::MouseButton aButton) {
  MultiTouchInput::MultiTouchType touchType;
  switch (msg) {
    case WM_POINTERDOWN:
      touchType = MultiTouchInput::MULTITOUCH_START;
      break;
    case WM_POINTERUPDATE:
      if (aPointerInfo.mPressure == 0) {
        return false;  // hover
      }
      touchType = MultiTouchInput::MULTITOUCH_MOVE;
      break;
    case WM_POINTERUP:
      touchType = MultiTouchInput::MULTITOUCH_END;
      break;
    default:
      return false;
  }

  nsPointWin touchPoint;
  touchPoint.x = GET_X_LPARAM(aLParam);
  touchPoint.y = GET_Y_LPARAM(aLParam);
  touchPoint.ScreenToClient(mWnd);

  SingleTouchData touchData(static_cast<int32_t>(aPointerInfo.pointerId),
                            ScreenIntPoint::FromUnknownPoint(touchPoint),
                            ScreenSize(1, 1),  // pixel size radius for pen
                            0.0f,              // no radius rotation
                            aPointerInfo.mPressure);
  touchData.mTiltX = aPointerInfo.tiltX;
  touchData.mTiltY = aPointerInfo.tiltY;
  touchData.mTwist = aPointerInfo.twist;

  MultiTouchInput touchInput;
  touchInput.mType = touchType;
  touchInput.mTimeStamp = GetMessageTimeStamp(::GetMessageTime());
  touchInput.mTouches.AppendElement(touchData);
  touchInput.mButton = aButton;
  touchInput.mButtons = aPointerInfo.mButtons;
  touchInput.mInputSource = MouseEvent_Binding::MOZ_SOURCE_PEN;

  // POINTER_INFO.dwKeyStates can't be used as it only supports Shift and Ctrl
  ModifierKeyState modifierKeyState;
  touchInput.modifiers = modifierKeyState.GetModifiers();

  DispatchTouchInput(touchInput);
  return true;
}

static MouseButton PenFlagsToMouseButton(PEN_FLAGS aPenFlags) {
  // Theoretically flags can be set together but they do not
  if (aPenFlags & PEN_FLAG_BARREL) {
    return MouseButton::eSecondary;
  }
  if (aPenFlags & PEN_FLAG_ERASER) {
    return MouseButton::eEraser;
  }
  return MouseButton::ePrimary;
}

bool nsWindow::OnPointerEvents(UINT msg, WPARAM aWParam, LPARAM aLParam) {
  if (!mAPZC) {
    // APZ is not available on context menu. Follow the behavior of touch input
    // which fallbacks to WM_LBUTTON* and WM_GESTURE, to keep consistency.
    return false;
  }

  uint32_t pointerId = mPointerEvents.GetPointerId(aWParam);
  POINTER_INPUT_TYPE pointerType = PT_POINTER;
  if (!GetPointerType(pointerId, &pointerType)) {
    MOZ_ASSERT(false, "cannot find PointerType");
    return false;
  }

  if (pointerType == PT_TOUCH) {
    if (!StaticPrefs::
            dom_w3c_pointer_events_dispatch_by_pointer_messages_touch()) {
      return false;
    }
    return OnTouchPointerEvents(pointerId, msg, aWParam, aLParam);
  }

  if (pointerType == PT_PEN) {
    return OnPenPointerEvents(pointerId, msg, aWParam, aLParam);
  }

  return false;
}

bool nsWindow::OnPenPointerEvents(uint32_t aPointerId, UINT aMsg,
                                  WPARAM aWParam, LPARAM aLParam) {
  if (!mPointerEvents.ShouldFirePointerEventByWinPointerMessages()) {
    // We have to handle WM_POINTER* to fetch and cache pen related information
    // and fire WidgetMouseEvent with the cached information the WM_*BUTTONDOWN
    // handler. This is because Windows doesn't support ::DoDragDrop in the
    // touch or pen message handlers.
    mPointerEvents.ConvertAndCachePointerInfo(aMsg, aWParam);
    // Don't consume the Windows WM_POINTER* messages
    return false;
  }

  POINTER_PEN_INFO penInfo{};
  if (!mPointerEvents.GetPointerPenInfo(aPointerId, &penInfo)) {
    return false;
  }

  // When dispatching mouse events with pen, there may be some
  // WM_POINTERUPDATE messages between WM_POINTERDOWN and WM_POINTERUP with
  // small movements. Those events will reset sLastMousePoint and reset
  // sLastClickCount. To prevent that, we keep the last pen down position
  // and compare it with the subsequent WM_POINTERUPDATE. If the movement is
  // smaller than GetSystemMetrics(SM_CXDRAG), then we suppress firing
  // eMouseMove for WM_POINTERUPDATE.
  static POINT sLastPointerDownPoint = {0};

  // We don't support chorded buttons for pen. Keep the button at
  // WM_POINTERDOWN.
  static mozilla::MouseButton sLastPenDownButton = MouseButton::ePrimary;
  static bool sPointerDown = false;

  EventMessage message;
  mozilla::MouseButton button = MouseButton::ePrimary;
  switch (aMsg) {
    case WM_POINTERDOWN: {
      LayoutDeviceIntPoint eventPoint(GET_X_LPARAM(aLParam),
                                      GET_Y_LPARAM(aLParam));
      sLastPointerDownPoint.x = eventPoint.x;
      sLastPointerDownPoint.y = eventPoint.y;
      message = eMouseDown;
      button = PenFlagsToMouseButton(penInfo.penFlags);
      sLastPenDownButton = button;
      sPointerDown = true;
    } break;
    case WM_POINTERUP:
      message = eMouseUp;
      MOZ_ASSERT(sPointerDown, "receive WM_POINTERUP w/o WM_POINTERDOWN");
      button = sPointerDown ? sLastPenDownButton : MouseButton::ePrimary;
      sPointerDown = false;
      break;
    case WM_POINTERUPDATE:
      message = eMouseMove;
      if (sPointerDown) {
        LayoutDeviceIntPoint eventPoint(GET_X_LPARAM(aLParam),
                                        GET_Y_LPARAM(aLParam));
        int32_t movementX = sLastPointerDownPoint.x > eventPoint.x
                                ? sLastPointerDownPoint.x - eventPoint.x.value
                                : eventPoint.x.value - sLastPointerDownPoint.x;
        int32_t movementY = sLastPointerDownPoint.y > eventPoint.y
                                ? sLastPointerDownPoint.y - eventPoint.y.value
                                : eventPoint.y.value - sLastPointerDownPoint.y;
        bool insideMovementThreshold =
            movementX < (int32_t)::GetSystemMetrics(SM_CXDRAG) &&
            movementY < (int32_t)::GetSystemMetrics(SM_CYDRAG);

        if (insideMovementThreshold) {
          // Suppress firing eMouseMove for WM_POINTERUPDATE if the movement
          // from last WM_POINTERDOWN is smaller than SM_CXDRAG / SM_CYDRAG
          return false;
        }
        button = sLastPenDownButton;
      }
      break;
    case WM_POINTERLEAVE:
      message = eMouseExitFromWidget;
      break;
    default:
      return false;
  }

  // Windows defines the pen pressure is normalized to a range between 0 and
  // 1024. Convert it to float.
  float pressure = penInfo.pressure ? (float)penInfo.pressure / 1024 : 0;
  int16_t buttons = sPointerDown
                        ? nsContentUtils::GetButtonsFlagForButton(button)
                        : static_cast<int16_t>(MouseButtonsFlag::eNoButtons);
  WinPointerInfo pointerInfo(aPointerId, penInfo.tiltX, penInfo.tiltY, pressure,
                             buttons);
  // Per
  // https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-pointer_pen_info,
  // the rotation is normalized in a range of 0 to 359.
  MOZ_ASSERT(penInfo.rotation <= 359);
  pointerInfo.twist = (int32_t)penInfo.rotation;

  // Fire touch events but not when the barrel button is pressed.
  if (button != MouseButton::eSecondary &&
      StaticPrefs::dom_w3c_pointer_events_scroll_by_pen_enabled() &&
      DispatchTouchEventFromWMPointer(aMsg, aLParam, pointerInfo, button)) {
    return true;
  }

  // The aLParam of WM_POINTER* is the screen location. Convert it to client
  // location
  LPARAM newLParam = lParamToClient(aLParam);
  DispatchMouseEvent(message, aWParam, newLParam, false, button,
                     MouseEvent_Binding::MOZ_SOURCE_PEN, &pointerInfo);

  if (button == MouseButton::eSecondary && message == eMouseUp) {
    // Fire eContextMenu manually since consuming WM_POINTER* blocks
    // WM_CONTEXTMENU
    DispatchMouseEvent(eContextMenu, aWParam, newLParam, false, button,
                       MouseEvent_Binding::MOZ_SOURCE_PEN, &pointerInfo);
  }
  // Consume WM_POINTER* to stop Windows fires WM_*BUTTONDOWN / WM_*BUTTONUP
  // WM_MOUSEMOVE.
  return true;
}

bool nsWindow::OnTouchPointerEvents(uint32_t aPointerId, UINT aMsg,
                                    WPARAM aWParam, LPARAM aLParam) {
  MultiTouchInput::MultiTouchType touchType;
  switch (aMsg) {
    case WM_POINTERDOWN:
      touchType = MultiTouchInput::MULTITOUCH_START;
      break;
    case WM_POINTERUPDATE:
      touchType = MultiTouchInput::MULTITOUCH_MOVE;
      break;
    case WM_POINTERUP:
      touchType = MultiTouchInput::MULTITOUCH_END;
      break;
    default:
      return false;
  }

  nsTArray<POINTER_TOUCH_INFO> touchInfoArray{};
  mPointerEvents.GetPointerFrameTouchInfo(aPointerId, touchInfoArray);
  if (touchInfoArray.IsEmpty()) {
    return false;
  }

  MultiTouchInput inputToDispatch;
  inputToDispatch.mInputType = MULTITOUCH_INPUT;
  inputToDispatch.mType = touchType;
  inputToDispatch.mTimeStamp = GetMessageTimeStamp(::GetMessageTime());

  for (const POINTER_TOUCH_INFO& touchInfo : touchInfoArray) {
    ScreenSize size(static_cast<float>(touchInfo.rcContact.right -
                                       touchInfo.rcContact.left),
                    static_cast<float>(touchInfo.rcContact.bottom -
                                       touchInfo.rcContact.top));

    nsPointWin touchPoint;
    touchPoint.x = touchInfo.pointerInfo.ptPixelLocation.x;
    touchPoint.y = touchInfo.pointerInfo.ptPixelLocation.y;
    touchPoint.ScreenToClient(mWnd);

    // Windows provides orientation info, but the behavior differs from
    // TouchEvent because TouchEvent's angle rotates the elliptic contact region
    // while the Windows provided orientation is independent from touch point
    // rect.
    //
    // e.g. For a vertically long touch pointer, Windows would give vertically
    // long rect and also give a 90 degree orientation, and passing both would
    // incorrectly represent a horizontal ellipse.
    //
    // See also: https://w3c.github.io/touch-events/#dom-touch-rotationangle
    // "The angle (in degrees) that the ellipse described by radiusX and radiusY
    // is rotated clockwise about its center; 0 if no value is known."
    float angle = 0.0f;

    bool hasPressure = !!(touchInfo.touchMask & TOUCH_MASK_PRESSURE);
    float pressure = hasPressure ? (float)touchInfo.pressure / 1024 : 0;
    inputToDispatch.mTouches.AppendElement(
        SingleTouchData(static_cast<int32_t>(touchInfo.pointerInfo.pointerId),
                        ScreenIntPoint::FromUnknownPoint(touchPoint), size / 2,
                        angle, pressure));
  }

  DispatchTouchInput(inputToDispatch);
  return true;
}

void nsWindow::GetCompositorWidgetInitData(
    mozilla::widget::CompositorWidgetInitData* aInitData) {
  *aInitData = WinCompositorWidgetInitData(
      reinterpret_cast<uintptr_t>(mWnd),
      reinterpret_cast<uintptr_t>(static_cast<nsIWidget*>(this)),
      mTransparencyMode);
}

bool nsWindow::SynchronouslyRepaintOnResize() { return false; }

void nsWindow::MaybeDispatchInitialFocusEvent() {
  if (mIsShowingPreXULSkeletonUI && ::GetActiveWindow() == mWnd) {
    DispatchFocusToTopLevelWindow(true);
  }
}

already_AddRefed<nsIWidget> nsIWidget::CreateTopLevelWindow() {
  nsCOMPtr<nsIWidget> window = new nsWindow();
  return window.forget();
}

already_AddRefed<nsIWidget> nsIWidget::CreateChildWindow() {
  nsCOMPtr<nsIWidget> window = new nsWindow();
  return window.forget();
}

// static
bool nsWindow::InitTouchInjection() {
  if (!sTouchInjectInitialized) {
    // Initialize touch injection on the first call
    HMODULE hMod = LoadLibraryW(kUser32LibName);
    if (!hMod) {
      return false;
    }

    InitializeTouchInjectionPtr func =
        (InitializeTouchInjectionPtr)GetProcAddress(hMod,
                                                    "InitializeTouchInjection");
    if (!func) {
      WinUtils::Log("InitializeTouchInjection not available.");
      return false;
    }

    if (!func(TOUCH_INJECT_MAX_POINTS, TOUCH_FEEDBACK_DEFAULT)) {
      WinUtils::Log("InitializeTouchInjection failure. GetLastError=%d",
                    GetLastError());
      return false;
    }

    sInjectTouchFuncPtr =
        (InjectTouchInputPtr)GetProcAddress(hMod, "InjectTouchInput");
    if (!sInjectTouchFuncPtr) {
      WinUtils::Log("InjectTouchInput not available.");
      return false;
    }
    sTouchInjectInitialized = true;
  }
  return true;
}

bool nsWindow::InjectTouchPoint(uint32_t aId, LayoutDeviceIntPoint& aPoint,
                                POINTER_FLAGS aFlags, uint32_t aPressure,
                                uint32_t aOrientation) {
  if (aId > TOUCH_INJECT_MAX_POINTS) {
    WinUtils::Log("Pointer ID exceeds maximum. See TOUCH_INJECT_MAX_POINTS.");
    return false;
  }

  POINTER_TOUCH_INFO info{};

  info.touchFlags = TOUCH_FLAG_NONE;
  info.touchMask =
      TOUCH_MASK_CONTACTAREA | TOUCH_MASK_ORIENTATION | TOUCH_MASK_PRESSURE;
  info.pressure = aPressure;
  info.orientation = aOrientation;

  info.pointerInfo.pointerFlags = aFlags;
  info.pointerInfo.pointerType = PT_TOUCH;
  info.pointerInfo.pointerId = aId;
  info.pointerInfo.ptPixelLocation.x = aPoint.x;
  info.pointerInfo.ptPixelLocation.y = aPoint.y;

  info.rcContact.top = info.pointerInfo.ptPixelLocation.y - 2;
  info.rcContact.bottom = info.pointerInfo.ptPixelLocation.y + 2;
  info.rcContact.left = info.pointerInfo.ptPixelLocation.x - 2;
  info.rcContact.right = info.pointerInfo.ptPixelLocation.x + 2;

  for (int i = 0; i < 3; i++) {
    if (sInjectTouchFuncPtr(1, &info)) {
      break;
    }
    DWORD error = GetLastError();
    if (error == ERROR_NOT_READY && i < 2) {
      // We sent it too quickly after the previous injection (see bug 1535140
      // comment 10). On the first loop iteration we just yield (via Sleep(0))
      // and try again. If it happens again on the second loop iteration we
      // explicitly Sleep(1) and try again. If that doesn't work either we just
      // error out.
      ::Sleep(i);
      continue;
    }
    WinUtils::Log("InjectTouchInput failure. GetLastError=%d", error);
    return false;
  }
  return true;
}

void nsWindow::ChangedDPI() {
  if (mWidgetListener) {
    if (PresShell* presShell = mWidgetListener->GetPresShell()) {
      presShell->BackingScaleFactorChanged();
    }
  }
  NotifyAPZOfDPIChange();
}

static Result<POINTER_FLAGS, nsresult> PointerStateToFlag(
    nsWindow::TouchPointerState aPointerState, bool isUpdate) {
  bool hover = aPointerState & nsWindow::TOUCH_HOVER;
  bool contact = aPointerState & nsWindow::TOUCH_CONTACT;
  bool remove = aPointerState & nsWindow::TOUCH_REMOVE;
  bool cancel = aPointerState & nsWindow::TOUCH_CANCEL;

  POINTER_FLAGS flags;
  if (isUpdate) {
    // We know about this pointer, send an update
    flags = POINTER_FLAG_UPDATE;
    if (hover) {
      flags |= POINTER_FLAG_INRANGE;
    } else if (contact) {
      flags |= POINTER_FLAG_INCONTACT | POINTER_FLAG_INRANGE;
    } else if (remove) {
      flags = POINTER_FLAG_UP;
    }

    if (cancel) {
      flags |= POINTER_FLAG_CANCELED;
    }
  } else {
    // Missing init state, error out
    if (remove || cancel) {
      return Err(NS_ERROR_INVALID_ARG);
    }

    // Create a new pointer
    flags = POINTER_FLAG_INRANGE;
    if (contact) {
      flags |= POINTER_FLAG_INCONTACT | POINTER_FLAG_DOWN;
    }
  }
  return flags;
}

nsresult nsWindow::SynthesizeNativeTouchPoint(
    uint32_t aPointerId, nsIWidget::TouchPointerState aPointerState,
    LayoutDeviceIntPoint aPoint, double aPointerPressure,
    uint32_t aPointerOrientation, nsIObserver* aObserver) {
  AutoObserverNotifier notifier(aObserver, "touchpoint");

  if (StaticPrefs::apz_test_fails_with_native_injection() ||
      !InitTouchInjection()) {
    // If we don't have touch injection from the OS, or if we are running a test
    // that cannot properly inject events to satisfy the OS requirements (see
    // bug 1313170)  we can just fake it and synthesize the events from here.
    MOZ_ASSERT(NS_IsMainThread());
    if (aPointerState == TOUCH_HOVER) {
      return NS_ERROR_UNEXPECTED;
    }

    if (!mSynthesizedTouchInput) {
      mSynthesizedTouchInput = MakeUnique<MultiTouchInput>();
    }

    WidgetEventTime time = CurrentMessageWidgetEventTime();
    LayoutDeviceIntPoint pointInWindow = aPoint - WidgetToScreenOffset();
    MultiTouchInput inputToDispatch = UpdateSynthesizedTouchState(
        mSynthesizedTouchInput.get(), time.mTimeStamp, aPointerId,
        aPointerState, pointInWindow, aPointerPressure, aPointerOrientation);
    DispatchTouchInput(inputToDispatch);
    return NS_OK;
  }

  // win api expects a value from 0 to 1024. aPointerPressure is a value
  // from 0.0 to 1.0.
  uint32_t pressure = (uint32_t)ceil(aPointerPressure * 1024);

  // If we already know about this pointer id get it's record
  return mActivePointers.WithEntryHandle(aPointerId, [&](auto&& entry) {
    POINTER_FLAGS flags;
    // Can't use MOZ_TRY_VAR because it confuses WithEntryHandle
    auto result = PointerStateToFlag(aPointerState, !!entry);
    if (result.isOk()) {
      flags = result.unwrap();
    } else {
      return result.unwrapErr();
    }

    if (!entry) {
      entry.Insert(MakeUnique<PointerInfo>(aPointerId, aPoint,
                                           PointerInfo::PointerType::TOUCH));
    } else {
      if (entry.Data()->mType != PointerInfo::PointerType::TOUCH) {
        return NS_ERROR_UNEXPECTED;
      }
      if (aPointerState & TOUCH_REMOVE) {
        // Remove the pointer from our tracking list. This is UniquePtr wrapped,
        // so shouldn't leak.
        entry.Remove();
      }
    }

    return !InjectTouchPoint(aPointerId, aPoint, flags, pressure,
                             aPointerOrientation)
               ? NS_ERROR_UNEXPECTED
               : NS_OK;
  });
}

nsresult nsWindow::ClearNativeTouchSequence(nsIObserver* aObserver) {
  AutoObserverNotifier notifier(aObserver, "cleartouch");
  if (!sTouchInjectInitialized) {
    return NS_OK;
  }

  // cancel all input points
  for (auto iter = mActivePointers.Iter(); !iter.Done(); iter.Next()) {
    auto* info = iter.UserData();
    if (info->mType != PointerInfo::PointerType::TOUCH) {
      continue;
    }
    InjectTouchPoint(info->mPointerId, info->mPosition, POINTER_FLAG_CANCELED);
    iter.Remove();
  }

  nsBaseWidget::ClearNativeTouchSequence(nullptr);

  return NS_OK;
}

#if !defined(NTDDI_WIN10_RS5) || (NTDDI_VERSION < NTDDI_WIN10_RS5)
static CreateSyntheticPointerDevicePtr CreateSyntheticPointerDevice;
static DestroySyntheticPointerDevicePtr DestroySyntheticPointerDevice;
static InjectSyntheticPointerInputPtr InjectSyntheticPointerInput;
#endif
static HSYNTHETICPOINTERDEVICE sSyntheticPenDevice;

static bool InitPenInjection() {
  if (sSyntheticPenDevice) {
    return true;
  }
#if !defined(NTDDI_WIN10_RS5) || (NTDDI_VERSION < NTDDI_WIN10_RS5)
  HMODULE hMod = LoadLibraryW(kUser32LibName);
  if (!hMod) {
    return false;
  }
  CreateSyntheticPointerDevice =
      (CreateSyntheticPointerDevicePtr)GetProcAddress(
          hMod, "CreateSyntheticPointerDevice");
  if (!CreateSyntheticPointerDevice) {
    WinUtils::Log("CreateSyntheticPointerDevice not available.");
    return false;
  }
  DestroySyntheticPointerDevice =
      (DestroySyntheticPointerDevicePtr)GetProcAddress(
          hMod, "DestroySyntheticPointerDevice");
  if (!DestroySyntheticPointerDevice) {
    WinUtils::Log("DestroySyntheticPointerDevice not available.");
    return false;
  }
  InjectSyntheticPointerInput = (InjectSyntheticPointerInputPtr)GetProcAddress(
      hMod, "InjectSyntheticPointerInput");
  if (!InjectSyntheticPointerInput) {
    WinUtils::Log("InjectSyntheticPointerInput not available.");
    return false;
  }
#endif
  sSyntheticPenDevice =
      CreateSyntheticPointerDevice(PT_PEN, 1, POINTER_FEEDBACK_DEFAULT);
  return !!sSyntheticPenDevice;
}

nsresult nsWindow::SynthesizeNativePenInput(
    uint32_t aPointerId, nsIWidget::TouchPointerState aPointerState,
    LayoutDeviceIntPoint aPoint, double aPressure, uint32_t aRotation,
    int32_t aTiltX, int32_t aTiltY, int32_t aButton, nsIObserver* aObserver) {
  AutoObserverNotifier notifier(aObserver, "peninput");
  if (!InitPenInjection()) {
    return NS_ERROR_UNEXPECTED;
  }

  // win api expects a value from 0 to 1024. aPointerPressure is a value
  // from 0.0 to 1.0.
  uint32_t pressure = (uint32_t)ceil(aPressure * 1024);

  // If we already know about this pointer id get it's record
  return mActivePointers.WithEntryHandle(aPointerId, [&](auto&& entry) {
    POINTER_FLAGS flags;
    // Can't use MOZ_TRY_VAR because it confuses WithEntryHandle
    auto result = PointerStateToFlag(aPointerState, !!entry);
    if (result.isOk()) {
      flags = result.unwrap();
    } else {
      return result.unwrapErr();
    }

    if (!entry) {
      entry.Insert(MakeUnique<PointerInfo>(aPointerId, aPoint,
                                           PointerInfo::PointerType::PEN));
    } else {
      if (entry.Data()->mType != PointerInfo::PointerType::PEN) {
        return NS_ERROR_UNEXPECTED;
      }
      if (aPointerState & TOUCH_REMOVE) {
        // Remove the pointer from our tracking list. This is UniquePtr wrapped,
        // so shouldn't leak.
        entry.Remove();
      }
    }

    POINTER_TYPE_INFO info{};

    info.type = PT_PEN;
    info.penInfo.pointerInfo.pointerType = PT_PEN;
    info.penInfo.pointerInfo.pointerFlags = flags;
    info.penInfo.pointerInfo.pointerId = aPointerId;
    info.penInfo.pointerInfo.ptPixelLocation.x = aPoint.x;
    info.penInfo.pointerInfo.ptPixelLocation.y = aPoint.y;

    info.penInfo.penFlags = PEN_FLAG_NONE;
    // PEN_FLAG_ERASER is not supported this way, unfortunately.
    if (aButton == 2) {
      info.penInfo.penFlags |= PEN_FLAG_BARREL;
    }
    info.penInfo.penMask = PEN_MASK_PRESSURE | PEN_MASK_ROTATION |
                           PEN_MASK_TILT_X | PEN_MASK_TILT_Y;
    info.penInfo.pressure = pressure;
    info.penInfo.rotation = aRotation;
    info.penInfo.tiltX = aTiltX;
    info.penInfo.tiltY = aTiltY;

    return InjectSyntheticPointerInput(sSyntheticPenDevice, &info, 1)
               ? NS_OK
               : NS_ERROR_UNEXPECTED;
  });
};

bool nsWindow::HandleAppCommandMsg(const MSG& aAppCommandMsg,
                                   LRESULT* aRetValue) {
  ModifierKeyState modKeyState;
  NativeKey nativeKey(this, aAppCommandMsg, modKeyState);
  bool consumed = nativeKey.HandleAppCommandMessage();
  *aRetValue = consumed ? 1 : 0;
  return consumed;
}

#ifdef DEBUG
nsresult nsWindow::SetHiDPIMode(bool aHiDPI) {
  return WinUtils::SetHiDPIMode(aHiDPI);
}

nsresult nsWindow::RestoreHiDPIMode() { return WinUtils::RestoreHiDPIMode(); }
#endif

mozilla::Maybe<UINT> nsWindow::GetHiddenTaskbarEdge() {
  HMONITOR windowMonitor = ::MonitorFromWindow(mWnd, MONITOR_DEFAULTTONEAREST);

  // Check all four sides of our monitor for an appbar.  Skip any that aren't
  // the system taskbar.
  MONITORINFO mi;
  mi.cbSize = sizeof(MONITORINFO);
  ::GetMonitorInfo(windowMonitor, &mi);

  APPBARDATA appBarData;
  appBarData.cbSize = sizeof(appBarData);
  appBarData.rc = mi.rcMonitor;
  const auto kEdges = {ABE_BOTTOM, ABE_TOP, ABE_LEFT, ABE_RIGHT};
  for (auto edge : kEdges) {
    appBarData.uEdge = edge;
    HWND appBarHwnd = (HWND)SHAppBarMessage(ABM_GETAUTOHIDEBAREX, &appBarData);
    if (appBarHwnd) {
      nsAutoString className;
      if (WinUtils::GetClassName(appBarHwnd, className)) {
        if (className.Equals(L"Shell_TrayWnd") ||
            className.Equals(L"Shell_SecondaryTrayWnd")) {
          return Some(edge);
        }
      }
    }
  }

  return Nothing();
}

static nsSizeMode GetSizeModeForWindowFrame(HWND aWnd, bool aFullscreenMode) {
  WINDOWPLACEMENT pl;
  pl.length = sizeof(pl);
  ::GetWindowPlacement(aWnd, &pl);

  if (pl.showCmd == SW_SHOWMINIMIZED) {
    return nsSizeMode_Minimized;
  } else if (aFullscreenMode) {
    return nsSizeMode_Fullscreen;
  } else if (pl.showCmd == SW_SHOWMAXIMIZED) {
    return nsSizeMode_Maximized;
  } else {
    return nsSizeMode_Normal;
  }
}

static void ShowWindowWithMode(HWND aWnd, nsSizeMode aMode) {
  // This will likely cause a callback to
  // nsWindow::FrameState::{OnFrameChanging() and OnFrameChanged()}
  switch (aMode) {
    case nsSizeMode_Fullscreen:
      ::ShowWindow(aWnd, SW_SHOW);
      break;

    case nsSizeMode_Maximized:
      ::ShowWindow(aWnd, SW_MAXIMIZE);
      break;

    case nsSizeMode_Minimized:
      ::ShowWindow(aWnd, SW_MINIMIZE);
      break;

    default:
      // Don't call ::ShowWindow if we're trying to "restore" a window that is
      // already in a normal state.  Prevents a bug where snapping to one side
      // of the screen and then minimizing would cause Windows to forget our
      // window's correct restored position/size.
      if (GetCurrentShowCmd(aWnd) != SW_SHOWNORMAL) {
        ::ShowWindow(aWnd, SW_RESTORE);
      }
  }
}

nsWindow::FrameState::FrameState(nsWindow* aWindow) : mWindow(aWindow) {}

nsSizeMode nsWindow::FrameState::GetSizeMode() const { return mSizeMode; }

void nsWindow::FrameState::CheckInvariant() const {
  MOZ_ASSERT(mSizeMode >= 0 && mSizeMode < nsSizeMode_Invalid);
  MOZ_ASSERT(mPreFullscreenSizeMode >= 0 &&
             mPreFullscreenSizeMode < nsSizeMode_Invalid);
  MOZ_ASSERT(mWindow);

  // We should never observe fullscreen sizemode unless fullscreen is enabled
  MOZ_ASSERT_IF(mSizeMode == nsSizeMode_Fullscreen, mFullscreenMode);
  MOZ_ASSERT_IF(!mFullscreenMode, mSizeMode != nsSizeMode_Fullscreen);

  // Something went wrong if we somehow saved fullscreen mode when we are
  // changing into fullscreen mode
  MOZ_ASSERT(mPreFullscreenSizeMode != nsSizeMode_Fullscreen);
}

void nsWindow::FrameState::ConsumePreXULSkeletonState(bool aWasMaximized) {
  mSizeMode = aWasMaximized ? nsSizeMode_Maximized : nsSizeMode_Normal;
}

void nsWindow::FrameState::EnsureSizeMode(nsSizeMode aMode,
                                          DoShowWindow aDoShowWindow) {
  if (mSizeMode == aMode) {
    return;
  }

  if (StaticPrefs::widget_windows_fullscreen_remind_taskbar()) {
    // If we're unminimizing a window, asynchronously notify the taskbar after
    // the message has been processed. This redundant notification works around
    // a race condition in explorer.exe. (See bug 1835851, or comments in
    // TaskbarConcealer.)
    //
    // Note that we notify regardless of `aMode`: unminimizing a non-fullscreen
    // window can also affect the correct taskbar state, yet fail to affect the
    // current taskbar state.
    if (mSizeMode == nsSizeMode_Minimized) {
      ::PostMessage(mWindow->mWnd, MOZ_WM_FULLSCREEN_STATE_UPDATE, 0, 0);
    }
  }

  if (aMode == nsSizeMode_Fullscreen) {
    EnsureFullscreenMode(true, aDoShowWindow);
    MOZ_ASSERT(mSizeMode == nsSizeMode_Fullscreen);
  } else if (mSizeMode == nsSizeMode_Fullscreen && aMode == nsSizeMode_Normal) {
    // If we are in fullscreen mode, minimize should work like normal and
    // return us to fullscreen mode when unminimized. Maximize isn't really
    // available and won't do anything. "Restore" should do the same thing as
    // requesting to end fullscreen.
    EnsureFullscreenMode(false, aDoShowWindow);
  } else {
    SetSizeModeInternal(aMode, aDoShowWindow);
  }
}

void nsWindow::FrameState::EnsureFullscreenMode(bool aFullScreen,
                                                DoShowWindow aDoShowWindow) {
  const bool changed = aFullScreen != mFullscreenMode;
  if (changed && aFullScreen) {
    // Save the size mode from before fullscreen.
    mPreFullscreenSizeMode = mSizeMode;
  }
  mFullscreenMode = aFullScreen;
  if (changed || aFullScreen) {
    // NOTE(emilio): When minimizing a fullscreen window we remain with
    // mFullscreenMode = true, but mSizeMode = nsSizeMode_Minimized. We need to
    // make sure to call SetSizeModeInternal even if mFullscreenMode didn't
    // change, to ensure we actually end up with a fullscreen sizemode when
    // restoring a window from that state.
    SetSizeModeInternal(
        aFullScreen ? nsSizeMode_Fullscreen : mPreFullscreenSizeMode,
        aDoShowWindow);
  }
}

void nsWindow::FrameState::OnFrameChanging() {
  const nsSizeMode newSizeMode =
      GetSizeModeForWindowFrame(mWindow->mWnd, mFullscreenMode);
  EnsureSizeMode(newSizeMode);
  mWindow->UpdateNonClientMargins(false);
}

void nsWindow::FrameState::OnFrameChanged() {
  // We don't want to perform the ShowWindow ourselves if we're on the frame
  // changed message. Windows has done the frame change for us, and we take care
  // of activating as needed. We also don't want to potentially trigger
  // more focus / restore. Among other things, this addresses a bug on Win7
  // related to window docking. (bug 489258)
  const auto oldSizeMode = mSizeMode;
  const auto newSizeMode =
      GetSizeModeForWindowFrame(mWindow->mWnd, mFullscreenMode);
  EnsureSizeMode(newSizeMode, DoShowWindow::No);

  // If window was restored, activate the window now to get correct attributes.
  if (mWindow->mIsVisible && mWindow->IsForegroundWindow() &&
      oldSizeMode == nsSizeMode_Minimized &&
      mSizeMode != nsSizeMode_Minimized) {
    mWindow->DispatchFocusToTopLevelWindow(true);
  }
}

static void MaybeLogSizeMode(nsSizeMode aMode) {
#ifdef WINSTATE_DEBUG_OUTPUT
  MOZ_LOG(gWindowsLog, LogLevel::Info, ("*** SizeMode: %d\n", int(aMode)));
#endif
}

void nsWindow::FrameState::SetSizeModeInternal(nsSizeMode aMode,
                                               DoShowWindow aDoShowWindow) {
  if (mSizeMode == aMode) {
    return;
  }

  const auto oldSizeMode = mSizeMode;
  const bool fullscreenChange =
      mSizeMode == nsSizeMode_Fullscreen || aMode == nsSizeMode_Fullscreen;
  const bool maximized = aMode == nsSizeMode_Maximized;
  const bool fullscreen = aMode == nsSizeMode_Fullscreen;

  mSizeMode = aMode;

  MaybeLogSizeMode(mSizeMode);

  if (bool(aDoShowWindow) && mWindow->mIsVisible) {
    ShowWindowWithMode(mWindow->mWnd, aMode);
  }

  mWindow->UpdateNonClientMargins(false);

  if (fullscreenChange) {
    mWindow->OnFullscreenChanged(oldSizeMode, fullscreen);
  } else if (maximized) {
    TaskbarConcealer::OnWindowMaximized(mWindow);
  }

  mWindow->OnSizeModeChange();
}

void nsWindow::ContextMenuPreventer::Update(
    const WidgetMouseEvent& aEvent,
    const nsIWidget::ContentAndAPZEventStatus& aEventStatus) {
  mNeedsToPreventContextMenu =
      aEvent.mMessage == eMouseUp &&
      aEvent.mButton == MouseButton::eSecondary &&
      aEvent.mInputSource == MouseEvent_Binding::MOZ_SOURCE_MOUSE &&
      aEventStatus.mApzStatus == nsEventStatus_eConsumeNoDefault;
}
