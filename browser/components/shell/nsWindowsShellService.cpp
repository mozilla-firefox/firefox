/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/StaticPrefs_browser.h"
#define UNICODE

#include "nsWindowsShellService.h"
#include "nsWindowsShellServiceInternal.h"
#include "WindowsShellServiceRust.h"

#include "BinaryPath.h"
#include "gfxUtils.h"
#include "imgIContainer.h"
#include "imgIRequest.h"
#include "imgITools.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/Promise.h"
#include "mozilla/ErrorResult.h"
#include "mozilla/FileUtils.h"
#include "mozilla/GetKnownFolderPath.h"
#include "mozilla/gfx/2D.h"
#include "mozilla/intl/Localization.h"
#include "mozilla/Preferences.h"
#include "mozilla/RefPtr.h"
#include "mozilla/widget/WinTaskbar.h"
#include "mozilla/WindowsVersion.h"
#include "mozilla/WinHeaderOnlyUtils.h"
#include "nsAppDirectoryServiceDefs.h"
#include "nsComponentManagerUtils.h"
#include "nsDirectoryServiceDefs.h"
#include "nsDirectoryServiceUtils.h"
#include "nsIContent.h"
#include "nsIFile.h"
#include "nsIFileStreams.h"
#include "nsIImageLoadingContent.h"
#include "nsIMIMEService.h"
#include "nsINIParser.h"
#include "nsIOutputStream.h"
#include "nsIPrefService.h"
#include "nsIStringBundle.h"
#include "nsITimer.h"
#include "nsIWindowsRegKey.h"
#include "nsIXULAppInfo.h"
#include "nsLocalFile.h"
#include "nsNativeAppSupportWin.h"
#include "nsNetUtil.h"
#include "nsProxyRelease.h"
#include "nsServiceManagerUtils.h"
#include "nsShellService.h"
#include "nsThreadUtils.h"
#include "nsUnicharUtils.h"
#include "nsWindowsHelpers.h"
#include "nsXULAppAPI.h"
#include "WindowsDefaultBrowser.h"
#include "WindowsUIElement.h"
#include "WindowsUIOverlayImage.h"
#include "WindowsUserChoice.h"
#include "WinUtils.h"
#include "xpcpublic.h"

#include <comutil.h>
#include <knownfolders.h>
#include <mbstring.h>
#include <objbase.h>
#include <propkey.h>
#include <uiautomation.h>
#include <propvarutil.h>
#include <shellapi.h>
#include <strsafe.h>
#include <windows.h>
#include <windows.foundation.h>
#include <wrl.h>
#include <wrl/wrappers/corewrappers.h>

using namespace ABI::Windows;
using namespace ABI::Windows::Foundation;
using namespace ABI::Windows::Foundation::Collections;
using namespace Microsoft::WRL;
using namespace Microsoft::WRL::Wrappers;

#ifndef __MINGW32__
#  include <windows.applicationmodel.h>
#  include <windows.applicationmodel.activation.h>
#  include <windows.applicationmodel.core.h>
#  include <windows.ui.startscreen.h>
using namespace ABI::Windows::ApplicationModel;
using namespace ABI::Windows::ApplicationModel::Core;
using namespace ABI::Windows::UI::StartScreen;
#endif

#define PRIVATE_BROWSING_BINARY L"private_browsing.exe"

#undef ACCESS_READ

#ifndef MAX_BUF
#  define MAX_BUF 4096
#endif

#define REG_SUCCEEDED(val) (val == ERROR_SUCCESS)

#define REG_FAILED(val) (val != ERROR_SUCCESS)

using namespace mozilla;
using mozilla::intl::Localization;

struct SysFreeStringDeleter {
  void operator()(BSTR aPtr) { ::SysFreeString(aPtr); }
};
using BStrPtr = mozilla::UniquePtr<OLECHAR, SysFreeStringDeleter>;

NS_IMPL_ISUPPORTS_INHERITED(nsWindowsShellService, nsShellService,
                            nsIShellService, nsIWindowsShellService)

/* Enable logging by setting MOZ_LOG to "nsWindowsShellService:5" for debugging
 * purposes. */
static LazyLogModule sLog("nsWindowsShellService");

static bool PollAppsFolderForShortcut(const nsAString& aAppUserModelId,
                                      const TimeDuration aTimeout);

static nsresult OpenKeyForReading(HKEY aKeyRoot, const nsAString& aKeyName,
                                  HKEY* aKey) {
  const nsString& flatName = PromiseFlatString(aKeyName);

  DWORD res = ::RegOpenKeyExW(aKeyRoot, flatName.get(), 0, KEY_READ, aKey);
  switch (res) {
    case ERROR_SUCCESS:
      break;
    case ERROR_ACCESS_DENIED:
      return NS_ERROR_FILE_ACCESS_DENIED;
    case ERROR_FILE_NOT_FOUND:
      return NS_ERROR_NOT_AVAILABLE;
  }

  return NS_OK;
}

nsresult GetHelperPath(nsAutoString& aPath) {
  nsresult rv;
  nsCOMPtr<nsIProperties> directoryService =
      do_GetService(NS_DIRECTORY_SERVICE_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIFile> appHelper;
  rv = directoryService->Get(XRE_EXECUTABLE_FILE, NS_GET_IID(nsIFile),
                             getter_AddRefs(appHelper));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = appHelper->SetNativeLeafName("uninstall"_ns);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = appHelper->AppendNative("helper.exe"_ns);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = appHelper->GetPath(aPath);

  aPath.Insert(L'"', 0);
  aPath.Append(L'"');
  return rv;
}

nsresult LaunchHelper(nsAutoString& aPath) {
  STARTUPINFOW si = {sizeof(si), 0};
  PROCESS_INFORMATION pi = {0};

  if (!CreateProcessW(nullptr, (LPWSTR)aPath.get(), nullptr, nullptr, FALSE, 0,
                      nullptr, nullptr, &si, &pi)) {
    return NS_ERROR_FAILURE;
  }

  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);
  return NS_OK;
}

static bool IsPathDefaultForClass(
    const RefPtr<IApplicationAssociationRegistration>& pAAR, wchar_t* exePath,
    LPCWSTR aClassName) {
  LPWSTR registeredApp;
  bool isProtocol = *aClassName != L'.';
  ASSOCIATIONTYPE queryType = isProtocol ? AT_URLPROTOCOL : AT_FILEEXTENSION;
  HRESULT hr = pAAR->QueryCurrentDefault(aClassName, queryType, AL_EFFECTIVE,
                                         &registeredApp);
  if (FAILED(hr)) {
    return false;
  }

  nsAutoString regAppName(registeredApp);
  CoTaskMemFree(registeredApp);

  // Make sure the application path for this progID is this installation.
  regAppName.AppendLiteral("\\shell\\open\\command");
  HKEY theKey;
  nsresult rv = OpenKeyForReading(HKEY_CLASSES_ROOT, regAppName, &theKey);
  if (NS_FAILED(rv)) {
    return false;
  }

  wchar_t cmdFromReg[MAX_BUF] = L"";
  DWORD len = sizeof(cmdFromReg);
  DWORD res = ::RegQueryValueExW(theKey, nullptr, nullptr, nullptr,
                                 (LPBYTE)cmdFromReg, &len);
  ::RegCloseKey(theKey);
  if (REG_FAILED(res)) {
    return false;
  }

  nsAutoString pathFromReg(cmdFromReg);
  nsLocalFile::CleanupCmdHandlerPath(pathFromReg);

  return _wcsicmp(exePath, pathFromReg.get()) == 0;
}

static bool IsMsixProgIdDefaultForClass(
    const RefPtr<IApplicationAssociationRegistration>& pAAR, LPCWSTR aClass) {
  UniquePtr<wchar_t[]> firefoxProgId;
  const nsresult nsr{GetMsixProgId(aClass, firefoxProgId)};
  if (NS_FAILED(nsr)) {
    return false;
  }

  const ASSOCIATIONTYPE queryType{aClass[0] != L'.' ? AT_URLPROTOCOL
                                                    : AT_FILEEXTENSION};
  LPWSTR defaultProgId;
  const HRESULT hr{pAAR->QueryCurrentDefault(aClass, queryType, AL_EFFECTIVE,
                                             &defaultProgId)};
  if (FAILED(hr)) {
    return false;
  }

  const bool isDefault{::CompareStringOrdinal(firefoxProgId.get(), -1,
                                              defaultProgId, -1,
                                              TRUE) == CSTR_EQUAL};

  CoTaskMemFree(defaultProgId);

  return isDefault;
}

NS_IMETHODIMP
nsWindowsShellService::IsDefaultBrowser(bool aForAllTypes,
                                        bool* aIsDefaultBrowser) {
  *aIsDefaultBrowser = false;

  RefPtr<IApplicationAssociationRegistration> pAAR;
  HRESULT hr = CoCreateInstance(
      CLSID_ApplicationAssociationRegistration, nullptr, CLSCTX_INPROC,
      IID_IApplicationAssociationRegistration, getter_AddRefs(pAAR));
  if (FAILED(hr)) {
    return NS_OK;
  }

  LPCWSTR httpClass{L"http"};
  LPCWSTR htmlClass{L".html"};

  if (widget::WinUtils::HasPackageIdentity()) {
    // Firefox is running as an MSIX package
    *aIsDefaultBrowser = IsMsixProgIdDefaultForClass(pAAR, httpClass);
    if (*aIsDefaultBrowser && aForAllTypes) {
      *aIsDefaultBrowser = IsMsixProgIdDefaultForClass(pAAR, htmlClass);
    }
    return NS_OK;
  }

  wchar_t exePath[MAXPATHLEN] = L"";
  nsresult rv = BinaryPath::GetLong(exePath);

  if (NS_FAILED(rv)) {
    return NS_OK;
  }

  *aIsDefaultBrowser = IsPathDefaultForClass(pAAR, exePath, httpClass);
  if (*aIsDefaultBrowser && aForAllTypes) {
    *aIsDefaultBrowser = IsPathDefaultForClass(pAAR, exePath, htmlClass);
  }
  return NS_OK;
}

NS_IMETHODIMP
nsWindowsShellService::IsDefaultBrowserAsync(bool aForAllTypes,
                                             JSContext* aContext,
                                             mozilla::dom::Promise** _retval) {
  return nsShellService::IsDefaultBrowserAsync(aForAllTypes, aContext, _retval);
}

NS_IMETHODIMP
nsWindowsShellService::IsDefaultHandlerFor(
    const nsAString& aFileExtensionOrProtocol, bool* aIsDefaultHandlerFor) {
  *aIsDefaultHandlerFor = false;

  RefPtr<IApplicationAssociationRegistration> pAAR;
  HRESULT hr = CoCreateInstance(
      CLSID_ApplicationAssociationRegistration, nullptr, CLSCTX_INPROC,
      IID_IApplicationAssociationRegistration, getter_AddRefs(pAAR));
  if (FAILED(hr)) {
    return NS_OK;
  }

  const nsString& flatClass = PromiseFlatString(aFileExtensionOrProtocol);

  if (widget::WinUtils::HasPackageIdentity()) {
    // Firefox is running as an MSIX package
    *aIsDefaultHandlerFor = IsMsixProgIdDefaultForClass(pAAR, flatClass.get());
    return NS_OK;
  }

  wchar_t exePath[MAXPATHLEN] = L"";
  nsresult rv = BinaryPath::GetLong(exePath);

  if (NS_FAILED(rv)) {
    return NS_OK;
  }

  *aIsDefaultHandlerFor = IsPathDefaultForClass(pAAR, exePath, flatClass.get());
  return NS_OK;
}

NS_IMETHODIMP
nsWindowsShellService::QueryCurrentDefaultHandlerFor(
    const nsAString& aFileExtensionOrProtocol, nsAString& aResult) {
  aResult.Truncate();

  RefPtr<IApplicationAssociationRegistration> pAAR;
  HRESULT hr = CoCreateInstance(
      CLSID_ApplicationAssociationRegistration, nullptr, CLSCTX_INPROC,
      IID_IApplicationAssociationRegistration, getter_AddRefs(pAAR));
  if (FAILED(hr)) {
    return NS_OK;
  }

  const nsString& flatClass = PromiseFlatString(aFileExtensionOrProtocol);

  LPWSTR registeredApp;
  bool isProtocol = flatClass.First() != L'.';
  ASSOCIATIONTYPE queryType = isProtocol ? AT_URLPROTOCOL : AT_FILEEXTENSION;
  hr = pAAR->QueryCurrentDefault(flatClass.get(), queryType, AL_EFFECTIVE,
                                 &registeredApp);
  if (hr == HRESULT_FROM_WIN32(ERROR_NO_ASSOCIATION)) {
    return NS_OK;
  }
  NS_ENSURE_HRESULT(hr, NS_ERROR_FAILURE);

  aResult = registeredApp;
  CoTaskMemFree(registeredApp);

  return NS_OK;
}

nsresult nsWindowsShellService::LaunchControlPanelDefaultsSelectionUI() {
  IApplicationAssociationRegistrationUI* pAARUI;
  HRESULT hr = CoCreateInstance(
      CLSID_ApplicationAssociationRegistrationUI, NULL, CLSCTX_INPROC,
      IID_IApplicationAssociationRegistrationUI, (void**)&pAARUI);
  if (SUCCEEDED(hr)) {
    mozilla::UniquePtr<wchar_t[]> appRegName;
    GetAppRegName(appRegName);
    hr = pAARUI->LaunchAdvancedAssociationUI(appRegName.get());
    pAARUI->Release();
  }
  return SUCCEEDED(hr) ? NS_OK : NS_ERROR_FAILURE;
}

NS_IMETHODIMP
nsWindowsShellService::CheckAllProgIDsExist(bool* aResult) {
  *aResult = false;
  nsAutoString aumid;
  if (!mozilla::widget::WinTaskbar::GetAppUserModelID(aumid)) {
    return NS_OK;
  }

  if (widget::WinUtils::HasPackageIdentity()) {
    UniquePtr<wchar_t[]> extraProgID;
    nsresult rv;
    bool result = true;

    // "FirefoxURL".
    rv = GetMsixProgId(L"https", extraProgID);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }
    result = result && CheckProgIDExists(extraProgID.get());

    // "FirefoxHTML".
    rv = GetMsixProgId(L".htm", extraProgID);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }
    result = result && CheckProgIDExists(extraProgID.get());

    // "FirefoxPDF".
    rv = GetMsixProgId(L".pdf", extraProgID);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }
    result = result && CheckProgIDExists(extraProgID.get());

    *aResult = result;
  } else {
    *aResult =
        CheckProgIDExists(FormatProgID(L"FirefoxURL", aumid.get()).get()) &&
        CheckProgIDExists(FormatProgID(L"FirefoxHTML", aumid.get()).get()) &&
        CheckProgIDExists(FormatProgID(L"FirefoxPDF", aumid.get()).get());
  }

  return NS_OK;
}

