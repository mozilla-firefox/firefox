/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <windows.h>
#include <winreg.h>
#include <wrl.h>
#include <powerbase.h>
#include <cfgmgr32.h>

#include "nsServiceManagerUtils.h"

#include "WindowsUIUtils.h"

#include "nsIObserverService.h"
#include "nsIAppShellService.h"
#include "nsAppShellCID.h"
#include "mozilla/ClearOnShutdown.h"
#include "mozilla/ResultVariant.h"
#include "mozilla/Services.h"
#include "mozilla/StaticPrefs_widget.h"
#include "mozilla/WidgetUtils.h"
#include "mozilla/WindowsVersion.h"
#include "mozilla/LookAndFeel.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/media/MediaUtils.h"
#include "nsString.h"
#include "nsGlobalWindowOuter.h"
#include "nsIWidget.h"
#include "nsIWindowMediator.h"
#include "nsPIDOMWindow.h"
#include "nsWindowGfx.h"
#include "Units.h"
#include "nsWindowsHelpers.h"
#include "WinRegistry.h"
#include "WinUtils.h"

mozilla::LazyLogModule gTabletModeLog("TabletMode");

/* mingw currently doesn't support windows.ui.viewmanagement.h, so we disable it
 * until it's fixed. */

// See
// https://github.com/tpn/winsdk-10/blob/master/Include/10.0.14393.0/winrt/windows.ui.viewmanagement.h
// for the source of some of these definitions for older SDKs.
#ifndef __MINGW32__

#  include <inspectable.h>
#  include <roapi.h>
#  include <windows.ui.viewmanagement.h>

#  pragma comment(lib, "runtimeobject.lib")

using namespace ABI::Windows::UI;
using namespace ABI::Windows::UI::ViewManagement;
using namespace Microsoft::WRL;
using namespace Microsoft::WRL::Wrappers;
using namespace ABI::Windows::Foundation;
using namespace ABI::Windows::ApplicationModel::DataTransfer;

#  ifndef RuntimeClass_Windows_UI_ViewManagement_UIViewSettings
#    define RuntimeClass_Windows_UI_ViewManagement_UIViewSettings \
      L"Windows.UI.ViewManagement.UIViewSettings"
#  endif

#  ifndef IUIViewSettingsInterop

using IUIViewSettingsInterop = interface IUIViewSettingsInterop;

MIDL_INTERFACE("3694dbf9-8f68-44be-8ff5-195c98ede8a6")
IUIViewSettingsInterop : public IInspectable {
 public:
  virtual HRESULT STDMETHODCALLTYPE GetForWindow(HWND hwnd, REFIID riid,
                                                 void** ppv) = 0;
};
#  endif

#  ifndef __IDataTransferManagerInterop_INTERFACE_DEFINED__
#    define __IDataTransferManagerInterop_INTERFACE_DEFINED__

using IDataTransferManagerInterop = interface IDataTransferManagerInterop;

MIDL_INTERFACE("3A3DCD6C-3EAB-43DC-BCDE-45671CE800C8")
IDataTransferManagerInterop : public IUnknown {
 public:
  virtual HRESULT STDMETHODCALLTYPE GetForWindow(
      HWND appWindow, REFIID riid, void** dataTransferManager) = 0;
  virtual HRESULT STDMETHODCALLTYPE ShowShareUIForWindow(HWND appWindow) = 0;
};

#  endif

#  if !defined( \
      ____x_ABI_CWindows_CApplicationModel_CDataTransfer_CIDataPackage4_INTERFACE_DEFINED__)
#    define ____x_ABI_CWindows_CApplicationModel_CDataTransfer_CIDataPackage4_INTERFACE_DEFINED__

MIDL_INTERFACE("13a24ec8-9382-536f-852a-3045e1b29a3b")
IDataPackage4 : public IInspectable {
 public:
  virtual HRESULT STDMETHODCALLTYPE add_ShareCanceled(
      __FITypedEventHandler_2_Windows__CApplicationModel__CDataTransfer__CDataPackage_IInspectable *
          handler,
      EventRegistrationToken * token) = 0;
  virtual HRESULT STDMETHODCALLTYPE remove_ShareCanceled(
      EventRegistrationToken token) = 0;
};

#  endif

#  ifndef RuntimeClass_Windows_UI_ViewManagement_UISettings
#    define RuntimeClass_Windows_UI_ViewManagement_UISettings \
      L"Windows.UI.ViewManagement.UISettings"
#  endif
#  if WINDOWS_FOUNDATION_UNIVERSALAPICONTRACT_VERSION < 0x80000
namespace ABI {
namespace Windows {
namespace UI {
namespace ViewManagement {

class UISettings;
class UISettingsAutoHideScrollBarsChangedEventArgs;
interface IUISettingsAutoHideScrollBarsChangedEventArgs;
MIDL_INTERFACE("87afd4b2-9146-5f02-8f6b-06d454174c0f")
IUISettingsAutoHideScrollBarsChangedEventArgs : public IInspectable{};

}  // namespace ViewManagement
}  // namespace UI
}  // namespace Windows
}  // namespace ABI

