/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "WinEventObserver.h"

// clang-format off
#include <windows.h>
#include <winternl.h>
#include <winuser.h>
#include <wtsapi32.h>
#include <dbt.h>
// clang-format on

#include "InputDeviceUtils.h"
#include "ScreenHelperWin.h"
#include "WinWindowOcclusionTracker.h"
#include "WindowsUIUtils.h"
#include "gfxDWriteFonts.h"
#include "gfxPlatform.h"
#include "mozilla/Assertions.h"
#include "mozilla/ClearOnShutdown.h"
#include "mozilla/DebugOnly.h"
#include "mozilla/Logging.h"
#include "mozilla/LookAndFeel.h"
#include "mozilla/Services.h"
#include "mozilla/WindowsVersion.h"
#include "nsIObserverService.h"
#include "nsIWidget.h"
#include "nsLookAndFeel.h"
#include "nsStringFwd.h"
#include "nsWindowDbg.h"
#include "nsXULAppAPI.h"
#include "nsdefs.h"

// borrowed from devblogs.microsoft.com/oldnewthing/20041025-00/?p=37483, by way
// of the Chromium sandboxing code's "current_module.h"
extern "C" IMAGE_DOS_HEADER __ImageBase;
#define CURRENT_MODULE() reinterpret_cast<HMODULE>(&__ImageBase)