NS_IMETHODIMP
nsWindowsShellService::CheckBrowserUserChoiceHashes(bool* aResult) {
  *aResult = ::CheckBrowserUserChoiceHashes();
  return NS_OK;
}

NS_IMETHODIMP
nsWindowsShellService::IsUserChoiceProtectionDriverRunning(bool* aResult) {
  *aResult = ::IsUserChoiceProtectionDriverRunning();
  return NS_OK;
}

NS_IMETHODIMP
nsWindowsShellService::CanRenameUserChoiceAssociationKey(
    const nsAString& aAssociation, bool* aResult) {
  const nsString& flatAssociation = PromiseFlatString(aAssociation);
  *aResult = ::CanRenameUserChoiceAssociationKey(flatAssociation.get());
  return NS_OK;
}

NS_IMETHODIMP
nsWindowsShellService::CheckCurrentProcessAUMIDForTesting(
    nsAString& aRetAumid) {
  PWSTR id;
  HRESULT hr = GetCurrentProcessExplicitAppUserModelID(&id);

  if (FAILED(hr)) {
    // Process AUMID may not be set on MSIX builds,
    // if so we should return a dummy value
    if (widget::WinUtils::HasPackageIdentity()) {
      aRetAumid.Assign(u"MSIXAumidTestValue"_ns);
      return NS_OK;
    }
    return NS_ERROR_FAILURE;
  }
  aRetAumid.Assign(id);
  CoTaskMemFree(id);

  return NS_OK;
}

NS_IMETHODIMP
nsWindowsShellService::CanSetDefaultBrowserUserChoice(bool* aResult) {
  *aResult = false;
// If the WDBA is not available, this could never succeed.
#ifdef MOZ_DEFAULT_BROWSER_AGENT
  bool progIDsExist = false;
  bool hashOk = false;
  *aResult = NS_SUCCEEDED(CheckAllProgIDsExist(&progIDsExist)) &&
             progIDsExist &&
             NS_SUCCEEDED(CheckBrowserUserChoiceHashes(&hashOk)) && hashOk;
#endif
  return NS_OK;
}

class __declspec(novtable) IOpenWithLauncher : public IUnknown {
 public:
  // lpszPath selects what the picker offers to set as default. It accepts
  // several shapes:
  //   - a file path:     "C:\\path\\to\\file.pdf"
  //   - a file type:     ".pdf"
  //   - a protocol:      "http"
  //   - a protocol URI:  "https://example.com", "mailto:foo@example.com"
  // flags determines the messaging and actions available of the
  // IOpenWithLauncher dialog.
  virtual HRESULT STDMETHODCALLTYPE Launch(HWND hWndParent, LPCWSTR lpszPath,
                                           int flags) = 0;
};