namespace ABI {
namespace Windows {
namespace Foundation {

template <>
struct __declspec(uuid("808aef30-2660-51b0-9c11-f75dd42006b4"))
ITypedEventHandler<ABI::Windows::UI::ViewManagement::UISettings*,
                   ABI::Windows::UI::ViewManagement::
                       UISettingsAutoHideScrollBarsChangedEventArgs*>
    : ITypedEventHandler_impl<
          ABI::Windows::Foundation::Internal::AggregateType<
              ABI::Windows::UI::ViewManagement::UISettings*,
              ABI::Windows::UI::ViewManagement::IUISettings*>,
          ABI::Windows::Foundation::Internal::AggregateType<
              ABI::Windows::UI::ViewManagement::
                  UISettingsAutoHideScrollBarsChangedEventArgs*,
              ABI::Windows::UI::ViewManagement::
                  IUISettingsAutoHideScrollBarsChangedEventArgs*>> {
  static const wchar_t* z_get_rc_name_impl() {
    return L"Windows.Foundation.TypedEventHandler`2<Windows.UI.ViewManagement."
           L"UISettings, "
           L"Windows.UI.ViewManagement."
           L"UISettingsAutoHideScrollBarsChangedEventArgs>";
  }
};
// Define a typedef for the parameterized interface specialization's mangled
// name. This allows code which uses the mangled name for the parameterized
// interface to access the correct parameterized interface specialization.
typedef ITypedEventHandler<ABI::Windows::UI::ViewManagement::UISettings*,
                           ABI::Windows::UI::ViewManagement::
                               UISettingsAutoHideScrollBarsChangedEventArgs*>
    __FITypedEventHandler_2_Windows__CUI__CViewManagement__CUISettings_Windows__CUI__CViewManagement__CUISettingsAutoHideScrollBarsChangedEventArgs_t;
#    define __FITypedEventHandler_2_Windows__CUI__CViewManagement__CUISettings_Windows__CUI__CViewManagement__CUISettingsAutoHideScrollBarsChangedEventArgs \
      ABI::Windows::Foundation::                                                                                                                            \
          __FITypedEventHandler_2_Windows__CUI__CViewManagement__CUISettings_Windows__CUI__CViewManagement__CUISettingsAutoHideScrollBarsChangedEventArgs_t

}  // namespace Foundation
}  // namespace Windows
}  // namespace ABI

namespace ABI {
namespace Windows {
namespace UI {
namespace ViewManagement {
class UISettings;
class UISettingsAutoHideScrollBarsChangedEventArgs;
interface IUISettings5;
MIDL_INTERFACE("5349d588-0cb5-5f05-bd34-706b3231f0bd")
IUISettings5 : public IInspectable {
 public:
  virtual HRESULT STDMETHODCALLTYPE get_AutoHideScrollBars(boolean * value) = 0;
  virtual HRESULT STDMETHODCALLTYPE add_AutoHideScrollBarsChanged(
      __FITypedEventHandler_2_Windows__CUI__CViewManagement__CUISettings_Windows__CUI__CViewManagement__CUISettingsAutoHideScrollBarsChangedEventArgs *
          handler,
      EventRegistrationToken * token) = 0;
  virtual HRESULT STDMETHODCALLTYPE remove_AutoHideScrollBarsChanged(
      EventRegistrationToken token) = 0;
};
}  // namespace ViewManagement
}  // namespace UI
}  // namespace Windows
}  // namespace ABI
#  endif
#endif

using namespace mozilla;

// Since Win10 and Win11 tablet modes can't both be simultaneously active, we
// only need one backing variable for the both of them.
enum class TabletModeState : uint8_t { Unknown, Off, On };
static TabletModeState sInTabletModeState = TabletModeState::Unknown;

WindowsUIUtils::WindowsUIUtils() = default;
WindowsUIUtils::~WindowsUIUtils() = default;

NS_IMPL_ISUPPORTS(WindowsUIUtils, nsIWindowsUIUtils)

NS_IMETHODIMP
WindowsUIUtils::GetSystemSmallIconSize(int32_t* aSize) {
  NS_ENSURE_ARG(aSize);

  mozilla::LayoutDeviceIntSize size =
      nsWindowGfx::GetIconMetrics(nsWindowGfx::kSmallIcon);
  *aSize = std::max(size.width, size.height);
  return NS_OK;
}

NS_IMETHODIMP
WindowsUIUtils::GetSystemLargeIconSize(int32_t* aSize) {
  NS_ENSURE_ARG(aSize);

  mozilla::LayoutDeviceIntSize size =
      nsWindowGfx::GetIconMetrics(nsWindowGfx::kRegularIcon);
  *aSize = std::max(size.width, size.height);
  return NS_OK;
}

NS_IMETHODIMP
WindowsUIUtils::SetWindowIcon(mozIDOMWindowProxy* aWindow,
                              imgIContainer* aSmallIcon,
                              imgIContainer* aBigIcon) {
  NS_ENSURE_ARG(aWindow);

  nsCOMPtr<nsIWidget> widget =
      nsGlobalWindowOuter::Cast(aWindow)->GetMainWidget();
  nsWindow* window = static_cast<nsWindow*>(widget.get());

  nsresult rv;

  if (aSmallIcon) {
    HICON hIcon = nullptr;
    rv = nsWindowGfx::CreateIcon(
        aSmallIcon, nullptr, false, mozilla::LayoutDeviceIntPoint(),
        nsWindowGfx::GetIconMetrics(nsWindowGfx::kSmallIcon), &hIcon);
    NS_ENSURE_SUCCESS(rv, rv);

    window->SetSmallIcon(hIcon);
  }

  if (aBigIcon) {
    HICON hIcon = nullptr;
    rv = nsWindowGfx::CreateIcon(
        aBigIcon, nullptr, false, mozilla::LayoutDeviceIntPoint(),
        nsWindowGfx::GetIconMetrics(nsWindowGfx::kRegularIcon), &hIcon);
    NS_ENSURE_SUCCESS(rv, rv);

    window->SetBigIcon(hIcon);
  }

  return NS_OK;
}