namespace mozilla::widget {

LazyLogModule gWinEventWindowLog("WinEventWindow");
#define OBS_LOG(...) \
  MOZ_LOG(gWinEventWindowLog, ::mozilla::LogLevel::Info, (__VA_ARGS__))

namespace {
namespace evtwin_details {
static HWND sHiddenWindow = nullptr;
static bool sHiddenWindowShutdown = false;
HDEVNOTIFY sDeviceNotifyHandle = nullptr;
}  // namespace evtwin_details
}  // namespace

/* static */
void WinEventWindow::Ensure() {
  MOZ_RELEASE_ASSERT(XRE_IsParentProcess());
  MOZ_RELEASE_ASSERT(NS_IsMainThread());

  using namespace evtwin_details;

  if (sHiddenWindow) return;
  if (sHiddenWindowShutdown) return;

  HMODULE const hSelf = CURRENT_MODULE();
  WNDCLASSW const wc = {.lpfnWndProc = WinEventWindow::WndProc,
                        .hInstance = hSelf,
                        .lpszClassName = kClassNameHidden};
  ATOM const atom = ::RegisterClassW(&wc);
  if (!atom) {
    // This is known to be possible when the atom table no longer has free
    // entries, which unfortunately happens more often than one might expect.
    // See bug 1571516.
    auto volatile const err [[maybe_unused]] = ::GetLastError();
    MOZ_CRASH("could not register broadcast-receiver window-class");
  }

  sHiddenWindow =
      ::CreateWindowW((LPCWSTR)(uintptr_t)atom, L"WinEventWindow", 0, 0, 0, 0,
                      0, nullptr, nullptr, hSelf, nullptr);

  if (!sHiddenWindow) {
    MOZ_CRASH("could not create broadcast-receiver window");
  }

  sDeviceNotifyHandle = InputDeviceUtils::RegisterNotification(sHiddenWindow);

  // WM_WTSSESSION_CHANGE is only sent to registered windows.
  DebugOnly<BOOL> wtsRegistered =
      ::WTSRegisterSessionNotification(sHiddenWindow, NOTIFY_FOR_THIS_SESSION);
  NS_ASSERTION(wtsRegistered, "WTSRegisterSessionNotification failed!");

  // It should be harmless to leak this window until destruction -- but other
  // parts of Gecko may expect all windows to be destroyed, so do that.
  mozilla::RunOnShutdown([]() {
    InputDeviceUtils::UnregisterNotification(sDeviceNotifyHandle);
    ::WTSUnRegisterSessionNotification(sHiddenWindow);

    sHiddenWindowShutdown = true;
    ::DestroyWindow(sHiddenWindow);
    sHiddenWindow = nullptr;
  });
};

/* static */
HWND WinEventWindow::GetHwndForTestingOnly() {
  return evtwin_details::sHiddenWindow;
}

// Callbacks for individual event types. These are private and internal
// implementation details of WinEventWindow.
namespace {
namespace evtwin_details {

static void NotifyThemeChanged(ThemeChangeKind aKind) {
  LookAndFeel::NotifyChangedAllWindows(aKind);
}

static void OnSessionChange(WPARAM wParam, LPARAM lParam) {
  if (wParam == WTS_SESSION_LOCK || wParam == WTS_SESSION_UNLOCK) {
    DWORD currentSessionId;
    BOOL const rv =
        ::ProcessIdToSessionId(::GetCurrentProcessId(), &currentSessionId);
    if (!rv) {
      // A process should always have the relevant access privileges for itself,
      // but the above call could still fail if, e.g., someone's playing games
      // with function imports. If so, just assert and/or skip out.
      //
      // Should this turn out to somehow be a real concern, we could do
      // ```
      //    DWORD const currentSessionId =
      //       ::NtCurrentTeb()->ProcessEnvironmentBlock->SessionId;
      // ```
      // instead, which is actually documented (albeit abjured against).
      MOZ_ASSERT(false, "::ProcessIdToSessionId() failed");
      return;
    }

    OBS_LOG("WinEventWindow OnSessionChange(): wParam=%zu lParam=%" PRIdLPTR
            " currentSessionId=%lu",
            wParam, lParam, currentSessionId);

    // Ignore lock/unlock messages for other sessions -- which Windows actually
    // _does_ send in some scenarios; see review of Chromium changeset 1929489:
    //
    // https://chromium-review.googlesource.com/c/chromium/src/+/1929489
    if (currentSessionId != (DWORD)lParam) {
      return;
    }

    if (auto* wwot = WinWindowOcclusionTracker::Get()) {
      wwot->OnSessionChange(wParam);
    }

    if (nsCOMPtr<nsIObserverService> obs = services::GetObserverService()) {
      obs->NotifyObservers(nullptr,
                           wParam == WTS_SESSION_LOCK
                               ? NS_WIDGET_SCREEN_LOCK_OBSERVER_TOPIC
                               : NS_WIDGET_SCREEN_UNLOCK_OBSERVER_TOPIC,
                           nullptr);
    }
  }
}

static void OnPowerBroadcast(WPARAM wParam, LPARAM lParam) {
  if (wParam == PBT_POWERSETTINGCHANGE) {
    POWERBROADCAST_SETTING* setting = (POWERBROADCAST_SETTING*)lParam;
    MOZ_ASSERT(setting);

    if (::IsEqualGUID(setting->PowerSetting, GUID_SESSION_DISPLAY_STATUS) &&
        setting->DataLength == sizeof(DWORD)) {
      MONITOR_DISPLAY_STATE state{};
      errno_t const err =
          ::memcpy_s(&state, sizeof(state), setting->Data, setting->DataLength);
      if (err) {
        MOZ_ASSERT(false, "bad data in POWERBROADCAST_SETTING in lParam");
        return;
      }

      bool const displayOn = MONITOR_DISPLAY_STATE::PowerMonitorOff != state;

      OBS_LOG("WinEventWindow OnPowerBroadcast(): displayOn=%d",
              int(displayOn ? 1 : 0));

      if (auto* wwot = WinWindowOcclusionTracker::Get()) {
        wwot->OnDisplayStateChanged(displayOn);
      }
    }
  }
}

static void OnSettingsChange(WPARAM wParam, LPARAM lParam) {
  switch (wParam) {
    case SPI_SETCLIENTAREAANIMATION:
    case SPI_SETKEYBOARDDELAY:
    case SPI_SETMOUSEVANISH:
    case MOZ_SPI_SETCURSORSIZE:
      // These need to update LookAndFeel cached values.
      //
      // They affect reduced motion settings / caret blink count / show pointer
      // while typing / tooltip offset, so no need to invalidate style / layout.
      NotifyThemeChanged(widget::ThemeChangeKind::MediaQueriesOnly);
      return;

    case SPI_SETFONTSMOOTHING:
    case SPI_SETFONTSMOOTHINGTYPE:
      gfxDWriteFont::UpdateSystemTextVars();
      return;

    case SPI_SETWORKAREA:
      // NB: We also refresh screens on WM_DISPLAYCHANGE, but the rcWork
      // values are sometimes wrong at that point.  This message then arrives
      // soon afterward, when we can get the right rcWork values.
      ScreenHelperWin::RefreshScreens();
      return;

    default:
      break;
  }

  if (lParam == 0) {
    return;
  }
  nsDependentString lParamString{reinterpret_cast<const wchar_t*>(lParam)};

  if (lParamString == u"ImmersiveColorSet"_ns) {
    // This affects system colors (-moz-win-accentcolor), so gotta pass the
    // style flag.
    NotifyThemeChanged(widget::ThemeChangeKind::Style);
    return;
  }

  // UserInteractionMode, ConvertibleSlateMode, and SystemDockMode may cause
  // @media(pointer) queries to change, which layout needs to know about.
  //
  // The former two of those also imply that the current tablet-mode state needs
  // to be updated.

  if (lParamString == u"UserInteractionMode"_ns) {
    // Documentation implies, and testing shows, that this is seen on Win10
    // only.
    (void)NS_WARN_IF(mozilla::IsWin11OrLater());
    WindowsUIUtils::UpdateInWin10TabletMode();
    NotifyThemeChanged(widget::ThemeChangeKind::MediaQueriesOnly);
    return;
  }

  if (lParamString == u"ConvertibleSlateMode"_ns) {
    // Documentation implies, and testing shows, that this is not seen on Win10.
    (void)NS_WARN_IF(!mozilla::IsWin11OrLater());
    WindowsUIUtils::UpdateInWin11TabletMode(
        WindowsUIUtils::TabletModeUpdateReason::Notification);
    NotifyThemeChanged(widget::ThemeChangeKind::MediaQueriesOnly);
    return;
  }

  if (lParamString == u"SystemDockMode"_ns) {
    NotifyThemeChanged(widget::ThemeChangeKind::MediaQueriesOnly);
    return;
  }
}

static void OnDeviceChange(WPARAM wParam, LPARAM lParam) {
  if (wParam == DBT_DEVICEARRIVAL || wParam == DBT_DEVICEREMOVECOMPLETE) {
    DEV_BROADCAST_HDR* hdr = reinterpret_cast<DEV_BROADCAST_HDR*>(lParam);
    // Check dbch_devicetype explicitly since we will get other device types
    // (e.g. DBT_DEVTYP_VOLUME) for some reason, even if we specify
    // DBT_DEVTYP_DEVICEINTERFACE in the filter for RegisterDeviceNotification.
    if (hdr->dbch_devicetype == DBT_DEVTYP_DEVICEINTERFACE) {
      // This can only change media queries (any-hover/any-pointer).
      NotifyThemeChanged(widget::ThemeChangeKind::MediaQueriesOnly);
    }
  }
}

}  // namespace evtwin_details
}  // namespace

// static
LRESULT CALLBACK WinEventWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam,
                                         LPARAM lParam) {
  NativeEventLogger eventLogger("WinEventWindow", hwnd, msg, wParam, lParam);

  switch (msg) {
    case WM_WINDOWPOSCHANGING: {
      // prevent rude external programs from making hidden window visible
      LPWINDOWPOS info = (LPWINDOWPOS)lParam;
      info->flags &= ~SWP_SHOWWINDOW;
    } break;

    case WM_WTSSESSION_CHANGE: {
      evtwin_details::OnSessionChange(wParam, lParam);
    } break;

    case WM_POWERBROADCAST: {
      evtwin_details::OnPowerBroadcast(wParam, lParam);
    } break;

    case WM_SYSCOLORCHANGE: {
      // No need to invalidate layout for system color changes, but we need to
      // invalidate style.
      evtwin_details::NotifyThemeChanged(widget::ThemeChangeKind::Style);
    } break;

    case WM_THEMECHANGED: {
      // We assume pretty much everything could've changed here.
      evtwin_details::NotifyThemeChanged(
          widget::ThemeChangeKind::StyleAndLayout);
    } break;

    case WM_FONTCHANGE: {
      // update the global font list
      gfxPlatform::GetPlatform()->UpdateFontList();
    } break;

    case WM_SETTINGCHANGE: {
      evtwin_details::OnSettingsChange(wParam, lParam);
    } break;

    case WM_DEVICECHANGE: {
      evtwin_details::OnDeviceChange(wParam, lParam);
    } break;

    case WM_DISPLAYCHANGE: {
      ScreenHelperWin::RefreshScreens();
      break;
    }
  }

  LRESULT const ret = ::DefWindowProcW(hwnd, msg, wParam, lParam);
  eventLogger.SetResult(ret, false);
  return ret;
}

#undef OBS_LOG

}  // namespace mozilla::widget