NS_IMETHODIMP
nsWindowsShellService::LaunchSetDefaultAppPicker(const nsAString& aTarget,
                                                 int32_t aFlags) {
  static constexpr GUID IID_IOpenWithLauncher = {
      0x6a283fe2,
      0xecfa,
      0x4599,
      {0x91, 0xc4, 0xe8, 0x09, 0x57, 0x13, 0x7b, 0x26}};

  nsresult rv;
  nsCOMPtr<nsIWindowsRegKey> regKey =
      do_CreateInstance("@mozilla.org/windows-registry-key;1", &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  // Get the CLSID from the registry.
  rv =
      regKey->Open(nsIWindowsRegKey::ROOT_KEY_LOCAL_MACHINE,
                   u"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\OpenWith"_ns,
                   nsIWindowsRegKey::ACCESS_READ);
  NS_ENSURE_SUCCESS(rv, rv);

  nsAutoString value;
  rv = regKey->ReadStringValue(u"OpenWithLauncher"_ns, value);
  NS_ENSURE_SUCCESS(rv, rv);

  CLSID CLSID_IOpenWithLauncher;
  HRESULT hr = ::CLSIDFromString(value.get(), &CLSID_IOpenWithLauncher);
  NS_ENSURE_HRESULT(hr, NS_ERROR_FAILURE);

  RefPtr<IOpenWithLauncher> pOWL;
  hr = CoCreateInstance(CLSID_IOpenWithLauncher, nullptr, CLSCTX_LOCAL_SERVER,
                        IID_IOpenWithLauncher, getter_AddRefs(pOWL));
  NS_ENSURE_HRESULT(hr, NS_ERROR_NOT_AVAILABLE);

  // Make sure the dialog is foregrounded.
  CoAllowSetForegroundWindow(pOWL, nullptr);

  hr = pOWL->Launch(nullptr, PromiseFlatString(aTarget).get(), aFlags);

  return SUCCEEDED(hr) ? NS_OK : NS_ERROR_FAILURE;
}

NS_IMETHODIMP
nsWindowsShellService::LaunchModernSettingsDialogDefaultApps() {
  return ::LaunchModernSettingsDialogDefaultApps() ? NS_OK : NS_ERROR_FAILURE;
}

static void DisplayOverlayImageWhileVisible(
    nsCOMPtr<nsISerialEventTarget> aSerialEventTarget,
    RefPtr<WindowsUIElement> aElement,
    WindowsUIOverlayImage::DisplayMode aDisplayMode) {
  RefPtr<WindowsUIOverlayImage> overlayImage{
      aElement->CreateOverlayImage(aDisplayMode)};
  if (!overlayImage) {
    return;
  }

  auto timer{std::make_shared<nsCOMPtr<nsITimer>>()};
  auto lastTime{std::make_shared<TimeStamp>(TimeStamp::Now())};
  auto callback{[timer, lastTime, aElement, overlayImage](nsITimer* aTimer) {
    if (aElement->IsMoving().valueOr(true)) {
      // The element is moving or not available
      aTimer->Cancel();
      return;
    }

    if (!overlayImage->IsVisible()) {
      // The overlay image isn't visible
      aTimer->Cancel();
      return;
    }

    const TimeStamp now{TimeStamp::Now()};
    const TimeDuration elapsed{now - *lastTime};
    overlayImage->AdvanceAnimation(elapsed);
    *lastTime = now;
  }};

  const uint32_t kDelayMs{10};
  NS_NewTimerWithCallback(getter_AddRefs(*timer), callback, kDelayMs,
                          nsITimer::TYPE_REPEATING_SLACK,
                          "DisplayOverlayImageWhileVisibleTimer"_ns,
                          aSerialEventTarget);
}

static void DisplayOverlayImageWhenElementIsStill(
    nsCOMPtr<nsISerialEventTarget> aSerialEventTarget,
    RefPtr<WindowsUIElement> aElement,
    WindowsUIOverlayImage::DisplayMode aDisplayMode) {
  auto timer{std::make_shared<nsCOMPtr<nsITimer>>()};
  auto attempts{std::make_shared<int>(0)};
  auto callback{[timer, attempts, aSerialEventTarget, aElement,
                 aDisplayMode](nsITimer* aTimer) {
    const int kMaxAttempts{10};
    if (++(*attempts) > kMaxAttempts) {
      // Maximum number of attempts reached
      aTimer->Cancel();
      return;
    }

    if (aElement->IsMoving().valueOr(true)) {
      // The element is moving
      return;
    }

    // The element is still
    aTimer->Cancel();

    DisplayOverlayImageWhileVisible(aSerialEventTarget, aElement, aDisplayMode);
  }};

  const uint32_t kDelayMs{500};
  NS_NewTimerWithCallback(getter_AddRefs(*timer), callback, kDelayMs,
                          nsITimer::TYPE_REPEATING_SLACK,
                          "DisplayOverlayImageWhenElementIsStillTimer"_ns,
                          aSerialEventTarget);
}

static mozilla::Maybe<WindowsUIOverlayImage::DisplayMode>
GetDisplayKitImagePref() {
  if (!mozilla::IsWin11OrLater()) {
    // The feature is only available in Win11
    return mozilla::Nothing();
  }

  nsAutoString pref;
  Preferences::GetString(
      "browser.shell.displayKitImageBehindSetDefaultBrowserButton", pref);

  if (pref.EqualsIgnoreCase("static")) {
    return mozilla::Some(WindowsUIOverlayImage::DisplayMode::Static);
  }
  if (pref.EqualsIgnoreCase("animated")) {
    return mozilla::Some(WindowsUIOverlayImage::DisplayMode::Animated);
  }
  return mozilla::Nothing();
}

static bool GetFocusPref() {
  return Preferences::GetBool("browser.shell.focusSetDefaultBrowserButton",
                              false);
}

static void HighlightSetDefaultBrowserButton() {
  const bool focus{GetFocusPref()};
  const mozilla::Maybe<WindowsUIOverlayImage::DisplayMode> displayMode{
      GetDisplayKitImagePref()};
  if (!focus && displayMode.isNothing()) {
    return;
  }

  nsCOMPtr<nsISerialEventTarget> serialEventTarget;
  const nsresult nsr{
      NS_CreateBackgroundTaskQueue("HighlightSetDefaultBrowserButtonQueue",
                                   getter_AddRefs(serialEventTarget))};
  if (NS_FAILED(nsr)) {
    return;
  }

  auto timer{std::make_shared<nsCOMPtr<nsITimer>>()};
  auto attempts{std::make_shared<int>(0)};
  auto callback{[timer, attempts, focus, displayMode,
                 serialEventTarget](nsITimer* aTimer) {
    const int kMaxAttempts{40};
    if (++(*attempts) > kMaxAttempts) {
      // Maximum number of attempts reached
      aTimer->Cancel();
      return;
    }

    auto [window, button]{FindSetDefaultBrowserButton()};
    if (!window || !button) {
      // The window or button cannot be found
      return;
    }

    // The window and button are found
    aTimer->Cancel();

    RefPtr<WindowsUIElement> element{new WindowsUIElement(window, button)};
    if (focus) {
      element->Focus();
    }
    if (displayMode.isSome()) {
      DisplayOverlayImageWhenElementIsStill(serialEventTarget, element,
                                            *displayMode);
    }
  }};

  const uint32_t kDelayMs{500};
  NS_NewTimerWithCallback(getter_AddRefs(*timer), callback, kDelayMs,
                          nsITimer::TYPE_REPEATING_SLACK,
                          "HighlightSetDefaultBrowserButtonTimer"_ns,
                          serialEventTarget);
}

NS_IMETHODIMP
nsWindowsShellService::SetDefaultBrowser(bool aForAllUsers) {
  // If running from within a package, don't attempt to set default with
  // the helper, as it will not work and will only confuse our package's
  // virtualized registry.
  nsresult rv = NS_OK;
  if (!widget::WinUtils::HasPackageIdentity()) {
    nsAutoString appHelperPath;
    if (NS_FAILED(GetHelperPath(appHelperPath))) return NS_ERROR_FAILURE;

    if (aForAllUsers) {
      appHelperPath.AppendLiteral(" /SetAsDefaultAppGlobal");
    } else {
      appHelperPath.AppendLiteral(" /SetAsDefaultAppUser");
    }

    rv = LaunchHelper(appHelperPath);
  }

  if (NS_SUCCEEDED(rv)) {
    rv = LaunchModernSettingsDialogDefaultApps();
    if (NS_SUCCEEDED(rv)) {
      HighlightSetDefaultBrowserButton();
    } else {
      // The above call should never really fail, but just in case
      // fall back to showing control panel for all defaults
      rv = LaunchControlPanelDefaultsSelectionUI();
    }
  }

  nsCOMPtr<nsIPrefBranch> prefs(do_GetService(NS_PREFSERVICE_CONTRACTID));
  if (prefs) {
    (void)prefs->SetBoolPref(PREF_CHECKDEFAULTBROWSER, true);
    // Reset the number of times the dialog should be shown
    // before it is silenced.
    (void)prefs->SetIntPref(PREF_DEFAULTBROWSERCHECKCOUNT, 0);
  }

  return rv;
}

NS_IMETHODIMP
nsWindowsShellService::SetDesktopBackgroundFile(nsIFile* aFile,
                                                int32_t aPosition) {
  nsAutoString path;
  nsresult rv = aFile->GetPath(path);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIWindowsRegKey> regKey =
      do_CreateInstance("@mozilla.org/windows-registry-key;1", &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = regKey->Create(nsIWindowsRegKey::ROOT_KEY_CURRENT_USER,
                      u"Control Panel\\Desktop"_ns,
                      nsIWindowsRegKey::ACCESS_SET_VALUE);
  NS_ENSURE_SUCCESS(rv, rv);

  nsAutoString tile;
  nsAutoString style;
  switch (aPosition) {
    case BACKGROUND_TILE:
      style.Assign('0');
      tile.Assign('1');
      break;
    case BACKGROUND_CENTER:
      style.Assign('0');
      tile.Assign('0');
      break;
    case BACKGROUND_STRETCH:
      style.Assign('2');
      tile.Assign('0');
      break;
    case BACKGROUND_FILL:
      style.AssignLiteral("10");
      tile.Assign('0');
      break;
    case BACKGROUND_FIT:
      style.Assign('6');
      tile.Assign('0');
      break;
    case BACKGROUND_SPAN:
      style.AssignLiteral("22");
      tile.Assign('0');
      break;
  }

  rv = regKey->WriteStringValue(u"TileWallpaper"_ns, tile);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = regKey->WriteStringValue(u"WallpaperStyle"_ns, style);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = regKey->Close();
  NS_ENSURE_SUCCESS(rv, rv);

  ::SystemParametersInfoW(SPI_SETDESKWALLPAPER, 0, (PVOID)path.get(),
                          SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);
  return NS_OK;
}

NS_IMETHODIMP
nsWindowsShellService::GetDesktopBackgroundColor(uint32_t* aColor) {
  uint32_t color = ::GetSysColor(COLOR_DESKTOP);
  *aColor =
      (GetRValue(color) << 16) | (GetGValue(color) << 8) | GetBValue(color);
  return NS_OK;
}

NS_IMETHODIMP
nsWindowsShellService::SetDesktopBackgroundColor(uint32_t aColor) {
  int aParameters[2] = {COLOR_BACKGROUND, COLOR_DESKTOP};
  BYTE r = (aColor >> 16);
  BYTE g = (aColor << 16) >> 24;
  BYTE b = (aColor << 24) >> 24;
  COLORREF colors[2] = {RGB(r, g, b), RGB(r, g, b)};

  ::SetSysColors(sizeof(aParameters) / sizeof(int), aParameters, colors);

  nsresult rv;
  nsCOMPtr<nsIWindowsRegKey> regKey =
      do_CreateInstance("@mozilla.org/windows-registry-key;1", &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = regKey->Create(nsIWindowsRegKey::ROOT_KEY_CURRENT_USER,
                      u"Control Panel\\Colors"_ns,
                      nsIWindowsRegKey::ACCESS_SET_VALUE);
  NS_ENSURE_SUCCESS(rv, rv);

  wchar_t rgb[12];
  _snwprintf(rgb, 12, L"%u %u %u", r, g, b);

  rv = regKey->WriteStringValue(u"Background"_ns, nsDependentString(rgb));
  NS_ENSURE_SUCCESS(rv, rv);

  return regKey->Close();
}

enum class ShortcutsLogChange {
  Add,
  Remove,
};

/*
 * Updates information about a shortcut to a shortcuts log in
 * %PROGRAMDATA%\Mozilla-1de4eec8-1241-4177-a864-e594e8d1fb38.
 * (This is the same directory used for update staging.)
 * For more on the shortcuts log format and purpose, consult
 * /toolkit/mozapps/installer/windows/nsis/common.nsh.
 *
 * The shortcuts log modified here is named after the currently
 * running application and current user SID. For example:
 * Firefox_$SID_shortcuts.ini.
 *
 * A new file will be created when the first shortcut is added.
 * If a matching shortcut already exists, a new one will not
 * be appended. The file will not be deleted if the last one is
 * removed.
 *
 * In an ideal world this function would not need aShortcutsLogDir
 * passed to it, but it is called by at least one function that runs
 * asynchronously, and is therefore unable to use nsDirectoryService
 * to look it up itself.
 */
static nsresult UpdateShortcutInLog(nsIFile* aShortcutsLogDir,
                                    KNOWNFOLDERID aFolderId,
                                    ShortcutsLogChange aChange,
                                    const nsAString& aShortcutRelativePath) {
  // the section inside the shortcuts log
  nsAutoCString section;
  // the shortcuts log wants "Programs" shortcuts in its "STARTMENU" section
  if (aFolderId == FOLDERID_CommonPrograms || aFolderId == FOLDERID_Programs) {
    section.Assign("STARTMENU");
  } else if (aFolderId == FOLDERID_PublicDesktop ||
             aFolderId == FOLDERID_Desktop) {
    section.Assign("DESKTOP");
  } else {
    return NS_ERROR_INVALID_ARG;
  }

  nsCOMPtr<nsIFile> shortcutsLog;
  nsresult rv = aShortcutsLogDir->GetParent(getter_AddRefs(shortcutsLog));
  NS_ENSURE_SUCCESS(rv, rv);

  nsAutoCString appName;
  nsCOMPtr<nsIXULAppInfo> appInfo =
      do_GetService("@mozilla.org/xre/app-info;1");
  rv = appInfo->GetName(appName);
  NS_ENSURE_SUCCESS(rv, rv);

  auto userSid = GetCurrentUserStringSid();
  if (!userSid) {
    return NS_ERROR_FILE_NOT_FOUND;
  }

  nsAutoString filename;
  filename.AppendPrintf("%s_%ls_shortcuts.ini", appName.get(), userSid.get());
  rv = shortcutsLog->Append(filename);
  NS_ENSURE_SUCCESS(rv, rv);

  nsINIParser parser;
  bool shortcutsLogEntryExists = false;
  nsAutoCString keyName, shortcutRelativePath, iniShortcut;

  shortcutRelativePath = NS_ConvertUTF16toUTF8(aShortcutRelativePath);

  // Last key that was valid.
  nsAutoCString lastValidKey;
  // Last key where the filename was found.
  nsAutoCString fileFoundAtKeyName;

  // If the shortcuts log exists, find either an existing matching
  // entry, or the next available shortcut index.
  rv = parser.Init(shortcutsLog);
  if (NS_SUCCEEDED(rv)) {
    for (int i = 0;; i++) {
      keyName.AssignLiteral("Shortcut");
      keyName.AppendInt(i);
      rv = parser.GetString(section.get(), keyName.get(), iniShortcut);
      if (NS_FAILED(rv) && rv != NS_ERROR_FAILURE) {
        return rv;
      }

      if (rv == NS_ERROR_FAILURE) {
        // This is the end of the file (as far as we're concerned.)
        break;
      } else if (iniShortcut.Equals(shortcutRelativePath)) {
        shortcutsLogEntryExists = true;
        fileFoundAtKeyName = keyName;
      }

      lastValidKey = keyName;
    }
  } else if (rv == NS_ERROR_FILE_NOT_FOUND) {
    // If the file doesn't exist, then start at Shortcut0.
    // When removing, this does nothing; when adding, this is always
    // a safe place to start.
    keyName.AssignLiteral("Shortcut0");
  } else {
    return rv;
  }

  bool changed = false;
  if (aChange == ShortcutsLogChange::Add && !shortcutsLogEntryExists) {
    parser.SetString(section.get(), keyName.get(), shortcutRelativePath.get());
    changed = true;
  } else if (aChange == ShortcutsLogChange::Remove && shortcutsLogEntryExists) {
    // Don't just remove it! The first missing index is considered
    // the end of the log. Instead, move the last one in, then delete
    // the last one, reducing the total length by one.
    parser.SetString(section.get(), fileFoundAtKeyName.get(),
                     iniShortcut.get());
    parser.DeleteString(section.get(), lastValidKey.get());
    changed = true;
  }

  if (changed) {
    // We write this ourselves instead of using parser->WriteToFile because
    // the INI parser in our uninstaller needs to read this, and only supports
    // UTF-16LE encoding. nsINIParser does not support UTF-16.
    nsAutoCString formatted;
    parser.WriteToString(formatted);
    FILE* writeFile;
    rv = shortcutsLog->OpenANSIFileDesc("w,ccs=UTF-16LE", &writeFile);
    NS_ENSURE_SUCCESS(rv, rv);
    NS_ConvertUTF8toUTF16 formattedUTF16(formatted);
    if (fwrite(formattedUTF16.get(), sizeof(wchar_t), formattedUTF16.Length(),
               writeFile) != formattedUTF16.Length()) {
      fclose(writeFile);
      return NS_ERROR_FAILURE;
    }
    fclose(writeFile);
  }

  return NS_OK;
}

nsresult CreateShellLinkObject(nsIFile* aBinary,
                               const CopyableTArray<nsString>& aArguments,
                               const nsAString& aDescription,
                               nsIFile* aIconFile, uint16_t aIconIndex,
                               const nsAString& aAppUserModelId,
                               IShellLinkW** aLink) {
  RefPtr<IShellLinkW> link;
  HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                IID_IShellLinkW, getter_AddRefs(link));
  NS_ENSURE_HRESULT(hr, NS_ERROR_FAILURE);

  nsString path(aBinary->NativePath());
  link->SetPath(path.get());

  wchar_t workingDir[MAX_PATH + 1];
  wcscpy_s(workingDir, MAX_PATH + 1, aBinary->NativePath().get());
  PathRemoveFileSpecW(workingDir);
  link->SetWorkingDirectory(workingDir);

  if (!aDescription.IsEmpty()) {
    link->SetDescription(PromiseFlatString(aDescription).get());
  }

  // TODO: Properly escape quotes in the string, see bug 1604287.
  nsString arguments;
  for (const auto& arg : aArguments) {
    arguments += u"\""_ns + arg + u"\" "_ns;
  }

  link->SetArguments(arguments.get());

  if (aIconFile) {
    nsString icon(aIconFile->NativePath());
    link->SetIconLocation(icon.get(), aIconIndex);
  }

  if (!aAppUserModelId.IsEmpty()) {
    RefPtr<IPropertyStore> propStore;
    hr = link->QueryInterface(IID_IPropertyStore, getter_AddRefs(propStore));
    NS_ENSURE_HRESULT(hr, NS_ERROR_FAILURE);

    PROPVARIANT pv;
    if (FAILED(InitPropVariantFromString(
            PromiseFlatString(aAppUserModelId).get(), &pv))) {
      return NS_ERROR_FAILURE;
    }

    hr = propStore->SetValue(PKEY_AppUserModel_ID, pv);
    PropVariantClear(&pv);
    NS_ENSURE_HRESULT(hr, NS_ERROR_FAILURE);

    hr = propStore->Commit();
    NS_ENSURE_HRESULT(hr, NS_ERROR_FAILURE);
  }

  link.forget(aLink);
  return NS_OK;
}

struct ShortcutLocations {
  KNOWNFOLDERID folderId;
  nsCOMPtr<nsIFile> shortcutsLogDir;
  nsCOMPtr<nsIFile> shortcutFile;
};

static nsresult CreateShortcutImpl(nsIFile* aBinary,
                                   const CopyableTArray<nsString>& aArguments,
                                   const nsAString& aDescription,
                                   nsIFile* aIconFile, uint16_t aIconIndex,
                                   const nsAString& aAppUserModelId,
                                   const ShortcutLocations& location,
                                   const nsAString& aShortcutRelativePath) {
  NS_ENSURE_ARG(aBinary);
  NS_ENSURE_ARG(aIconFile);

  if (xpc::IsInAutomation() && !StaticPrefs::browser_shell_shortcut_test()) {
    // Don't create shortcuts under test.
    return NS_OK;
  }

  nsresult rv =
      UpdateShortcutInLog(location.shortcutsLogDir, location.folderId,
                          ShortcutsLogChange::Add, aShortcutRelativePath);
  NS_ENSURE_SUCCESS(rv, rv);

  RefPtr<IShellLinkW> link;
  rv = CreateShellLinkObject(aBinary, aArguments, aDescription, aIconFile,
                             aIconIndex, aAppUserModelId, getter_AddRefs(link));
  NS_ENSURE_SUCCESS(rv, rv);

  RefPtr<IPersistFile> persist;
  HRESULT hr = link->QueryInterface(IID_IPersistFile, getter_AddRefs(persist));
  NS_ENSURE_HRESULT(hr, NS_ERROR_FAILURE);

  hr = persist->Save(location.shortcutFile->NativePath().get(), TRUE);
  NS_ENSURE_HRESULT(hr, NS_ERROR_FAILURE);

  return NS_OK;
}

static Result<ShortcutLocations, nsresult> GetShortcutPaths(
    const nsAString& aShortcutFolder, const nsAString& aShortcutRelativePath) {
  KNOWNFOLDERID folderId;
  if (aShortcutFolder.Equals(L"Programs")) {
    folderId = FOLDERID_Programs;
  } else if (aShortcutFolder.Equals(L"Desktop")) {
    folderId = FOLDERID_Desktop;
  } else {
    return Err(NS_ERROR_INVALID_ARG);
  }

  nsCOMPtr<nsIFile> updRoot, shortcutsLogDir;
  nsresult nsrv =
      NS_GetSpecialDirectory(XRE_UPDATE_ROOT_DIR, getter_AddRefs(updRoot));
  NS_ENSURE_SUCCESS(nsrv, Err(nsrv));
  nsrv = updRoot->GetParent(getter_AddRefs(shortcutsLogDir));
  NS_ENSURE_SUCCESS(nsrv, Err(nsrv));

  nsCOMPtr<nsIFile> shortcutFile;
  if (folderId == FOLDERID_Programs) {
    nsrv = NS_GetSpecialDirectory(NS_WIN_PROGRAMS_DIR,
                                  getter_AddRefs(shortcutFile));
  } else if (folderId == FOLDERID_Desktop) {
    nsrv =
        NS_GetSpecialDirectory(NS_OS_DESKTOP_DIR, getter_AddRefs(shortcutFile));
  } else {
    return Err(NS_ERROR_FILE_NOT_FOUND);
  }

  if (NS_FAILED(nsrv)) {
    return Err(NS_ERROR_FILE_NOT_FOUND);
  }

  nsrv = shortcutFile->AppendRelativePath(aShortcutRelativePath);
  NS_ENSURE_SUCCESS(nsrv, Err(nsrv));

  ShortcutLocations result{};
  result.folderId = folderId;
  result.shortcutsLogDir = std::move(shortcutsLogDir);
  result.shortcutFile = std::move(shortcutFile);
  return result;
}

NS_IMETHODIMP
nsWindowsShellService::CreateShortcut(nsIFile* aBinary,
                                      const nsTArray<nsString>& aArguments,
                                      const nsAString& aDescription,
                                      nsIFile* aIconFile, uint16_t aIconIndex,
                                      const nsAString& aAppUserModelId,
                                      const nsAString& aShortcutFolder,
                                      const nsAString& aShortcutRelativePath,
                                      JSContext* aCx, dom::Promise** aPromise) {
  if (!NS_IsMainThread()) {
    return NS_ERROR_NOT_SAME_THREAD;
  }

  ErrorResult rv;
  RefPtr<dom::Promise> promise =
      dom::Promise::Create(xpc::CurrentNativeGlobal(aCx), rv);

  if (MOZ_UNLIKELY(rv.Failed())) {
    return rv.StealNSResult();
  }

  ShortcutLocations location =
      MOZ_TRY(GetShortcutPaths(aShortcutFolder, aShortcutRelativePath));

  nsCOMPtr<nsIFile> parentDirectory;
  nsresult nsrv;
  nsrv = location.shortcutFile->GetParent(getter_AddRefs(parentDirectory));
  NS_ENSURE_SUCCESS(nsrv, nsrv);
  nsrv = parentDirectory->Create(nsIFile::DIRECTORY_TYPE, 0755);
  if (NS_FAILED(nsrv) && nsrv != NS_ERROR_FILE_ALREADY_EXISTS) {
    return nsrv;
  }

  auto promiseHolder = MakeRefPtr<nsMainThreadPtrHolder<dom::Promise>>(
      "CreateShortcut promise", promise);

  nsCOMPtr<nsIFile> binary(aBinary);
  nsCOMPtr<nsIFile> iconFile(aIconFile);
  NS_DispatchBackgroundTask(
      NS_NewRunnableFunction(
          "CreateShortcut",
          [binary, aArguments = CopyableTArray<nsString>(aArguments),
           aDescription = nsString{aDescription}, iconFile, aIconIndex,
           aAppUserModelId = nsString{aAppUserModelId},
           location = std::move(location),
           aShortcutFolder = nsString{aShortcutFolder},
           aShortcutRelativePath = nsString{aShortcutRelativePath},
           promiseHolder = std::move(promiseHolder)] {
            nsresult rv = CreateShortcutImpl(
                binary.get(), aArguments, aDescription, iconFile.get(),
                aIconIndex, aAppUserModelId, location, aShortcutRelativePath);

            NS_DispatchToMainThread(NS_NewRunnableFunction(
                "CreateShortcut callback",
                [rv, shortcutFile = location.shortcutFile,
                 promiseHolder = std::move(promiseHolder)] {
                  dom::Promise* promise = promiseHolder.get()->get();

                  if (NS_SUCCEEDED(rv)) {
                    promise->MaybeResolve(shortcutFile->NativePath());
                  } else {
                    promise->MaybeReject(rv);
                  }
                }));
          }),
      NS_DISPATCH_EVENT_MAY_BLOCK);

  promise.forget(aPromise);
  return NS_OK;
}

static nsresult DeleteShortcutImpl(const ShortcutLocations& aLocation,
                                   const nsAString& aShortcutRelativePath) {
  if (xpc::IsInAutomation() && !StaticPrefs::browser_shell_shortcut_test()) {
    // Don't delete shortcuts under test.
    return NS_OK;
  }

  // Do the removal first so an error keeps it in the log.
  nsresult rv = aLocation.shortcutFile->Remove(false);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = UpdateShortcutInLog(aLocation.shortcutsLogDir, aLocation.folderId,
                           ShortcutsLogChange::Remove, aShortcutRelativePath);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

NS_IMETHODIMP
nsWindowsShellService::DeleteShortcut(const nsAString& aShortcutFolder,
                                      const nsAString& aShortcutRelativePath,
                                      JSContext* aCx, dom::Promise** aPromise) {
  if (!NS_IsMainThread()) {
    return NS_ERROR_NOT_SAME_THREAD;
  }

  ErrorResult rv;
  RefPtr<dom::Promise> promise =
      dom::Promise::Create(xpc::CurrentNativeGlobal(aCx), rv);

  if (MOZ_UNLIKELY(rv.Failed())) {
    return rv.StealNSResult();
  }

  ShortcutLocations location =
      MOZ_TRY(GetShortcutPaths(aShortcutFolder, aShortcutRelativePath));

  auto promiseHolder = MakeRefPtr<nsMainThreadPtrHolder<dom::Promise>>(
      "DeleteShortcut promise", promise);

  NS_DispatchBackgroundTask(
      NS_NewRunnableFunction(
          "DeleteShortcut",
          [aShortcutFolder = nsString{aShortcutFolder},
           aShortcutRelativePath = nsString{aShortcutRelativePath},
           location = std::move(location),
           promiseHolder = std::move(promiseHolder)] {
            nsresult rv = DeleteShortcutImpl(location, aShortcutRelativePath);

            NS_DispatchToMainThread(NS_NewRunnableFunction(
                "DeleteShortcut callback",
                [rv, shortcutFile = location.shortcutFile,
                 promiseHolder = std::move(promiseHolder)] {
                  dom::Promise* promise = promiseHolder.get()->get();

                  if (NS_SUCCEEDED(rv)) {
                    promise->MaybeResolve(shortcutFile->NativePath());
                  } else {
                    promise->MaybeReject(rv);
                  }
                }));
          }),
      NS_DISPATCH_EVENT_MAY_BLOCK);

  promise.forget(aPromise);
  return NS_OK;
}

NS_IMETHODIMP
nsWindowsShellService::GetLaunchOnLoginShortcuts(
    nsTArray<nsString>& aShortcutPaths) {
  aShortcutPaths.Clear();

  // Get AppData\\Roaming folder using a known folder ID
  RefPtr<IKnownFolderManager> fManager;
  RefPtr<IKnownFolder> roamingAppData;
  LPWSTR roamingAppDataW;
  nsString roamingAppDataNS;
  HRESULT hr =
      CoCreateInstance(CLSID_KnownFolderManager, nullptr, CLSCTX_INPROC_SERVER,
                       IID_IKnownFolderManager, getter_AddRefs(fManager));
  if (FAILED(hr)) {
    return NS_ERROR_ABORT;
  }
  fManager->GetFolder(FOLDERID_RoamingAppData,
                      roamingAppData.StartAssignment());
  hr = roamingAppData->GetPath(0, &roamingAppDataW);
  if (FAILED(hr)) {
    return NS_ERROR_FILE_NOT_FOUND;
  }

  // Append startup folder to AppData\\Roaming
  roamingAppDataNS.Assign(roamingAppDataW);
  CoTaskMemFree(roamingAppDataW);
  nsString startupFolder =
      roamingAppDataNS +
      u"\\Microsoft\\Windows\\Start Menu\\Programs\\Startup"_ns;
  nsString startupFolderWildcard = startupFolder + u"\\*.lnk"_ns;

  // Get known path for binary file for later comparison with shortcuts.
  // Returns lowercase file path which should be fine for Windows as all
  // directories and files are case-insensitive by default.
  RefPtr<nsIFile> binFile;
  nsString binPath;
  nsresult rv = XRE_GetBinaryPath(binFile.StartAssignment());
  if (FAILED(rv)) {
    return NS_ERROR_FAILURE;
  }
  rv = binFile->GetPath(binPath);
  if (FAILED(rv)) {
    return NS_ERROR_FILE_UNRECOGNIZED_PATH;
  }

  // Check for if first file exists with a shortcut extension (.lnk)
  WIN32_FIND_DATAW ffd;
  HANDLE fileHandle = INVALID_HANDLE_VALUE;
  fileHandle = FindFirstFileW(startupFolderWildcard.get(), &ffd);
  if (fileHandle == INVALID_HANDLE_VALUE) {
    // This means that no files were found in the folder which
    // doesn't imply an error. Most of the time the user won't
    // have any shortcuts here.
    return NS_OK;
  }

  do {
    // Extract shortcut target path from every
    // shortcut in the startup folder.
    nsString fileName(ffd.cFileName);
    RefPtr<IShellLinkW> link;
    RefPtr<IPersistFile> ppf;
    nsString target;
    target.SetLength(MAX_PATH);
    CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                     IID_IShellLinkW, getter_AddRefs(link));
    hr = link->QueryInterface(IID_IPersistFile, getter_AddRefs(ppf));
    if (NS_WARN_IF(FAILED(hr))) {
      continue;
    }
    nsString filePath = startupFolder + u"\\"_ns + fileName;
    hr = ppf->Load(filePath.get(), STGM_READ);
    if (NS_WARN_IF(FAILED(hr))) {
      continue;
    }
    hr = link->GetPath(target.get(), MAX_PATH, nullptr, 0);
    if (NS_WARN_IF(FAILED(hr))) {
      continue;
    }

    // If shortcut target matches known binary file value
    // then add the path to the shortcut as a valid
    // startup shortcut. This has to be a substring search as
    // the user could have added unknown command line arguments
    // to the shortcut.
    if (_wcsnicmp(target.get(), binPath.get(), binPath.Length()) == 0) {
      aShortcutPaths.AppendElement(filePath);
    }
  } while (FindNextFile(fileHandle, &ffd) != 0);
  FindClose(fileHandle);
  return NS_OK;
}

// Look for any installer-created shortcuts in the given location that match
// the given AUMID and EXE Path. If one is found, output its path.
//
// NOTE: DO NOT USE if a false negative (mismatch) is unacceptable.
// aExePath is compared directly to the path retrieved from the shortcut.
// Due to the presence of symlinks or other filesystem issues, it's possible
// for different paths to refer to the same file, which would cause the check
// to fail.
// This should rarely be an issue as we are most likely to be run from a path
// written by the installer (shortcut, association, launch from installer),
// which also wrote the shortcuts. But it is possible.
//
// aCSIDL   the CSIDL of the directory to look for matching shortcuts in
// aAUMID   the AUMID to check for
// aExePath the target exe path to check for, should be a long path where
//          possible
// aShortcutSubstring a substring to limit which shortcuts in aCSIDL are
//                    inspected for a match. Only shortcuts whose filename
//                    contains this substring will be considered
// aShortcutPath outparam, set to matching shortcut path if NS_OK is returned.
//
// Returns
//   NS_ERROR_FAILURE on errors before any shortcuts were loaded
//   NS_ERROR_FILE_NOT_FOUND if no shortcuts matching aShortcutSubstring exist
//   NS_ERROR_FILE_ALREADY_EXISTS if shortcuts were found but did not match
//                                aAUMID or aExePath
//   NS_OK if a matching shortcut is found
static nsresult GetMatchingShortcut(int aCSIDL, const nsAString& aAUMID,
                                    const wchar_t aExePath[MAXPATHLEN],
                                    const nsAString& aShortcutSubstring,
                                    /* out */ nsAString& aShortcutPath) {
  nsresult result = NS_ERROR_FAILURE;

  wchar_t folderPath[MAX_PATH] = {};
  HRESULT hr = SHGetFolderPathW(nullptr, aCSIDL, nullptr, SHGFP_TYPE_CURRENT,
                                folderPath);
  if (NS_WARN_IF(FAILED(hr))) {
    return NS_ERROR_FAILURE;
  }
  if (wcscat_s(folderPath, MAX_PATH, L"\\") != 0) {
    return NS_ERROR_FAILURE;
  }

  // Get list of shortcuts in aCSIDL
  nsAutoString pattern(folderPath);
  pattern.AppendLiteral("*.lnk");

  WIN32_FIND_DATAW findData = {};
  HANDLE hFindFile = FindFirstFileW(pattern.get(), &findData);
  if (hFindFile == INVALID_HANDLE_VALUE) {
    (void)NS_WARN_IF(GetLastError() != ERROR_FILE_NOT_FOUND);
    return NS_ERROR_FILE_NOT_FOUND;
  }
  // Past this point we don't return until the end of the function,
  // when FindClose() is called.

  // todo: improve return values here
  do {
    // Skip any that don't contain aShortcutSubstring
    // This is a case sensitive comparison, but that's probably fine for
    // the vast majority of cases -- and certainly for all the ones where
    // a shortcut was created by the installer.
    if (StrStrIW(findData.cFileName,
                 PromiseFlatString(aShortcutSubstring).get()) == NULL) {
      continue;
    }

    nsAutoString path(folderPath);
    path.Append(findData.cFileName);

    // Create a shell link object for loading the shortcut
    RefPtr<IShellLinkW> link;
    HRESULT hr =
        CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                         IID_IShellLinkW, getter_AddRefs(link));
    if (NS_WARN_IF(FAILED(hr))) {
      continue;
    }

    // Load
    RefPtr<IPersistFile> persist;
    hr = link->QueryInterface(IID_IPersistFile, getter_AddRefs(persist));
    if (NS_WARN_IF(FAILED(hr))) {
      continue;
    }

    hr = persist->Load(path.get(), STGM_READ);
    if (FAILED(hr)) {
      if (NS_WARN_IF(hr != HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND))) {
        // empty branch, result unchanged but warning issued
      } else {
        // If we've ever gotten past this block, result will already be
        // NS_ERROR_FILE_ALREADY_EXISTS, which is a more accurate error
        // than NS_ERROR_FILE_NOT_FOUND.
        if (result != NS_ERROR_FILE_ALREADY_EXISTS) {
          result = NS_ERROR_FILE_NOT_FOUND;
        }
      }
      continue;
    }
    result = NS_ERROR_FILE_ALREADY_EXISTS;

    // Check the AUMID
    RefPtr<IPropertyStore> propStore;
    hr = link->QueryInterface(IID_IPropertyStore, getter_AddRefs(propStore));
    if (NS_WARN_IF(FAILED(hr))) {
      continue;
    }

    PROPVARIANT pv;
    hr = propStore->GetValue(PKEY_AppUserModel_ID, &pv);
    if (NS_WARN_IF(FAILED(hr))) {
      continue;
    }

    wchar_t storedAUMID[MAX_PATH];
    hr = PropVariantToString(pv, storedAUMID, MAX_PATH);
    PropVariantClear(&pv);
    if (NS_WARN_IF(FAILED(hr))) {
      continue;
    }

    if (!aAUMID.Equals(storedAUMID)) {
      continue;
    }

    // Check the exe path
    static_assert(MAXPATHLEN == MAX_PATH);
    wchar_t storedExePath[MAX_PATH] = {};
    // With no flags GetPath gets a long path
    hr = link->GetPath(storedExePath, std::size(storedExePath), nullptr, 0);
    if (FAILED(hr) || hr == S_FALSE) {
      continue;
    }
    // Case insensitive path comparison
    if (wcsnicmp(storedExePath, aExePath, MAXPATHLEN) == 0) {
      aShortcutPath.Assign(path);
      result = NS_OK;
      break;
    }
  } while (FindNextFileW(hFindFile, &findData));

  FindClose(hFindFile);

  return result;
}