NS_IMETHODIMP
WindowsUIUtils::SetWindowIconFromExe(mozIDOMWindowProxy* aWindow,
                                     const nsAString& aExe, uint16_t aIndex) {
  NS_ENSURE_ARG(aWindow);

  nsCOMPtr<nsIWidget> widget =
      nsGlobalWindowOuter::Cast(aWindow)->GetMainWidget();
  nsWindow* window = static_cast<nsWindow*>(widget.get());

  HICON icon = ::LoadIconW(::GetModuleHandleW(PromiseFlatString(aExe).get()),
                           MAKEINTRESOURCEW(aIndex));
  window->SetBigIcon(icon);
  window->SetSmallIcon(icon);

  return NS_OK;
}

NS_IMETHODIMP
WindowsUIUtils::SetWindowIconNoData(mozIDOMWindowProxy* aWindow) {
  NS_ENSURE_ARG(aWindow);

  nsCOMPtr<nsIWidget> widget =
      nsGlobalWindowOuter::Cast(aWindow)->GetMainWidget();
  nsWindow* window = static_cast<nsWindow*>(widget.get());

  window->SetSmallIconNoData();
  window->SetBigIconNoData();

  return NS_OK;
}

bool WindowsUIUtils::GetInWin10TabletMode() {
  MOZ_DIAGNOSTIC_ASSERT(NS_IsMainThread());
  if (IsWin11OrLater()) {
    return false;
  }
  if (sInTabletModeState == TabletModeState::Unknown) {
    UpdateInWin10TabletMode();
  }
  return sInTabletModeState == TabletModeState::On;
}

bool WindowsUIUtils::GetInWin11TabletMode() {
  MOZ_DIAGNOSTIC_ASSERT(NS_IsMainThread());
  if (!IsWin11OrLater()) {
    return false;
  }
  if (sInTabletModeState == TabletModeState::Unknown) {
    UpdateInWin11TabletMode();
  }
  return sInTabletModeState == TabletModeState::On;
}

NS_IMETHODIMP
WindowsUIUtils::GetInWin10TabletMode(bool* aResult) {
  *aResult = GetInWin10TabletMode();
  return NS_OK;
}

NS_IMETHODIMP
WindowsUIUtils::GetInWin11TabletMode(bool* aResult) {
  *aResult = GetInWin11TabletMode();
  return NS_OK;
}

static IInspectable* GetUISettings() {
  MOZ_DIAGNOSTIC_ASSERT(NS_IsMainThread());
#ifndef __MINGW32__
  // We need to keep this alive for ~ever so that change callbacks work as
  // expected, sigh.
  static StaticRefPtr<IInspectable> sUiSettingsAsInspectable;

  if (!sUiSettingsAsInspectable) {
    ComPtr<IInspectable> uiSettingsAsInspectable;
    ::RoActivateInstance(
        HStringReference(RuntimeClass_Windows_UI_ViewManagement_UISettings)
            .Get(),
        &uiSettingsAsInspectable);
    if (NS_WARN_IF(!uiSettingsAsInspectable)) {
      return nullptr;
    }

    ComPtr<IUISettings5> uiSettings5;
    if (SUCCEEDED(uiSettingsAsInspectable.As(&uiSettings5))) {
      EventRegistrationToken unusedToken;
      auto callback = Callback<ITypedEventHandler<
          UISettings*, UISettingsAutoHideScrollBarsChangedEventArgs*>>(
          [](auto...) {
            // Scrollbar sizes change layout.
            LookAndFeel::NotifyChangedAllWindows(
                widget::ThemeChangeKind::StyleAndLayout);
            return S_OK;
          });
      (void)NS_WARN_IF(FAILED(uiSettings5->add_AutoHideScrollBarsChanged(
          callback.Get(), &unusedToken)));
    }

    ComPtr<IUISettings2> uiSettings2;
    if (SUCCEEDED(uiSettingsAsInspectable.As(&uiSettings2))) {
      EventRegistrationToken unusedToken;
      auto callback =
          Callback<ITypedEventHandler<UISettings*, IInspectable*>>([](auto...) {
            // Text scale factor changes style and layout.
            LookAndFeel::NotifyChangedAllWindows(
                widget::ThemeChangeKind::StyleAndLayout);
            return S_OK;
          });
      (void)NS_WARN_IF(FAILED(uiSettings2->add_TextScaleFactorChanged(
          callback.Get(), &unusedToken)));
    }

    ComPtr<IUISettings3> uiSettings3;
    if (SUCCEEDED(uiSettingsAsInspectable.As(&uiSettings3))) {
      EventRegistrationToken unusedToken;
      auto callback =
          Callback<ITypedEventHandler<UISettings*, IInspectable*>>([](auto...) {
            // System color changes change style only.
            LookAndFeel::NotifyChangedAllWindows(
                widget::ThemeChangeKind::Style);
            return S_OK;
          });
      (void)NS_WARN_IF(FAILED(
          uiSettings3->add_ColorValuesChanged(callback.Get(), &unusedToken)));
    }

    ComPtr<IUISettings4> uiSettings4;
    if (SUCCEEDED(uiSettingsAsInspectable.As(&uiSettings4))) {
      EventRegistrationToken unusedToken;
      auto callback =
          Callback<ITypedEventHandler<UISettings*, IInspectable*>>([](auto...) {
            // Transparent effects changes change media queries only.
            LookAndFeel::NotifyChangedAllWindows(
                widget::ThemeChangeKind::MediaQueriesOnly);
            return S_OK;
          });
      (void)NS_WARN_IF(FAILED(uiSettings4->add_AdvancedEffectsEnabledChanged(
          callback.Get(), &unusedToken)));
    }

    sUiSettingsAsInspectable = dont_AddRef(uiSettingsAsInspectable.Detach());
    ClearOnShutdown(&sUiSettingsAsInspectable);
  }

  return sUiSettingsAsInspectable.get();
#else
  return nullptr;
#endif
}