static nsresult FindPinnableShortcut(const nsAString& aAppUserModelId,
                                     const nsAString& aShortcutSubstring,
                                     const bool aPrivateBrowsing,
                                     nsAString& aShortcutPath) {
  wchar_t exePath[MAXPATHLEN] = {};
  if (NS_WARN_IF(NS_FAILED(BinaryPath::GetLong(exePath)))) {
    return NS_ERROR_FAILURE;
  }

  if (aPrivateBrowsing) {
    if (!PathRemoveFileSpecW(exePath)) {
      return NS_ERROR_FAILURE;
    }
    if (!PathAppendW(exePath, L"private_browsing.exe")) {
      return NS_ERROR_FAILURE;
    }
  }

  int shortcutCSIDLs[] = {CSIDL_COMMON_PROGRAMS, CSIDL_PROGRAMS};
  for (int shortcutCSIDL : shortcutCSIDLs) {
    // GetMatchingShortcut may fail when the exe path doesn't match, even
    // if it refers to the same file. This should be rare, and the worst
    // outcome would be failure to pin, so the risk is acceptable.
    nsresult rv = GetMatchingShortcut(shortcutCSIDL, aAppUserModelId, exePath,
                                      aShortcutSubstring, aShortcutPath);
    if (NS_SUCCEEDED(rv)) {
      return NS_OK;
    }
  }

  return NS_ERROR_FILE_NOT_FOUND;
}

static bool HasPinnableShortcutImpl(const nsAString& aAppUserModelId,
                                    const bool aPrivateBrowsing,
                                    const nsAutoString& aShortcutSubstring) {
  // unused by us, but required
  nsAutoString shortcutPath;
  nsresult rv = FindPinnableShortcut(aAppUserModelId, aShortcutSubstring,
                                     aPrivateBrowsing, shortcutPath);
  if (SUCCEEDED(rv)) {
    return true;
  }

  return false;
}

NS_IMETHODIMP nsWindowsShellService::HasPinnableShortcut(
    const nsAString& aAppUserModelId, const bool aPrivateBrowsing,
    JSContext* aCx, dom::Promise** aPromise) {
  if (!NS_IsMainThread()) {
    return NS_ERROR_NOT_SAME_THREAD;
  }

  ErrorResult rv;
  RefPtr<dom::Promise> promise =
      dom::Promise::Create(xpc::CurrentNativeGlobal(aCx), rv);

  if (MOZ_UNLIKELY(rv.Failed())) {
    return rv.StealNSResult();
  }

  auto promiseHolder = MakeRefPtr<nsMainThreadPtrHolder<dom::Promise>>(
      "HasPinnableShortcut promise", promise);

  NS_DispatchBackgroundTask(
      NS_NewRunnableFunction(
          "HasPinnableShortcut",
          [aAppUserModelId = nsString{aAppUserModelId}, aPrivateBrowsing,
           promiseHolder = std::move(promiseHolder)] {
            nsAutoString shortcutSubstring;
            shortcutSubstring.AssignLiteral(MOZ_APP_DISPLAYNAME);
            bool hasPinnableShortcut = HasPinnableShortcutImpl(
                aAppUserModelId, aPrivateBrowsing, shortcutSubstring);

            NS_DispatchToMainThread(NS_NewRunnableFunction(
                "HasPinnableShortcut callback",
                [hasPinnableShortcut,
                 promiseHolder = std::move(promiseHolder)] {
                  dom::Promise* promise = promiseHolder.get()->get();

                  promise->MaybeResolve(hasPinnableShortcut);
                }));
          }),
      NS_DISPATCH_EVENT_MAY_BLOCK);

  promise.forget(aPromise);
  return NS_OK;
}

static nsresult EnsureShellAppsFolderShortcut(
    const nsAString& aAppUserModelId) {
  MOZ_ASSERT(!NS_IsMainThread());

  // Verify shortcut is visible to `shell:appsfolder`. Shortcut creation -
  // during install or runtime - causes a race between it propagating to the
  // virtual `shell:appsfolder` and attempts to pin via `ITaskbarManager`,
  // resulting in pin failures when the latter occurs before the former. We can
  // skip this when we're in an MSIX build.
  if (!widget::WinUtils::HasPackageIdentity() &&
      !PollAppsFolderForShortcut(aAppUserModelId,
                                 TimeDuration::FromSeconds(15))) {
    return NS_ERROR_FILE_NOT_FOUND;
  }

  return NS_OK;
}

/* This function pins a shortcut to the taskbar based on its location. While
 * Windows 11 only needs the `aAppUserModelId`, `aShortcutPath` is required
 * for pinning in Windows 10.
 * @param aAppUserModelId
 *        The same string used to create an lnk file.
 * @param aShortcutPaths
 *        Path for existing shortcuts (e.g., start menu)
 */
NS_IMETHODIMP
nsWindowsShellService::PinShortcutToTaskbar(
    const nsAString& aAppUserModelId, const nsAString& aShortcutFolder,
    const nsAString& aShortcutRelativePath, JSContext* aCx,
    dom::Promise** aPromise) {
  NS_ENSURE_ARG_POINTER(aCx);
  NS_ENSURE_ARG_POINTER(aPromise);

  if (!NS_IsMainThread()) {
    return NS_ERROR_NOT_SAME_THREAD;
  }

  // First available on 1809
  if (!IsWin10Sep2018UpdateOrLater()) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  ErrorResult rv;
  RefPtr<dom::Promise> promise =
      dom::Promise::Create(xpc::CurrentNativeGlobal(aCx), rv);

  if (MOZ_UNLIKELY(rv.Failed())) {
    return rv.StealNSResult();
  }

  ShortcutLocations location =
      MOZ_TRY(GetShortcutPaths(aShortcutFolder, aShortcutRelativePath));

  auto promiseHolder = MakeRefPtr<nsMainThreadPtrHolder<dom::Promise>>(
      "pinShortcutToTaskbar promise", promise);

  NS_DispatchBackgroundTask(
      NS_NewRunnableFunction(
          "pinShortcutToTaskbar",
          [aumid = nsString{aAppUserModelId}, location = std::move(location),
           promiseHolder = std::move(promiseHolder)] {
            nsresult rv = EnsureShellAppsFolderShortcut(aumid);

            NS_DispatchToMainThread(NS_NewRunnableFunction(
                "pinShortcutToTaskbar callback",
                [aumid, rv, location,
                 promiseHolder = std::move(promiseHolder)] {
                  dom::Promise* promise = promiseHolder.get()->get();

                  auto shortcut_path = location.shortcutFile->NativePath();
                  if (NS_SUCCEEDED(rv)) {
                    shell_windows_taskbar_pin_app_to_taskbar(
                        &aumid, &shortcut_path, /* fire_and_forget */ false,
                        /* prompt_only */ false, promise);
                  } else {
                    promise->MaybeReject(rv);
                  }
                }));
          }),
      NS_DISPATCH_EVENT_MAY_BLOCK);

  promise.forget(aPromise);
  return NS_OK;
}

NS_IMETHODIMP
nsWindowsShellService::UnpinShortcutFromTaskbar(
    const nsAString& aShortcutFolder, const nsAString& aShortcutRelativePath) {
  ShortcutLocations location =
      MOZ_TRY(GetShortcutPaths(aShortcutFolder, aShortcutRelativePath));

  mozilla::PathString path = location.shortcutFile->NativePath();
  return shell_windows_taskbar_unpin_shortcut_from_taskbar(&path);
}

// There's a delay between shortcuts being created in locations visible to
// `shell:appsfolder` and that information being propogated to
// `shell:appsfolder`. APIs like `ITaskbarManager` pinning rely on said
// shortcuts being visible to `shell:appsfolder`, so we have to introduce a wait
// until they're visible when creating these shortcuts at runtime.
static bool PollAppsFolderForShortcut(const nsAString& aAppUserModelId,
                                      const TimeDuration aTimeout) {
  MOZ_DIAGNOSTIC_ASSERT(!NS_IsMainThread(),
                        "PollAppsFolderForShortcut blocks and should be called "
                        "off main thread only");

  // Implementation note: it was taken into consideration at the time of writing
  // to implement this with `SHChangeNotifyRegister` and a `HWND_MESSAGE`
  // window. This added significant complexity in terms of resource management
  // and control flow that was deemed excessive for a function that is rarely
  // run. Absent evidence that we're consuming excessive system resources, this
  // simple, poll-based approach seemed more appropriate.
  //
  // If in the future it seems appropriate to modify this to be event based,
  // here are some of the lessons learned during the investigation:
  //   - `shell:appsfolder` is a virtual directory, composed of shortcut files
  //     with unique AUMIDs from
  //     `[%PROGRAMDATA%|%APPDATA%]\Microsoft\Windows\Start Menu\Programs`.
  //   - `shell:appsfolder` does not have a full path in the filesystem,
  //     therefore does not work with most file watching APIs.
  //   - `SHChangeNotifyRegister` should listen for `SHCNE_UPDATEDIR` on
  //     `FOLDERID_AppsFolder`. `SHCNE_CREATE` events are not issued for
  //     shortcuts added to `FOLDERID_AppsFolder` likely due to it's virtual
  //     nature.
  //   - The mechanism for inspecting the `shell:appsfolder` for a shortcut with
  //     matching AUMID is the same in an event-based implementation due to
  //     `SHCNE_UPDATEDIR` events include the modified folder, but not the
  //     modified file.

  TimeStamp start = TimeStamp::Now();

  ComPtr<IShellItem> appsFolder;
  HRESULT hr = SHGetKnownFolderItem(FOLDERID_AppsFolder, KF_FLAG_DEFAULT,
                                    nullptr, IID_PPV_ARGS(&appsFolder));
  if (FAILED(hr)) {
    return false;
  }

  do {
    // It's possible to have identically named files in `shell:appsfolder` as
    // it's disambiguated by AUMID instead of file name, so we have to iterate
    // over all items instead of querying the specific shortcut.
    ComPtr<IEnumShellItems> shortcutIter;
    hr = appsFolder->BindToHandler(nullptr, BHID_EnumItems,
                                   IID_PPV_ARGS(&shortcutIter));
    if (FAILED(hr)) {
      return false;
    }

    ComPtr<IShellItem> shortcut;
    while (shortcutIter->Next(1, &shortcut, nullptr) == S_OK) {
      ComPtr<IShellItem2> shortcut2;
      hr = shortcut.As(&shortcut2);
      if (FAILED(hr)) {
        return false;
      }

      mozilla::UniquePtr<WCHAR, mozilla::CoTaskMemFreeDeleter> shortcutAumid;
      hr = shortcut2->GetString(PKEY_AppUserModel_ID,
                                getter_Transfers(shortcutAumid));
      if (FAILED(hr)) {
        // `shell:appsfolder` is populated by unique shortcut AUMID; if this is
        // absent something has gone wrong and we should exit.
        return false;
      }

      if (aAppUserModelId == nsDependentString(shortcutAumid.get())) {
        return true;
      }
    }

    // Sleep for a quarter of a second to avoid pinning the CPU while waiting.
    ::Sleep(250);
  } while ((TimeStamp::Now() - start) < aTimeout);

  return false;
}

static Result<nsString, nsresult> EnsurePinnableShortcutExists(
    bool aPrivateBrowsing, const nsAString& aAppUserModelId,
    const nsAString& aShortcutName, const nsAString& aShortcutSubstring,
    nsIFile* aGreDir, const ShortcutLocations& aLocation) {
  MOZ_DIAGNOSTIC_ASSERT(
      !NS_IsMainThread(),
      "EnsurePinnableShortcutExists should be called off main thread only");

  if (xpc::IsInAutomation()) {
    // We don't create or use pinnable shortcuts in tests.
    return nsString(u"dummy_test_shortcut_path"_ns);
  }

  nsString shortcutPath;
  nsresult rv = FindPinnableShortcut(aAppUserModelId, aShortcutSubstring,
                                     aPrivateBrowsing, shortcutPath);
  if (NS_FAILED(rv)) {
    shortcutPath.Truncate();
  }
  if (shortcutPath.IsEmpty()) {
    nsAutoString linkName(aShortcutName);

    nsCOMPtr<nsIFile> exeFile(aGreDir);
    if (aPrivateBrowsing) {
      nsAutoString pbExeStr(PRIVATE_BROWSING_BINARY);
      MOZ_TRY(exeFile->Append(pbExeStr));
    } else {
      wchar_t exePath[MAXPATHLEN] = {};
      MOZ_TRY(BinaryPath::GetLong(exePath));
      nsAutoString exeStr(exePath);
      MOZ_TRY(NS_NewLocalFile(exeStr, getter_AddRefs(exeFile)));
    }

    nsTArray<nsString> arguments;
    MOZ_TRY(CreateShortcutImpl(exeFile, arguments, aShortcutName, exeFile,
                               // Icon indexes are defined as Resource IDs, but
                               // CreateShortcutImpl needs an index.
                               IDI_APPICON - 1, aAppUserModelId, aLocation,
                               linkName));

    shortcutPath.Assign(aLocation.shortcutFile->NativePath());
  }

  MOZ_TRY(EnsureShellAppsFolderShortcut(aAppUserModelId));

  return shortcutPath;
}

static nsresult PinCurrentAppToTaskbarImpl(bool aPrivateBrowsing,
                                           JSContext* aCx,
                                           dom::Promise** aPromise,
                                           const bool aFireAndForget = false,
                                           const bool aPromptOnly = false) {
  if (!NS_IsMainThread()) {
    return NS_ERROR_NOT_SAME_THREAD;
  }

  // First available on 1809
  if (!IsWin10Sep2018UpdateOrLater()) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  ErrorResult rv;
  RefPtr<dom::Promise> promise =
      dom::Promise::Create(xpc::CurrentNativeGlobal(aCx), rv);

  if (MOZ_UNLIKELY(rv.Failed())) {
    return rv.StealNSResult();
  }

  nsAutoString aumid;
  if (NS_WARN_IF(!mozilla::widget::WinTaskbar::GetAppUserModelID(
          aumid, aPrivateBrowsing))) {
    return NS_ERROR_FAILURE;
  }

  // NOTE: In the installer, non-private shortcuts are named
  // "${BrandShortName}.lnk". This is set from MOZ_APP_DISPLAYNAME in
  // defines.nsi.in. (Except in dev edition where it's explicitly set to
  // "Firefox Developer Edition" in branding.nsi, which matches
  // MOZ_APP_DISPLAYNAME in aurora/configure.sh.)
  //
  // If this changes, we could expand this to check shortcuts_log.ini,
  // which records the name of the shortcuts as created by the installer.
  //
  // Private shortcuts are not created by the installer (they're created
  // upon user request, ultimately by CreateShortcutImpl, and recorded in
  // a separate shortcuts log. As with non-private shortcuts they have a known
  // name - so there's no need to look through logs to find them.
  nsAutoString shortcutName;
  if (aPrivateBrowsing) {
    nsTArray<nsCString> resIds = {
        "branding/brand.ftl"_ns,
        "browser/browser.ftl"_ns,
    };
    RefPtr<Localization> l10n = Localization::Create(resIds, true);
    nsAutoCString pbStr;
    IgnoredErrorResult rv;
    l10n->FormatValueSync("private-browsing-shortcut-text-2"_ns, {}, pbStr, rv);
    shortcutName.Append(NS_ConvertUTF8toUTF16(pbStr));
    shortcutName.AppendLiteral(".lnk");
  } else {
    shortcutName.AppendLiteral(MOZ_APP_DISPLAYNAME ".lnk");
  }

  nsCOMPtr<nsIFile> greDir;
  nsresult nsrv = NS_GetSpecialDirectory(NS_GRE_DIR, getter_AddRefs(greDir));
  NS_ENSURE_SUCCESS(nsrv, nsrv);

  ShortcutLocations location =
      MOZ_TRY(GetShortcutPaths(nsString(L"Programs"), shortcutName));

  auto promiseHolder = MakeRefPtr<nsMainThreadPtrHolder<dom::Promise>>(
      "PinCurrentAppToTaskbarImpl promise", promise);

  NS_DispatchBackgroundTask(
      NS_NewRunnableFunction(
          "PinCurrentAppToTaskbarImpl",
          [aPrivateBrowsing, aFireAndForget, aPromptOnly, shortcutName,
           aumid = nsString{aumid}, greDir, location = std::move(location),
           promiseHolder = std::move(promiseHolder)] {
            nsAutoString shortcutSubstring;
            shortcutSubstring.AssignLiteral(MOZ_APP_DISPLAYNAME);
            Result<nsString, nsresult> rv = EnsurePinnableShortcutExists(
                aPrivateBrowsing, aumid, shortcutName, shortcutSubstring,
                greDir.get(), location);

            NS_DispatchToMainThread(NS_NewRunnableFunction(
                "PinCurrentAppToTaskbarImpl callback",
                [aFireAndForget, aPromptOnly, aumid, rv = std::move(rv),
                 promiseHolder = std::move(promiseHolder)] {
                  dom::Promise* promise = promiseHolder.get()->get();

                  if (rv.isOk()) {
                    shell_windows_taskbar_pin_app_to_taskbar(
                        &aumid, &rv.inspect(), aFireAndForget, aPromptOnly,
                        promise);
                  } else {
                    promise->MaybeReject(rv.inspectErr());
                  }
                }));
          }),
      NS_DISPATCH_EVENT_MAY_BLOCK);

  promise.forget(aPromise);
  return NS_OK;
}