Maybe<nscolor> WindowsUIUtils::GetAccentColor(int aTone) {
  MOZ_ASSERT(aTone >= -3);
  MOZ_ASSERT(aTone <= 3);
#ifndef __MINGW32__
  ComPtr<IInspectable> settings = GetUISettings();
  if (NS_WARN_IF(!settings)) {
    return Nothing();
  }
  ComPtr<IUISettings3> uiSettings3;
  if (NS_WARN_IF(FAILED(settings.As(&uiSettings3)))) {
    return Nothing();
  }
  Color color;
  auto colorType = UIColorType(int(UIColorType_Accent) + aTone);
  if (NS_WARN_IF(FAILED(uiSettings3->GetColorValue(colorType, &color)))) {
    return Nothing();
  }
  return Some(NS_RGBA(color.R, color.G, color.B, color.A));
#else
  return Nothing();
#endif
}

Maybe<nscolor> WindowsUIUtils::GetSystemColor(ColorScheme aScheme,
                                              int aSysColor) {
#ifndef __MINGW32__
  if (!StaticPrefs::widget_windows_uwp_system_colors_enabled()) {
    return Nothing();
  }

  // https://docs.microsoft.com/en-us/windows/apps/design/style/color
  // Is a useful resource to see which values have decent contrast.
  if (StaticPrefs::widget_windows_uwp_system_colors_highlight_accent()) {
    if (aSysColor == COLOR_HIGHLIGHT) {
      int tone = aScheme == ColorScheme::Light ? 0 : -1;
      if (auto c = GetAccentColor(tone)) {
        return c;
      }
    }
    if (aSysColor == COLOR_HIGHLIGHTTEXT && GetAccentColor()) {
      return Some(NS_RGBA(255, 255, 255, 255));
    }
  }

  if (aScheme == ColorScheme::Dark) {
    // There are no explicitly dark colors in UWP, other than the highlight
    // colors above.
    return Nothing();
  }

  auto knownType = [&]() -> Maybe<UIElementType> {
#  define MAP(_win32, _uwp) \
    case COLOR_##_win32:    \
      return Some(UIElementType_##_uwp)
    switch (aSysColor) {
      MAP(HIGHLIGHT, Highlight);
      MAP(HIGHLIGHTTEXT, HighlightText);
      MAP(ACTIVECAPTION, ActiveCaption);
      MAP(BTNFACE, ButtonFace);
      MAP(BTNTEXT, ButtonText);
      MAP(CAPTIONTEXT, CaptionText);
      MAP(GRAYTEXT, GrayText);
      MAP(HOTLIGHT, Hotlight);
      MAP(INACTIVECAPTION, InactiveCaption);
      MAP(INACTIVECAPTIONTEXT, InactiveCaptionText);
      MAP(WINDOW, Window);
      MAP(WINDOWTEXT, WindowText);
      default:
        return Nothing();
    }
#  undef MAP
  }();
  if (!knownType) {
    return Nothing();
  }
  ComPtr<IInspectable> settings = GetUISettings();
  if (NS_WARN_IF(!settings)) {
    return Nothing();
  }
  ComPtr<IUISettings> uiSettings;
  if (NS_WARN_IF(FAILED(settings.As(&uiSettings)))) {
    return Nothing();
  }
  Color color;
  if (NS_WARN_IF(FAILED(uiSettings->UIElementColor(*knownType, &color)))) {
    return Nothing();
  }
  return Some(NS_RGBA(color.R, color.G, color.B, color.A));
#else
  return Nothing();
#endif
}
bool WindowsUIUtils::ComputeOverlayScrollbars() {
#ifndef __MINGW32__
  if (!IsWin11OrLater()) {
    // While in theory Windows 10 supports overlay scrollbar settings, it's off
    // by default and it's untested whether our Win10 scrollbar drawing code
    // deals with it properly.
    return false;
  }
  if (!StaticPrefs::widget_windows_overlay_scrollbars_enabled()) {
    return false;
  }
  ComPtr<IInspectable> settings = GetUISettings();
  if (NS_WARN_IF(!settings)) {
    return false;
  }
  ComPtr<IUISettings5> uiSettings5;
  if (NS_WARN_IF(FAILED(settings.As(&uiSettings5)))) {
    return false;
  }
  boolean autoHide = false;
  if (NS_WARN_IF(FAILED(uiSettings5->get_AutoHideScrollBars(&autoHide)))) {
    return false;
  }
  return autoHide;
#else
  return false;
#endif
}

double WindowsUIUtils::ComputeTextScaleFactor() {
#ifndef __MINGW32__
  ComPtr<IInspectable> settings = GetUISettings();
  if (NS_WARN_IF(!settings)) {
    return 1.0;
  }
  ComPtr<IUISettings2> uiSettings2;
  if (NS_WARN_IF(FAILED(settings.As(&uiSettings2)))) {
    return false;
  }
  double scaleFactor = 1.0;
  if (NS_WARN_IF(FAILED(uiSettings2->get_TextScaleFactor(&scaleFactor)))) {
    return 1.0;
  }
  return scaleFactor;
#else
  return 1.0;
#endif
}

bool WindowsUIUtils::ComputeTransparencyEffects() {
  constexpr bool kDefault = true;
#ifndef __MINGW32__
  ComPtr<IInspectable> settings = GetUISettings();
  if (NS_WARN_IF(!settings)) {
    return kDefault;
  }
  ComPtr<IUISettings4> uiSettings4;
  if (NS_WARN_IF(FAILED(settings.As(&uiSettings4)))) {
    return kDefault;
  }
  boolean transparencyEffects = kDefault;
  if (NS_WARN_IF(FAILED(
          uiSettings4->get_AdvancedEffectsEnabled(&transparencyEffects)))) {
    return kDefault;
  }
  return transparencyEffects;
#else
  return kDefault;
#endif
}

void WindowsUIUtils::UpdateInWin10TabletMode() {
  if (IsWin11OrLater()) {
    // (In theory we should never get here under Win11; but it's conceivable
    // that there are third-party applications that try to "assist" legacy Win10
    // apps by synthesizing Win10-style tablet-mode notifications.)
    return;
  }

  // The getter below relies on querying a HWND which is affine to the main
  // thread; its operation is not known to be thread-safe, let alone lock-free.
  MOZ_DIAGNOSTIC_ASSERT(NS_IsMainThread());

#ifndef __MINGW32__
  nsresult rv;
  nsCOMPtr<nsIWindowMediator> winMediator(
      do_GetService(NS_WINDOWMEDIATOR_CONTRACTID, &rv));
  if (NS_FAILED(rv)) {
    return;
  }

  nsCOMPtr<nsIWidget> widget;
  nsCOMPtr<mozIDOMWindowProxy> navWin;

  rv = winMediator->GetMostRecentBrowserWindow(getter_AddRefs(navWin));
  if (NS_FAILED(rv) || !navWin) {
    return;
  }

  nsPIDOMWindowOuter* win = nsPIDOMWindowOuter::From(navWin);
  widget = widget::WidgetUtils::DOMWindowToWidget(win);

  if (!widget) {
    return;
  }

  HWND winPtr = (HWND)widget->GetNativeData(NS_NATIVE_WINDOW);
  ComPtr<IUIViewSettingsInterop> uiViewSettingsInterop;

  HRESULT hr = GetActivationFactory(
      HStringReference(RuntimeClass_Windows_UI_ViewManagement_UIViewSettings)
          .Get(),
      &uiViewSettingsInterop);
  if (FAILED(hr)) {
    return;
  }
  ComPtr<IUIViewSettings> uiViewSettings;
  hr = uiViewSettingsInterop->GetForWindow(winPtr,
                                           IID_PPV_ARGS(&uiViewSettings));
  if (FAILED(hr)) {
    return;
  }
  UserInteractionMode mode;
  hr = uiViewSettings->get_UserInteractionMode(&mode);
  if (FAILED(hr)) {
    return;
  }

  TabletModeState oldTabletModeState = sInTabletModeState;
  sInTabletModeState = mode == UserInteractionMode_Touch ? TabletModeState::On
                                                         : TabletModeState::Off;

  if (sInTabletModeState != oldTabletModeState) {
    nsCOMPtr<nsIObserverService> observerService =
        mozilla::services::GetObserverService();
    observerService->NotifyObservers(nullptr, "tablet-mode-change",
                                     sInTabletModeState == TabletModeState::On
                                         ? u"win10-tablet-mode"
                                         : u"normal-mode");
  }
#endif
}

// Cache: whether this device is believed to be capable of entering tablet mode.
//
// Meaningful only if `IsWin11OrLater()`.
static Maybe<bool> sIsTabletCapable = Nothing();

// The UUID of a GPIO pin which indicates whether or not a convertible device is
// currently in tablet mode. (We copy `DEFINE_GUID`'s implementation here since
// we can't control `INITGUID`, which the canonical one is conditional on.)
//
// https://learn.microsoft.com/en-us/windows-hardware/drivers/gpiobtn/laptop-slate-mode-toggling-between-states
#define MOZ_DEFINE_GUID(name, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8) \
  EXTERN_C const GUID DECLSPEC_SELECTANY name = {                        \
      l, w1, w2, {b1, b2, b3, b4, b5, b6, b7, b8}}
/* 317fc439-3f77-41c8-b09e-08ad63272aa3 */ MOZ_DEFINE_GUID(
    MOZ_GUID_GPIOBUTTONS_LAPTOPSLATE_INTERFACE, 0x317fc439, 0x3f77, 0x41c8,
    0xb0, 0x9e, 0x08, 0xad, 0x63, 0x27, 0x2a, 0xa3);

void WindowsUIUtils::UpdateInWin11TabletMode() {
  // The OS-level getter itself is threadsafe, but we retain the main-thread
  // restriction to parallel the Win10 getter's (presumed) restriction.
  MOZ_DIAGNOSTIC_ASSERT(NS_IsMainThread());

  if (!IsWin11OrLater()) {
    // We should ordinarily never reach this point in Win10 -- but there may
    // well be some third-party application out there that synthesizes Win11-
    // style tablet-mode notifications.
    return;
  }

  // ***  ***  ***  WARNING: RELIANCE ON UNDOCUMENTED BEHAVIOR  ***  ***  ***
  //
  // Windows 10's `UserInteractionMode` API is no longer useful under Windows
  // 11: it always returns `UserInteractionMode_Mouse`.
  //
  // The documented API to query whether we're in tablet mode (alt.: "slate
  // mode") under Windows 11 is `::GetSystemMetrics(SM_CONVERTIBLESLATEMODE)`.
  // This returns 0 if we are in slate mode and 1 otherwise... except on devices
  // where tablet mode is unavailable (such as desktops), in which case it
  // returns 0 unconditionally.
  //
  // Unfortunately, there is no documented API to determine whether
  // `SM_CONVERTIBLESLATEMODE` is `0` because the device is currently in slate
  // mode or because the device can never be in slate mode.
  //
  // As such, we follow Chromium's lead here, and attempt to determine
  // heuristically whether that API is going to return anything sensible.
  // (Indeed, the heuristic below is in large part taken from Chromium.)

  if (sIsTabletCapable.isNothing()) {
    bool const heuristic = ([]() -> bool {
      // If the user has set the relevant pref to override our tablet-detection
      // heuristics, go with that.
      switch (StaticPrefs::widget_windows_tablet_detection_override()) {
        case -1:
          MOZ_LOG(gTabletModeLog, LogLevel::Info,
                  ("TCH: override detected (-1)"));
          return false;
        case 1:
          MOZ_LOG(gTabletModeLog, LogLevel::Info,
                  ("TCH: override detected (+1)"));
          return true;
        default:
          break;
      }

      // If ::GSM(SM_CONVERTIBLESLATEMODE) is _currently_ nonzero, we must be on
      // a system that does somnething with SM_CONVERTIBLESLATEMODE, so we can
      // trust it.
      if (::GetSystemMetrics(SM_CONVERTIBLESLATEMODE) != 0) {
        MOZ_LOG(gTabletModeLog, LogLevel::Info,
                ("TCH: SM_CONVERTIBLESLATEMODE != 0"));
        return true;
      }

      // If the device does not support touch it can't possibly be a tablet.
      if (GetSystemMetrics(SM_MAXIMUMTOUCHES) == 0) {
        MOZ_LOG(gTabletModeLog, LogLevel::Info,
                ("TCH: SM_MAXIMUMTOUCHES != 0"));
        return false;
      }

      if (MOZ_LOG_TEST(gTabletModeLog, LogLevel::Info)) {
        [&]() -> void {
          // Check to see if a particular registry key [1] exists, and what its
          // value is.
          //
          // This is not presently considered reliable, as some
          // non-tablet-capable devices have this registry key present, but not
          // set to 1 -- see bug 1932775, as well as comments in Chromium [2].
          //
          // This is probably strictly redundant with the CONVERTIBLESLATEMODE
          // check above, so we only even look at it if we're logging.
          //
          // [1] https://learn.microsoft.com/en-us/windows-hardware/customize/desktop/unattend/microsoft-windows-gpiobuttons-convertibleslatemode
          // [2] https://source.chromium.org/chromium/chromium/src/+/main:base/win/win_util.cc;l=240;drc=5a02fc6cdee77d0a39e9c43a4c2a29bbccc88852
          namespace Reg = mozilla::widget::WinRegistry;
          Reg::Key key(
              HKEY_LOCAL_MACHINE,
              uR"(System\CurrentControlSet\Control\PriorityControl)"_ns,
              Reg::KeyMode::QueryValue);
          if (!key) {
            MOZ_LOG(gTabletModeLog, LogLevel::Info,
                    ("TCH: \"PriorityControl\" registry path not found"));
          }

          auto const valueType = key.GetValueType(u"ConvertibleSlateMode"_ns);
          if (valueType == Reg::ValueType::None) {
            MOZ_LOG(gTabletModeLog, LogLevel::Info,
                    ("TCH: 'ConvertibleSlateMode' not found"));
            return;
          }

          if (auto const val =
                  key.GetValueAsDword(u"ConvertibleSlateMode"_ns)) {
            MOZ_LOG(gTabletModeLog, LogLevel::Info,
                    ("TCH: 'ConvertibleSlateMode' found; value is 0x%08" PRIX32,
                     *val));
          } else {
            MOZ_LOG(gTabletModeLog, LogLevel::Info,
                    ("TCH: 'ConvertibleSlateMode' found, but not a DWORD "
                     "(type=0x%08" PRIX32 ")",
                     uint32_t(valueType)));
          }
        }();
      }

      // If the device has this GUID mapped to a GPIO pin, it's almost certainly
      // tablet-capable. (It's not certain whether the converse is true.)
      //
      // https://learn.microsoft.com/en-us/windows-hardware/design/device-experiences/continuum#designing-your-device-for-tablet-mode
      bool const hasTabletGpioPin = [&]() {
        ULONG size = 0;
        GUID guid{MOZ_GUID_GPIOBUTTONS_LAPTOPSLATE_INTERFACE};

        CONFIGRET const err = ::CM_Get_Device_Interface_List_SizeW(
            &size, &guid, nullptr, CM_GET_DEVICE_INTERFACE_LIST_PRESENT);

        // (The next step at this point would usually be to call the function
        // "::CM_Get_Device_Interface_ListW()" -- but we don't care where the
        // associated device interface is actually mapped to; we only care
        // whether it's mapped at all.
        //
        // For our purposes, a zero-length null-terminated string doesn't count
        // as "present".)
        return err == CR_SUCCESS && size > 1;
      }();
      if (hasTabletGpioPin) {
        MOZ_LOG(gTabletModeLog, LogLevel::Info,
                ("TCH: relevant GPIO interface found"));
        return true;
      }

      // If the device has no rotation sensor, it's _probably_ not a convertible
      // device. (There are exceptions! See bug 1918292.)
      AR_STATE rotation_state;
      if (HRESULT hr = ::GetAutoRotationState(&rotation_state); !FAILED(hr)) {
        if ((rotation_state & (AR_NOT_SUPPORTED | AR_LAPTOP | AR_NOSENSOR)) !=
            0) {
          MOZ_LOG(gTabletModeLog, LogLevel::Info, ("TCH: no rotation sensor"));
          return false;
        }
      }

      // If the device returns `PlatformRoleSlate` for its POWER_PLATFORM_ROLE,
      // it's probably tablet-capable.
      //
      // The converse is known to be false; the tablet-capable Dell Inspiron 14
      // 7445 2-in-1 returns `PlatformRoleMobile`.
      //
      // (Chromium checks for PlatformRoleMobile as well, but (e.g.) a Dell XPS
      // 15 9500 also returns `PlatformRoleMobile` despite *not* being tablet-
      // capable.)
      POWER_PLATFORM_ROLE const role =
          mozilla::widget::WinUtils::GetPowerPlatformRole();
      if (role == PlatformRoleSlate) {
        MOZ_LOG(gTabletModeLog, LogLevel::Info,
                ("TCH: role == PlatformRoleSlate"));
        return true;
      }

      // Without some specific indicator of tablet-capability, assume that we're
      // tablet-incapable.
      MOZ_LOG(gTabletModeLog, LogLevel::Info,
              ("TCH: no indication; falling through"));
      return false;
    })();

    MOZ_LOG(gTabletModeLog, LogLevel::Info,
            ("tablet-capability heuristic: %s", heuristic ? "true" : "false"));

    sIsTabletCapable = Some(heuristic);
    // If we appear not to be tablet-capable, don't bother doing the check.
    // (We also don't need to send a signal.)
    if (!heuristic) {
      sInTabletModeState = TabletModeState::Off;
      return;
    }
  } else if (sIsTabletCapable == Some(false)) {
    // We've been in here before, and the heuristic came back false... but
    // somehow, we've just gotten an update for the convertible-slate-mode
    // state.
    //
    // Clearly the heuristic was wrong!
    //
    // TODO(rkraesig): should we add telemetry to see how often this gets hit?
    MOZ_LOG(gTabletModeLog, LogLevel::Warning,
            ("recv'd update signal after false heuristic run; reversing"));
    sIsTabletCapable = Some(true);
  }

  // at this point, we must be tablet-capable
  MOZ_ASSERT(sIsTabletCapable == Some(true));

  TabletModeState const oldState = sInTabletModeState;
  bool const isTableting =
      ::GetSystemMetrics(SM_CONVERTIBLESLATEMODE) == 0 /* [sic!] */;
  sInTabletModeState = isTableting ? TabletModeState::On : TabletModeState::Off;
  if (oldState != sInTabletModeState) {
    nsCOMPtr<nsIObserverService> observerService =
        mozilla::services::GetObserverService();
    observerService->NotifyObservers(nullptr, "tablet-mode-change",
                                     sInTabletModeState == TabletModeState::On
                                         ? u"win11-tablet-mode"
                                         : u"normal-mode");
  }
}

#ifndef __MINGW32__
struct HStringDeleter {
  using pointer = HSTRING;
  void operator()(pointer aString) { WindowsDeleteString(aString); }
};

using HStringUniquePtr = UniquePtr<HSTRING, HStringDeleter>;

Result<HStringUniquePtr, HRESULT> ConvertToWindowsString(
    const nsAString& aStr) {
  HSTRING rawStr;
  HRESULT hr = WindowsCreateString(PromiseFlatString(aStr).get(), aStr.Length(),
                                   &rawStr);
  if (FAILED(hr)) {
    return Err(hr);
  }
  return HStringUniquePtr(rawStr);
}

static Result<Ok, nsresult> RequestShare(
    std::function<HRESULT(IDataRequestedEventArgs* pArgs)>&& aCallback) {
  HWND hwnd = GetForegroundWindow();
  if (!hwnd) {
    return Err(NS_ERROR_FAILURE);
  }

  ComPtr<IDataTransferManagerInterop> dtmInterop;
  ComPtr<IDataTransferManager> dtm;

  HRESULT hr = RoGetActivationFactory(
      HStringReference(
          RuntimeClass_Windows_ApplicationModel_DataTransfer_DataTransferManager)
          .Get(),
      IID_PPV_ARGS(&dtmInterop));
  if (FAILED(hr) ||
      FAILED(dtmInterop->GetForWindow(hwnd, IID_PPV_ARGS(&dtm)))) {
    return Err(NS_ERROR_FAILURE);
  }

  auto callback = Callback<
      ITypedEventHandler<DataTransferManager*, DataRequestedEventArgs*>>(
      [aCallback](IDataTransferManager*,
                  IDataRequestedEventArgs* pArgs) -> HRESULT {
        return aCallback(pArgs);
      });

  EventRegistrationToken dataRequestedToken;
  if (FAILED(dtm->add_DataRequested(callback.Get(), &dataRequestedToken)) ||
      FAILED(dtmInterop->ShowShareUIForWindow(hwnd))) {
    return Err(NS_ERROR_FAILURE);
  }

  return Ok();
}

static Result<Ok, nsresult> AddShareEventListeners(
    const RefPtr<mozilla::media::Refcountable<MozPromiseHolder<SharePromise>>>&
        aPromiseHolder,
    const ComPtr<IDataPackage>& aDataPackage) {
  ComPtr<IDataPackage3> spDataPackage3;

  if (FAILED(aDataPackage.As(&spDataPackage3))) {
    return Err(NS_ERROR_FAILURE);
  }

  auto completedCallback =
      Callback<ITypedEventHandler<DataPackage*, ShareCompletedEventArgs*>>(
          [aPromiseHolder](IDataPackage*,
                           IShareCompletedEventArgs*) -> HRESULT {
            aPromiseHolder->Resolve(true, __func__);
            return S_OK;
          });

  EventRegistrationToken dataRequestedToken;
  if (FAILED(spDataPackage3->add_ShareCompleted(completedCallback.Get(),
                                                &dataRequestedToken))) {
    return Err(NS_ERROR_FAILURE);
  }

  ComPtr<IDataPackage4> spDataPackage4;
  if (SUCCEEDED(aDataPackage.As(&spDataPackage4))) {
    // Use SharedCanceled API only on supported versions of Windows
    // So that the older ones can still use ShareUrl()

    auto canceledCallback =
        Callback<ITypedEventHandler<DataPackage*, IInspectable*>>(
            [aPromiseHolder](IDataPackage*, IInspectable*) -> HRESULT {
              aPromiseHolder->Reject(NS_ERROR_FAILURE, __func__);
              return S_OK;
            });

    if (FAILED(spDataPackage4->add_ShareCanceled(canceledCallback.Get(),
                                                 &dataRequestedToken))) {
      return Err(NS_ERROR_FAILURE);
    }
  }

  return Ok();
}
#endif

RefPtr<SharePromise> WindowsUIUtils::Share(nsAutoString aTitle,
                                           nsAutoString aText,
                                           nsAutoString aUrl) {
  auto promiseHolder = MakeRefPtr<
      mozilla::media::Refcountable<MozPromiseHolder<SharePromise>>>();
  RefPtr<SharePromise> promise = promiseHolder->Ensure(__func__);

#ifndef __MINGW32__
  auto result = RequestShare([promiseHolder, title = std::move(aTitle),
                              text = std::move(aText), url = std::move(aUrl)](
                                 IDataRequestedEventArgs* pArgs) {
    ComPtr<IDataRequest> spDataRequest;
    ComPtr<IDataPackage> spDataPackage;
    ComPtr<IDataPackage2> spDataPackage2;
    ComPtr<IDataPackagePropertySet> spDataPackageProperties;

    if (FAILED(pArgs->get_Request(&spDataRequest)) ||
        FAILED(spDataRequest->get_Data(&spDataPackage)) ||
        FAILED(spDataPackage.As(&spDataPackage2)) ||
        FAILED(spDataPackage->get_Properties(&spDataPackageProperties))) {
      promiseHolder->Reject(NS_ERROR_FAILURE, __func__);
      return E_FAIL;
    }

    /*
     * Windows always requires a title, and an empty string does not work.
     * Thus we trick the API by passing a whitespace when we have no title.
     * https://docs.microsoft.com/en-us/windows/uwp/app-to-app/share-data
     */
    auto wTitle = ConvertToWindowsString((title.IsVoid() || title.Length() == 0)
                                             ? nsAutoString(u" "_ns)
                                             : title);
    if (wTitle.isErr() ||
        FAILED(spDataPackageProperties->put_Title(wTitle.unwrap().get()))) {
      promiseHolder->Reject(NS_ERROR_FAILURE, __func__);
      return E_FAIL;
    }

    // Assign even if empty, as Windows requires some data to share
    auto wText = ConvertToWindowsString(text);
    if (wText.isErr() || FAILED(spDataPackage->SetText(wText.unwrap().get()))) {
      promiseHolder->Reject(NS_ERROR_FAILURE, __func__);
      return E_FAIL;
    }

    if (!url.IsVoid()) {
      auto wUrl = ConvertToWindowsString(url);
      if (wUrl.isErr()) {
        promiseHolder->Reject(NS_ERROR_FAILURE, __func__);
        return wUrl.unwrapErr();
      }

      ComPtr<IUriRuntimeClassFactory> uriFactory;
      ComPtr<IUriRuntimeClass> uri;

      auto hr = GetActivationFactory(
          HStringReference(RuntimeClass_Windows_Foundation_Uri).Get(),
          &uriFactory);

      if (FAILED(hr) ||
          FAILED(uriFactory->CreateUri(wUrl.unwrap().get(), &uri)) ||
          FAILED(spDataPackage2->SetWebLink(uri.Get()))) {
        promiseHolder->Reject(NS_ERROR_FAILURE, __func__);
        return E_FAIL;
      }
    }

    if (!StaticPrefs::widget_windows_share_wait_action_enabled()) {
      promiseHolder->Resolve(true, __func__);
    } else if (AddShareEventListeners(promiseHolder, spDataPackage).isErr()) {
      promiseHolder->Reject(NS_ERROR_FAILURE, __func__);
      return E_FAIL;
    }

    return S_OK;
  });
  if (result.isErr()) {
    promiseHolder->Reject(result.unwrapErr(), __func__);
  }
#else
  promiseHolder->Reject(NS_ERROR_FAILURE, __func__);
#endif

  return promise;
}

NS_IMETHODIMP
WindowsUIUtils::ShareUrl(const nsAString& aUrlToShare,
                         const nsAString& aShareTitle) {
  nsAutoString text;
  text.SetIsVoid(true);
  WindowsUIUtils::Share(nsAutoString(aShareTitle), text,
                        nsAutoString(aUrlToShare));
  return NS_OK;
}