NS_IMETHODIMP
nsWindowsShellService::PinCurrentAppToTaskbar(bool aPrivateBrowsing,
                                              bool aFireAndForget,
                                              bool aPromptOnly, JSContext* aCx,
                                              dom::Promise** aPromise) {
  return PinCurrentAppToTaskbarImpl(aPrivateBrowsing, aCx, aPromise,
                                    aFireAndForget, aPromptOnly);
}

NS_IMETHODIMP
nsWindowsShellService::CanPinToTaskbar() {
  // First available on 1809
  if (!IsWin10Sep2018UpdateOrLater()) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  return shell_windows_taskbar_can_pin_to_taskbar();
}

NS_IMETHODIMP
nsWindowsShellService::IsCurrentAppPinnedToTaskbar(
    const nsAString& aumid, JSContext* aCx, /* out */ dom::Promise** aPromise) {
  if (!NS_IsMainThread()) {
    return NS_ERROR_NOT_SAME_THREAD;
  }

  ErrorResult rv;
  RefPtr<dom::Promise> promise =
      dom::Promise::Create(xpc::CurrentNativeGlobal(aCx), rv);
  if (MOZ_UNLIKELY(rv.Failed())) {
    return rv.StealNSResult();
  }

  nsresult pinRv = shell_windows_taskbar_is_current_app_pinned(&aumid, promise);
  if (NS_FAILED(pinRv)) {
    return pinRv;
  }

  promise.forget(aPromise);
  return NS_OK;
}

#ifndef __MINGW32__
#  define RESOLVE_AND_RETURN(HOLDER, RESOLVE, RETURN)                \
    NS_DispatchToMainThread(NS_NewRunnableFunction(                  \
        __func__, [resolveVal = (RESOLVE), promiseHolder = HOLDER] { \
          promiseHolder.get()->get()->MaybeResolve(resolveVal);      \
        }));                                                         \
    return RETURN

#  define REJECT_AND_RETURN(HOLDER, REJECT, RETURN)                 \
    NS_DispatchToMainThread(                                        \
        NS_NewRunnableFunction(__func__, [promiseHolder = HOLDER] { \
          promiseHolder.get()->get()->MaybeReject(REJECT);          \
        }));                                                        \
    return RETURN

static void EnableLaunchOnLoginMSIXImpl(
    const nsString& capturedTaskId,
    const RefPtr<nsMainThreadPtrHolder<dom::Promise>> promiseHolder) {
  ComPtr<IStartupTaskStatics> startupTaskStatics;
  HRESULT hr = GetActivationFactory(
      HStringReference(RuntimeClass_Windows_ApplicationModel_StartupTask).Get(),
      &startupTaskStatics);
  if (FAILED(hr)) {
    REJECT_AND_RETURN(promiseHolder, NS_ERROR_FAILURE, /* void */);
  }
  ComPtr<IAsyncOperation<StartupTask*>> getTaskOperation = nullptr;
  hr = startupTaskStatics->GetAsync(
      HStringReference(capturedTaskId.get()).Get(), &getTaskOperation);
  if (FAILED(hr)) {
    REJECT_AND_RETURN(promiseHolder, NS_ERROR_FAILURE, /* void */);
  }
  auto getTaskCallback =
      Callback<IAsyncOperationCompletedHandler<StartupTask*>>(
          [promiseHolder](IAsyncOperation<StartupTask*>* operation,
                          AsyncStatus status) -> HRESULT {
            if (status != AsyncStatus::Completed) {
              REJECT_AND_RETURN(promiseHolder, NS_ERROR_FAILURE, E_FAIL);
            }
            ComPtr<IStartupTask> startupTask;
            HRESULT hr = operation->GetResults(&startupTask);
            if (FAILED(hr)) {
              REJECT_AND_RETURN(promiseHolder, NS_ERROR_FAILURE, E_FAIL);
            }
            ComPtr<IAsyncOperation<StartupTaskState>> enableOperation;
            hr = startupTask->RequestEnableAsync(&enableOperation);
            if (FAILED(hr)) {
              REJECT_AND_RETURN(promiseHolder, NS_ERROR_FAILURE, E_FAIL);
            }
            // Set another callback for enabling the startup task
            auto enableHandler =
                Callback<IAsyncOperationCompletedHandler<StartupTaskState>>(
                    [promiseHolder](
                        IAsyncOperation<StartupTaskState>* operation,
                        AsyncStatus status) -> HRESULT {
                      StartupTaskState resultState;
                      HRESULT hr = operation->GetResults(&resultState);
                      if (SUCCEEDED(hr) && status == AsyncStatus::Completed) {
                        RESOLVE_AND_RETURN(promiseHolder, true, S_OK);
                      }
                      RESOLVE_AND_RETURN(promiseHolder, false, S_OK);
                    });
            hr = enableOperation->put_Completed(enableHandler.Get());
            if (FAILED(hr)) {
              REJECT_AND_RETURN(promiseHolder, NS_ERROR_FAILURE, hr);
            }
            return hr;
          });
  hr = getTaskOperation->put_Completed(getTaskCallback.Get());
  if (FAILED(hr)) {
    REJECT_AND_RETURN(promiseHolder, NS_ERROR_FAILURE, /* void */);
  }
}

static void DisableLaunchOnLoginMSIXImpl(
    const nsString& capturedTaskId,
    const RefPtr<nsMainThreadPtrHolder<dom::Promise>> promiseHolder) {
  ComPtr<IStartupTaskStatics> startupTaskStatics;
  HRESULT hr = GetActivationFactory(
      HStringReference(RuntimeClass_Windows_ApplicationModel_StartupTask).Get(),
      &startupTaskStatics);
  if (FAILED(hr)) {
    REJECT_AND_RETURN(promiseHolder, NS_ERROR_FAILURE, /* void */);
  }
  ComPtr<IAsyncOperation<StartupTask*>> getTaskOperation = nullptr;
  hr = startupTaskStatics->GetAsync(
      HStringReference(capturedTaskId.get()).Get(), &getTaskOperation);
  if (FAILED(hr)) {
    REJECT_AND_RETURN(promiseHolder, NS_ERROR_FAILURE, /* void */);
  }
  auto getTaskCallback =
      Callback<IAsyncOperationCompletedHandler<StartupTask*>>(
          [promiseHolder](IAsyncOperation<StartupTask*>* operation,
                          AsyncStatus status) -> HRESULT {
            if (status != AsyncStatus::Completed) {
              REJECT_AND_RETURN(promiseHolder, NS_ERROR_FAILURE, E_FAIL);
            }
            ComPtr<IStartupTask> startupTask;
            HRESULT hr = operation->GetResults(&startupTask);
            if (FAILED(hr)) {
              REJECT_AND_RETURN(promiseHolder, NS_ERROR_FAILURE, E_FAIL);
            }
            hr = startupTask->Disable();
            if (FAILED(hr)) {
              REJECT_AND_RETURN(promiseHolder, NS_ERROR_FAILURE, E_FAIL);
            }
            RESOLVE_AND_RETURN(promiseHolder, true, S_OK);
          });
  hr = getTaskOperation->put_Completed(getTaskCallback.Get());
  if (FAILED(hr)) {
    REJECT_AND_RETURN(promiseHolder, NS_ERROR_FAILURE, /* void */);
  }
}

static void GetLaunchOnLoginEnabledMSIXImpl(
    const nsString& capturedTaskId,
    const RefPtr<nsMainThreadPtrHolder<dom::Promise>> promiseHolder) {
  ComPtr<IStartupTaskStatics> startupTaskStatics;
  HRESULT hr = GetActivationFactory(
      HStringReference(RuntimeClass_Windows_ApplicationModel_StartupTask).Get(),
      &startupTaskStatics);
  if (FAILED(hr)) {
    REJECT_AND_RETURN(promiseHolder, NS_ERROR_FAILURE, /* void */);
  }
  ComPtr<IAsyncOperation<StartupTask*>> getTaskOperation = nullptr;
  hr = startupTaskStatics->GetAsync(
      HStringReference(capturedTaskId.get()).Get(), &getTaskOperation);
  if (FAILED(hr)) {
    REJECT_AND_RETURN(promiseHolder, NS_ERROR_FAILURE, /* void */);
  }
  auto getTaskCallback =
      Callback<IAsyncOperationCompletedHandler<StartupTask*>>(
          [promiseHolder](IAsyncOperation<StartupTask*>* operation,
                          AsyncStatus status) -> HRESULT {
            if (status != AsyncStatus::Completed) {
              REJECT_AND_RETURN(promiseHolder, NS_ERROR_FAILURE, E_FAIL);
            }
            ComPtr<IStartupTask> startupTask;
            HRESULT hr = operation->GetResults(&startupTask);
            if (FAILED(hr)) {
              REJECT_AND_RETURN(promiseHolder, NS_ERROR_FAILURE, E_FAIL);
            }
            StartupTaskState state;
            hr = startupTask->get_State(&state);
            if (FAILED(hr)) {
              REJECT_AND_RETURN(promiseHolder, NS_ERROR_FAILURE, E_FAIL);
            }
            switch (state) {
              case StartupTaskState_EnabledByPolicy:
                RESOLVE_AND_RETURN(
                    promiseHolder,
                    nsIWindowsShellService::LaunchOnLoginEnabledEnumerator::
                        LAUNCH_ON_LOGIN_ENABLED_BY_POLICY,
                    S_OK);
                break;
              case StartupTaskState_Enabled:
                RESOLVE_AND_RETURN(
                    promiseHolder,
                    nsIWindowsShellService::LaunchOnLoginEnabledEnumerator::
                        LAUNCH_ON_LOGIN_ENABLED,
                    S_OK);
                break;
              case StartupTaskState_DisabledByUser:
              case StartupTaskState_DisabledByPolicy:
                RESOLVE_AND_RETURN(
                    promiseHolder,
                    nsIWindowsShellService::LaunchOnLoginEnabledEnumerator::
                        LAUNCH_ON_LOGIN_DISABLED_BY_SETTINGS,
                    S_OK);
                break;
              default:
                RESOLVE_AND_RETURN(
                    promiseHolder,
                    nsIWindowsShellService::LaunchOnLoginEnabledEnumerator::
                        LAUNCH_ON_LOGIN_DISABLED,
                    S_OK);
            }
          });
  hr = getTaskOperation->put_Completed(getTaskCallback.Get());
  if (FAILED(hr)) {
    REJECT_AND_RETURN(promiseHolder, NS_ERROR_FAILURE, /* void */);
  }
}

NS_IMETHODIMP
nsWindowsShellService::EnableLaunchOnLoginMSIX(
    const nsAString& aTaskId, JSContext* aCx,
    /* out */ dom::Promise** aPromise) {
  if (!widget::WinUtils::HasPackageIdentity()) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  if (!NS_IsMainThread()) {
    return NS_ERROR_NOT_SAME_THREAD;
  }
  ErrorResult rv;
  RefPtr<dom::Promise> promise =
      dom::Promise::Create(xpc::CurrentNativeGlobal(aCx), rv);
  if (MOZ_UNLIKELY(rv.Failed())) {
    return rv.StealNSResult();
  }

  // A holder to pass the promise through the background task and back to
  // the main thread when finished.
  auto promiseHolder = MakeRefPtr<nsMainThreadPtrHolder<dom::Promise>>(
      "EnableLaunchOnLoginMSIX promise", promise);

  NS_DispatchBackgroundTask(NS_NewRunnableFunction(
      "EnableLaunchOnLoginMSIX", [taskId = nsString(aTaskId), promiseHolder] {
        EnableLaunchOnLoginMSIXImpl(taskId, promiseHolder);
      }));

  promise.forget(aPromise);
  return NS_OK;
}

NS_IMETHODIMP
nsWindowsShellService::DisableLaunchOnLoginMSIX(
    const nsAString& aTaskId, JSContext* aCx,
    /* out */ dom::Promise** aPromise) {
  if (!widget::WinUtils::HasPackageIdentity()) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  if (!NS_IsMainThread()) {
    return NS_ERROR_NOT_SAME_THREAD;
  }
  ErrorResult rv;
  RefPtr<dom::Promise> promise =
      dom::Promise::Create(xpc::CurrentNativeGlobal(aCx), rv);
  if (MOZ_UNLIKELY(rv.Failed())) {
    return rv.StealNSResult();
  }

  // A holder to pass the promise through the background task and back to
  // the main thread when finished.
  auto promiseHolder = MakeRefPtr<nsMainThreadPtrHolder<dom::Promise>>(
      "DisableLaunchOnLoginMSIX promise", promise);

  NS_DispatchBackgroundTask(NS_NewRunnableFunction(
      "DisableLaunchOnLoginMSIX", [taskId = nsString(aTaskId), promiseHolder] {
        DisableLaunchOnLoginMSIXImpl(taskId, promiseHolder);
      }));

  promise.forget(aPromise);
  return NS_OK;
}

NS_IMETHODIMP
nsWindowsShellService::GetLaunchOnLoginEnabledMSIX(
    const nsAString& aTaskId, JSContext* aCx,
    /* out */ dom::Promise** aPromise) {
  if (!widget::WinUtils::HasPackageIdentity()) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  if (!NS_IsMainThread()) {
    return NS_ERROR_NOT_SAME_THREAD;
  }
  ErrorResult rv;
  RefPtr<dom::Promise> promise =
      dom::Promise::Create(xpc::CurrentNativeGlobal(aCx), rv);
  if (MOZ_UNLIKELY(rv.Failed())) {
    return rv.StealNSResult();
  }

  // A holder to pass the promise through the background task and back to
  // the main thread when finished.
  auto promiseHolder = MakeRefPtr<nsMainThreadPtrHolder<dom::Promise>>(
      "GetLaunchOnLoginEnabledMSIX promise", promise);

  NS_DispatchBackgroundTask(NS_NewRunnableFunction(
      "GetLaunchOnLoginEnabledMSIX",
      [taskId = nsString(aTaskId), promiseHolder] {
        GetLaunchOnLoginEnabledMSIXImpl(taskId, promiseHolder);
      }));

  promise.forget(aPromise);
  return NS_OK;
}

static HRESULT GetPackage3(ComPtr<IPackage3>& package3) {
  // Get the current package and cast it to IPackage3 so we can
  // check for AppListEntries
  ComPtr<IPackageStatics> packageStatics;
  HRESULT hr = GetActivationFactory(
      HStringReference(RuntimeClass_Windows_ApplicationModel_Package).Get(),
      &packageStatics);

  if (FAILED(hr)) {
    return hr;
  }
  ComPtr<IPackage> package;
  hr = packageStatics->get_Current(&package);
  if (FAILED(hr)) {
    return hr;
  }
  hr = package.As(&package3);
  return hr;
}

static HRESULT GetStartScreenManager(
    ComPtr<IVectorView<AppListEntry*>>& appListEntries,
    ComPtr<IAppListEntry>& entry,
    ComPtr<IStartScreenManager>& startScreenManager) {
  unsigned int numEntries = 0;
  HRESULT hr = appListEntries->get_Size(&numEntries);
  if (FAILED(hr) || numEntries == 0) {
    return E_FAIL;
  }
  // There's only one AppListEntry in the Firefox package and by
  // convention our main executable should be the first in the
  // list.
  hr = appListEntries->GetAt(0, &entry);

  // Create and init a StartScreenManager and check if we're already
  // pinned.
  ComPtr<IStartScreenManagerStatics> startScreenManagerStatics;
  hr = GetActivationFactory(
      HStringReference(RuntimeClass_Windows_UI_StartScreen_StartScreenManager)
          .Get(),
      &startScreenManagerStatics);
  if (FAILED(hr)) {
    return hr;
  }

  hr = startScreenManagerStatics->GetDefault(&startScreenManager);
  return hr;
}

static void PinCurrentAppToStartMenuImpl(
    const RefPtr<nsMainThreadPtrHolder<dom::Promise>> promiseHolder) {
  ComPtr<IPackage3> package3;
  HRESULT hr = GetPackage3(package3);
  if (FAILED(hr)) {
    REJECT_AND_RETURN(promiseHolder, NS_ERROR_FAILURE, /* void */);
  }

  // Get the AppList entries
  ComPtr<IVectorView<AppListEntry*>> appListEntries;
  ComPtr<IAsyncOperation<IVectorView<AppListEntry*>*>>
      getAppListEntriesOperation;
  hr = package3->GetAppListEntriesAsync(&getAppListEntriesOperation);
  if (FAILED(hr)) {
    REJECT_AND_RETURN(promiseHolder, NS_ERROR_FAILURE, /* void */);
  }
  auto getAppListEntriesCallback = Callback<
      IAsyncOperationCompletedHandler<IVectorView<AppListEntry*>*>>(
      [promiseHolder](IAsyncOperation<IVectorView<AppListEntry*>*>* operation,
                      AsyncStatus status) -> HRESULT {
        if (status != AsyncStatus::Completed) {
          REJECT_AND_RETURN(promiseHolder, NS_ERROR_FAILURE, E_FAIL);
        }
        ComPtr<IVectorView<AppListEntry*>> appListEntries;
        HRESULT hr = operation->GetResults(&appListEntries);
        if (FAILED(hr)) {
          REJECT_AND_RETURN(promiseHolder, NS_ERROR_FAILURE, E_FAIL);
        }
        ComPtr<IStartScreenManager> startScreenManager;
        ComPtr<IAppListEntry> entry;
        hr = GetStartScreenManager(appListEntries, entry, startScreenManager);
        if (FAILED(hr)) {
          REJECT_AND_RETURN(promiseHolder, NS_ERROR_FAILURE, E_FAIL);
        }
        ComPtr<IAsyncOperation<bool>> getPinnedOperation;
        hr = startScreenManager->ContainsAppListEntryAsync(entry.Get(),
                                                           &getPinnedOperation);
        if (FAILED(hr)) {
          REJECT_AND_RETURN(promiseHolder, NS_ERROR_FAILURE, E_FAIL);
        }
        auto getPinnedCallback =
            Callback<IAsyncOperationCompletedHandler<bool>>(
                [promiseHolder, entry, startScreenManager](
                    IAsyncOperation<bool>* operation,
                    AsyncStatus status) -> HRESULT {
                  if (status != AsyncStatus::Completed) {
                    REJECT_AND_RETURN(promiseHolder, NS_ERROR_FAILURE, E_FAIL);
                  }
                  boolean isAlreadyPinned;
                  HRESULT hr = operation->GetResults(&isAlreadyPinned);
                  if (FAILED(hr)) {
                    REJECT_AND_RETURN(promiseHolder, NS_ERROR_FAILURE, E_FAIL);
                  }
                  // If we're already pinned we can return early
                  if (isAlreadyPinned) {
                    RESOLVE_AND_RETURN(promiseHolder, true, S_OK);
                  }
                  ComPtr<IAsyncOperation<bool>> pinOperation;
                  startScreenManager->RequestAddAppListEntryAsync(
                      entry.Get(), &pinOperation);
                  // Set another callback for pinning to the start menu
                  auto pinOperationCallback =
                      Callback<IAsyncOperationCompletedHandler<bool>>(
                          [promiseHolder](IAsyncOperation<bool>* operation,
                                          AsyncStatus status) -> HRESULT {
                            if (status != AsyncStatus::Completed) {
                              REJECT_AND_RETURN(promiseHolder, NS_ERROR_FAILURE,
                                                E_FAIL);
                            };
                            boolean pinSuccess;
                            HRESULT hr = operation->GetResults(&pinSuccess);
                            if (FAILED(hr)) {
                              REJECT_AND_RETURN(promiseHolder, NS_ERROR_FAILURE,
                                                E_FAIL);
                            }
                            RESOLVE_AND_RETURN(promiseHolder,
                                               pinSuccess ? true : false, S_OK);
                          });
                  hr = pinOperation->put_Completed(pinOperationCallback.Get());
                  if (FAILED(hr)) {
                    REJECT_AND_RETURN(promiseHolder, NS_ERROR_FAILURE, hr);
                  }
                  return hr;
                });
        hr = getPinnedOperation->put_Completed(getPinnedCallback.Get());
        if (FAILED(hr)) {
          REJECT_AND_RETURN(promiseHolder, NS_ERROR_FAILURE, hr);
        }
        return hr;
      });
  hr = getAppListEntriesOperation->put_Completed(
      getAppListEntriesCallback.Get());
  if (FAILED(hr)) {
    REJECT_AND_RETURN(promiseHolder, NS_ERROR_FAILURE, /* void */);
  }
}

NS_IMETHODIMP
nsWindowsShellService::PinCurrentAppToStartMenu(JSContext* aCx,
                                                dom::Promise** aPromise) {
  if (!NS_IsMainThread()) {
    return NS_ERROR_NOT_SAME_THREAD;
  }
  // Unfortunately pinning to the Start Menu requires IAppListEntry
  // which is only implemented for packaged applications.
  if (!widget::WinUtils::HasPackageIdentity()) {
    return NS_ERROR_NOT_AVAILABLE;
  }
  ErrorResult rv;
  RefPtr<dom::Promise> promise =
      dom::Promise::Create(xpc::CurrentNativeGlobal(aCx), rv);
  if (MOZ_UNLIKELY(rv.Failed())) {
    return rv.StealNSResult();
  }

  // A holder to pass the promise through the background task and back to
  // the main thread when finished.
  auto promiseHolder = MakeRefPtr<nsMainThreadPtrHolder<dom::Promise>>(
      "PinCurrentAppToStartMenu promise", promise);
  NS_DispatchBackgroundTask(NS_NewRunnableFunction(
      "PinCurrentAppToStartMenu",
      [promiseHolder] { PinCurrentAppToStartMenuImpl(promiseHolder); }));
  promise.forget(aPromise);
  return NS_OK;
}

static void IsCurrentAppPinnedToStartMenuImpl(
    const RefPtr<nsMainThreadPtrHolder<dom::Promise>> promiseHolder) {
  ComPtr<IPackage3> package3;
  HRESULT hr = GetPackage3(package3);
  if (FAILED(hr)) {
    REJECT_AND_RETURN(promiseHolder, NS_ERROR_FAILURE, /* void */);
  }

  // Get the AppList entries
  ComPtr<IVectorView<AppListEntry*>> appListEntries;
  ComPtr<IAsyncOperation<IVectorView<AppListEntry*>*>>
      getAppListEntriesOperation;
  hr = package3->GetAppListEntriesAsync(&getAppListEntriesOperation);
  if (FAILED(hr)) {
    REJECT_AND_RETURN(promiseHolder, NS_ERROR_FAILURE, /* void */);
  }
  auto getAppListEntriesCallback =
      Callback<IAsyncOperationCompletedHandler<IVectorView<AppListEntry*>*>>(
          [promiseHolder](
              IAsyncOperation<IVectorView<AppListEntry*>*>* operation,
              AsyncStatus status) -> HRESULT {
            if (status != AsyncStatus::Completed) {
              REJECT_AND_RETURN(promiseHolder, NS_ERROR_FAILURE, E_FAIL);
            }
            ComPtr<IVectorView<AppListEntry*>> appListEntries;
            HRESULT hr = operation->GetResults(&appListEntries);
            if (FAILED(hr)) {
              REJECT_AND_RETURN(promiseHolder, NS_ERROR_FAILURE, E_FAIL);
            }
            ComPtr<IStartScreenManager> startScreenManager;
            ComPtr<IAppListEntry> entry;
            hr = GetStartScreenManager(appListEntries, entry,
                                       startScreenManager);
            if (FAILED(hr)) {
              REJECT_AND_RETURN(promiseHolder, NS_ERROR_FAILURE, E_FAIL);
            }
            ComPtr<IAsyncOperation<bool>> getPinnedOperation;
            hr = startScreenManager->ContainsAppListEntryAsync(
                entry.Get(), &getPinnedOperation);
            if (FAILED(hr)) {
              REJECT_AND_RETURN(promiseHolder, NS_ERROR_FAILURE, E_FAIL);
            }
            auto getPinnedCallback =
                Callback<IAsyncOperationCompletedHandler<bool>>(
                    [promiseHolder, entry, startScreenManager](
                        IAsyncOperation<bool>* operation,
                        AsyncStatus status) -> HRESULT {
                      if (status != AsyncStatus::Completed) {
                        REJECT_AND_RETURN(promiseHolder, NS_ERROR_FAILURE,
                                          E_FAIL);
                      }
                      boolean isAlreadyPinned;
                      HRESULT hr = operation->GetResults(&isAlreadyPinned);
                      if (FAILED(hr)) {
                        REJECT_AND_RETURN(promiseHolder, NS_ERROR_FAILURE,
                                          E_FAIL);
                      }
                      RESOLVE_AND_RETURN(promiseHolder,
                                         isAlreadyPinned ? true : false, S_OK);
                    });
            hr = getPinnedOperation->put_Completed(getPinnedCallback.Get());
            if (FAILED(hr)) {
              REJECT_AND_RETURN(promiseHolder, NS_ERROR_FAILURE, hr);
            }
            return hr;
          });
  hr = getAppListEntriesOperation->put_Completed(
      getAppListEntriesCallback.Get());
  if (FAILED(hr)) {
    REJECT_AND_RETURN(promiseHolder, NS_ERROR_FAILURE, /* void */);
  }
}

NS_IMETHODIMP
nsWindowsShellService::IsCurrentAppPinnedToStartMenu(JSContext* aCx,
                                                     dom::Promise** aPromise) {
  if (!NS_IsMainThread()) {
    return NS_ERROR_NOT_SAME_THREAD;
  }
  // Unfortunately pinning to the Start Menu requires IAppListEntry
  // which is only implemented for packaged applications.
  if (!widget::WinUtils::HasPackageIdentity()) {
    return NS_ERROR_NOT_AVAILABLE;
  }
  ErrorResult rv;
  RefPtr<dom::Promise> promise =
      dom::Promise::Create(xpc::CurrentNativeGlobal(aCx), rv);
  if (MOZ_UNLIKELY(rv.Failed())) {
    return rv.StealNSResult();
  }

  // A holder to pass the promise through the background task and back to
  // the main thread when finished.
  auto promiseHolder = MakeRefPtr<nsMainThreadPtrHolder<dom::Promise>>(
      "IsCurrentAppPinnedToStartMenu promise", promise);
  NS_DispatchBackgroundTask(NS_NewRunnableFunction(
      "IsCurrentAppPinnedToStartMenu",
      [promiseHolder] { IsCurrentAppPinnedToStartMenuImpl(promiseHolder); }));
  promise.forget(aPromise);
  return NS_OK;
}

#else
NS_IMETHODIMP
nsWindowsShellService::EnableLaunchOnLoginMSIX(
    const nsAString& aTaskId, JSContext* aCx,
    /* out */ dom::Promise** aPromise) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsWindowsShellService::DisableLaunchOnLoginMSIX(
    const nsAString& aTaskId, JSContext* aCx,
    /* out */ dom::Promise** aPromise) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsWindowsShellService::GetLaunchOnLoginEnabledMSIX(
    const nsAString& aTaskId, JSContext* aCx,
    /* out */ dom::Promise** aPromise) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsWindowsShellService::PinCurrentAppToStartMenu(JSContext* aCx,
                                                dom::Promise** aPromise) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsWindowsShellService::IsCurrentAppPinnedToStartMenu(JSContext* aCx,
                                                     dom::Promise** aPromise) {
  return NS_ERROR_NOT_IMPLEMENTED;
}
#endif

NS_IMETHODIMP
nsWindowsShellService::ClassifyShortcut(const nsAString& aPath,
                                        nsAString& aResult) {
  aResult.Truncate();

  nsAutoString shortcutPath(PromiseFlatString(aPath));

  // NOTE: On Windows 7, Start Menu pin shortcuts are stored under
  // "<FOLDERID_User Pinned>\StartMenu", but on Windows 10 they are just normal
  // Start Menu shortcuts. These both map to "StartMenu" for consistency,
  // rather than having a separate "StartMenuPins" which would only apply on
  // Win7.
  struct {
    KNOWNFOLDERID folderId;
    const char16_t* postfix;
    const char16_t* classification;
  } folders[] = {{FOLDERID_CommonStartMenu, u"\\", u"StartMenu"},
                 {FOLDERID_StartMenu, u"\\", u"StartMenu"},
                 {FOLDERID_PublicDesktop, u"\\", u"Desktop"},
                 {FOLDERID_Desktop, u"\\", u"Desktop"},
                 {FOLDERID_UserPinned, u"\\TaskBar\\", u"Taskbar"},
                 {FOLDERID_UserPinned, u"\\StartMenu\\", u"StartMenu"}};

  for (size_t i = 0; i < std::size(folders); ++i) {
    nsAutoString knownPath;

    // These flags are chosen to avoid I/O, see bug 1363398.
    DWORD flags =
        KF_FLAG_SIMPLE_IDLIST | KF_FLAG_DONT_VERIFY | KF_FLAG_NO_ALIAS;
    PWSTR rawPath = nullptr;

    if (FAILED(SHGetKnownFolderPath(folders[i].folderId, flags, nullptr,
                                    &rawPath))) {
      continue;
    }

    knownPath = nsDependentString(rawPath);
    CoTaskMemFree(rawPath);

    knownPath.Append(folders[i].postfix);
    // Check if the shortcut path starts with the shell folder path.
    if (wcsnicmp(shortcutPath.get(), knownPath.get(), knownPath.Length()) ==
        0) {
      aResult.Assign(folders[i].classification);
      nsTArray<nsCString> resIds = {
          "branding/brand.ftl"_ns,
          "browser/browser.ftl"_ns,
      };
      RefPtr<Localization> l10n = Localization::Create(resIds, true);
      nsAutoCString pbStr;
      IgnoredErrorResult rv;
      l10n->FormatValueSync("private-browsing-shortcut-text-2"_ns, {}, pbStr,
                            rv);
      NS_ConvertUTF8toUTF16 widePbStr(pbStr);
      if (wcsstr(shortcutPath.get(), widePbStr.get())) {
        aResult.AppendLiteral("Private");
      }
      return NS_OK;
    }
  }

  // Nothing found, aResult is already "".
  return NS_OK;
}

static nsresult SetShortcutsIconImpl(
    const CopyableTArray<nsString>& aShortcutPaths, const nsAString& aIconPath,
    uint16_t aIconResourceId) {
  // Reference the icon by resource ID using the Win32 negative-index
  // convention (see ExtractIconExW), so the reference stays valid regardless of
  // the order icons are embedded in aIconPath. A resource ID of 0 selects the
  // binary's default icon (positional index 0).
  int32_t iconLocationIndex =
      aIconResourceId == 0 ? 0 : -static_cast<int32_t>(aIconResourceId);

  bool updatedAtLeastOne = false;
  for (const auto& shortcutPath : aShortcutPaths) {
    RefPtr<IShellLinkW> link;
    HRESULT hr =
        CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                         IID_IShellLinkW, getter_AddRefs(link));
    if (FAILED(hr)) {
      MOZ_LOG(sLog, LogLevel::Error, ("Could not create CLSID_ShellLink"));
      continue;
    }

    RefPtr<IPersistFile> persist;
    hr = link->QueryInterface(IID_IPersistFile, getter_AddRefs(persist));
    if (FAILED(hr)) {
      MOZ_LOG(sLog, LogLevel::Error,
              ("Could not get IPersistFile off CLSID_ShellLink"));
      continue;
    }

    const nsString& flatShortcutPath = PromiseFlatString(shortcutPath);
    const NS_ConvertUTF16toUTF8 loggablePath(flatShortcutPath);
    hr = persist->Load(flatShortcutPath.get(), STGM_READWRITE);
    if (FAILED(hr)) {
      if (hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)) {
        MOZ_LOG(sLog, LogLevel::Warning,
                ("Could not find shortcut at %s to update. Skipping.",
                 loggablePath.get()));
      } else {
        MOZ_LOG(sLog, LogLevel::Warning,
                ("Could not load shortcut at %s to update (%ld). Skipping.",
                 loggablePath.get(), hr));
      }
      continue;
    }

    hr = link->SetIconLocation(PromiseFlatString(aIconPath).get(),
                               iconLocationIndex);
    if (FAILED(hr)) {
      MOZ_LOG(sLog, LogLevel::Warning,
              ("Could not update icon for shortcut at %s (%ld). Skipping.",
               loggablePath.get(), hr));
      continue;
    }

    hr = persist->Save(flatShortcutPath.get(), TRUE);
    if (FAILED(hr)) {
      MOZ_LOG(sLog, LogLevel::Warning,
              ("Could not save icon update for shortcut at %s (%ld). Skipping.",
               loggablePath.get(), hr));
      continue;
    }

    ::SHChangeNotify(SHCNE_UPDATEITEM, SHCNF_PATHW, flatShortcutPath.get(),
                     nullptr);
    updatedAtLeastOne = true;
  }
  ::SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);

  if (updatedAtLeastOne) {
    return NS_OK;
  } else {
    return NS_ERROR_NOT_AVAILABLE;
  }
}

NS_IMETHODIMP
nsWindowsShellService::SetShortcutsIcon(
    const nsTArray<nsString>& aShortcutPaths, const nsAString& aIconPath,
    uint16_t aIconResourceId, JSContext* aCx, dom::Promise** aPromise) {
  if (!NS_IsMainThread()) {
    return NS_ERROR_NOT_SAME_THREAD;
  }

  ErrorResult rv;
  RefPtr<dom::Promise> promise =
      dom::Promise::Create(xpc::CurrentNativeGlobal(aCx), rv);

  if (MOZ_UNLIKELY(rv.Failed())) {
    return rv.StealNSResult();
  }

  auto promiseHolder = MakeRefPtr<nsMainThreadPtrHolder<dom::Promise>>(
      "SetShortcutsIcon promise", promise);

  NS_DispatchBackgroundTask(
      NS_NewRunnableFunction(
          "SetShortcutsIcon",
          [aShortcutPaths = CopyableTArray<nsString>(aShortcutPaths),
           aIconPath = nsString{aIconPath}, aIconResourceId,
           promiseHolder = std::move(promiseHolder)] {
            nsresult rv = SetShortcutsIconImpl(aShortcutPaths, aIconPath,
                                               aIconResourceId);
            NS_DispatchToMainThread(NS_NewRunnableFunction(
                "SetShortcutsIcon callback",
                [rv, promiseHolder = std::move(promiseHolder)] {
                  dom::Promise* promise = promiseHolder.get()->get();
                  if (NS_SUCCEEDED(rv)) {
                    promise->MaybeResolveWithUndefined();
                  } else {
                    promise->MaybeReject(rv);
                  }
                }));
          }),
      NS_DISPATCH_EVENT_MAY_BLOCK);

  promise.forget(aPromise);
  return NS_OK;
}

// A single enumerateInstallShortcuts result.
class InstallShortcutInfo final : public nsIInstallShortcutInfo {
 public:
  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSIINSTALLSHORTCUTINFO

  InstallShortcutInfo(const nsAString& aPath, const nsAString& aLocation)
      : mPath(aPath), mLocation(aLocation) {}

 private:
  ~InstallShortcutInfo() = default;

  const nsString mPath;
  const nsString mLocation;
};

NS_IMPL_ISUPPORTS(InstallShortcutInfo, nsIInstallShortcutInfo)

NS_IMETHODIMP InstallShortcutInfo::GetPath(nsAString& aPath) {
  aPath = mPath;
  return NS_OK;
}

NS_IMETHODIMP InstallShortcutInfo::GetLocation(nsAString& aLocation) {
  aLocation = mLocation;
  return NS_OK;
}

static void CollectMatchingShortcutsInDir(const nsAString& aDirPath,
                                          const nsAString& aAUMID,
                                          const wchar_t aExePath[MAXPATHLEN],
                                          const nsAString& aShortcutSubstring,
                                          nsTArray<nsString>& aOut) {
  nsAutoString pattern(aDirPath);
  if (pattern.IsEmpty()) {
    return;
  }
  if (pattern.Last() != L'\\') {
    pattern.Append(u'\\');
  }
  nsAutoString folderWithSlash(pattern);
  pattern.AppendLiteral("*.lnk");

  WIN32_FIND_DATAW findData = {};
  HANDLE hFindFile = FindFirstFileW(pattern.get(), &findData);
  if (hFindFile == INVALID_HANDLE_VALUE) {
    return;
  }

  do {
    if (!aShortcutSubstring.IsEmpty() &&
        StrStrIW(findData.cFileName,
                 PromiseFlatString(aShortcutSubstring).get()) == nullptr) {
      continue;
    }

    nsAutoString path(folderWithSlash);
    path.Append(findData.cFileName);

    RefPtr<IShellLinkW> link;
    HRESULT hr =
        CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                         IID_IShellLinkW, getter_AddRefs(link));
    if (FAILED(hr)) {
      continue;
    }

    RefPtr<IPersistFile> persist;
    hr = link->QueryInterface(IID_IPersistFile, getter_AddRefs(persist));
    if (FAILED(hr)) {
      continue;
    }

    hr = persist->Load(path.get(), STGM_READ);
    if (FAILED(hr)) {
      continue;
    }

    RefPtr<IPropertyStore> propStore;
    hr = link->QueryInterface(IID_IPropertyStore, getter_AddRefs(propStore));
    if (FAILED(hr)) {
      continue;
    }

    PROPVARIANT pv;
    hr = propStore->GetValue(PKEY_AppUserModel_ID, &pv);
    if (FAILED(hr)) {
      continue;
    }

    wchar_t storedAUMID[MAX_PATH];
    hr = PropVariantToString(pv, storedAUMID, MAX_PATH);
    PropVariantClear(&pv);
    if (FAILED(hr)) {
      continue;
    }

    if (!aAUMID.Equals(storedAUMID)) {
      continue;
    }

    static_assert(MAXPATHLEN == MAX_PATH);
    wchar_t storedExePath[MAX_PATH] = {};
    hr = link->GetPath(storedExePath, std::size(storedExePath), nullptr, 0);
    if (FAILED(hr) || hr == S_FALSE) {
      continue;
    }

    if (wcsnicmp(storedExePath, aExePath, MAXPATHLEN) == 0) {
      aOut.AppendElement(path);
    }
  } while (FindNextFileW(hFindFile, &findData));

  FindClose(hFindFile);
}

static nsresult EnumerateInstallShortcutsImpl(
    const nsAString& aAUMID, nsTArray<RefPtr<InstallShortcutInfo>>& aOut) {
  wchar_t exePath[MAXPATHLEN] = {};
  if (NS_FAILED(BinaryPath::GetLong(exePath))) {
    return NS_ERROR_FAILURE;
  }

  nsAutoString shortcutSubstring;
  shortcutSubstring.AssignLiteral(MOZ_APP_DISPLAYNAME);

  auto collectTagged = [&](const nsAString& aDirPath,
                           const char16_t* aLocation) {
    nsTArray<nsString> paths;
    CollectMatchingShortcutsInDir(aDirPath, aAUMID, exePath, shortcutSubstring,
                                  paths);
    for (const nsString& path : paths) {
      aOut.AppendElement(
          new InstallShortcutInfo(path, nsDependentString(aLocation)));
    }
  };

  struct {
    KNOWNFOLDERID folderId;
    const char16_t* location;
  } folders[] = {{FOLDERID_Desktop, u"Desktop"},
                 {FOLDERID_Programs, u"Programs"},
                 {FOLDERID_CommonPrograms, u"CommonPrograms"}};
  for (const auto& folder : folders) {
    UniquePtr<wchar_t, mozilla::CoTaskMemFreeDeleter> folderPath;
    HRESULT hr = SHGetKnownFolderPath(folder.folderId, SHGFP_TYPE_CURRENT,
                                      nullptr, getter_Transfers(folderPath));
    if (FAILED(hr)) {
      continue;
    }
    collectTagged(nsDependentString(folderPath.get()), folder.location);
  }

  UniquePtr<wchar_t, mozilla::CoTaskMemFreeDeleter> appDataPath;
  HRESULT hr = SHGetKnownFolderPath(FOLDERID_RoamingAppData, SHGFP_TYPE_CURRENT,
                                    nullptr, getter_Transfers(appDataPath));
  if (SUCCEEDED(hr)) {
    nsAutoString taskbarPath(appDataPath.get());
    taskbarPath.AppendLiteral(
        "\\Microsoft\\Internet Explorer\\Quick Launch\\User Pinned\\TaskBar");
    if (taskbarPath.Length() < MAX_PATH) {
      collectTagged(taskbarPath, u"Taskbar");
    }
  }

  return NS_OK;
}

NS_IMETHODIMP
nsWindowsShellService::EnumerateInstallShortcuts(
    const nsAString& aAppUserModelId, JSContext* aCx, dom::Promise** aPromise) {
  if (!NS_IsMainThread()) {
    return NS_ERROR_NOT_SAME_THREAD;
  }

  ErrorResult rv;
  RefPtr<dom::Promise> promise =
      dom::Promise::Create(xpc::CurrentNativeGlobal(aCx), rv);

  if (MOZ_UNLIKELY(rv.Failed())) {
    return rv.StealNSResult();
  }

  auto promiseHolder = MakeRefPtr<nsMainThreadPtrHolder<dom::Promise>>(
      "EnumerateInstallShortcuts promise", promise);

  NS_DispatchBackgroundTask(
      NS_NewRunnableFunction(
          "EnumerateInstallShortcuts",
          [aAppUserModelId = nsString{aAppUserModelId},
           promiseHolder = std::move(promiseHolder)] {
            nsTArray<RefPtr<InstallShortcutInfo>> results;
            nsresult rv =
                EnumerateInstallShortcutsImpl(aAppUserModelId, results);

            NS_DispatchToMainThread(NS_NewRunnableFunction(
                "EnumerateInstallShortcuts callback",
                [rv, results = std::move(results),
                 promiseHolder = std::move(promiseHolder)]() mutable {
                  dom::Promise* promise = promiseHolder.get()->get();
                  if (NS_SUCCEEDED(rv)) {
                    promise->MaybeResolve(results);
                  } else {
                    promise->MaybeReject(rv);
                  }
                }));
          }),
      NS_DISPATCH_EVENT_MAY_BLOCK);

  promise.forget(aPromise);
  return NS_OK;
}
