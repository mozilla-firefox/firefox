/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#define UNICODE

#include "nsWindowsShellService.h"
#include "nsWindowsShellServiceInternal.h"

#include "BinaryPath.h"
#include "gfxUtils.h"
#include "imgIContainer.h"
#include "imgIRequest.h"
#include "imgITools.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/Promise.h"
#include "mozilla/ErrorResult.h"
#include "mozilla/FileUtils.h"
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
#include "Windows11TaskbarPinning.h"
#include "WindowsDefaultBrowser.h"
#include "WindowsUserChoice.h"
#include "WinUtils.h"

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

#ifdef DEBUG
#  define NS_ENSURE_HRESULT(hres, ret)                    \
    do {                                                  \
      HRESULT result = hres;                              \
      if (MOZ_UNLIKELY(FAILED(result))) {                 \
        mozilla::SmprintfPointer msg = mozilla::Smprintf( \
            "NS_ENSURE_HRESULT(%s, %s) failed with "      \
            "result 0x%" PRIX32,                          \
            #hres, #ret, static_cast<uint32_t>(result));  \
        NS_WARNING(msg.get());                            \
        return ret;                                       \
      }                                                   \
    } while (false)
#else
#  define NS_ENSURE_HRESULT(hres, ret) \
    if (MOZ_UNLIKELY(FAILED(hres))) return ret
#endif

using namespace mozilla;
using mozilla::intl::Localization;

struct SysFreeStringDeleter {
  void operator()(BSTR aPtr) { ::SysFreeString(aPtr); }
};
using BStrPtr = mozilla::UniquePtr<OLECHAR, SysFreeStringDeleter>;

NS_IMPL_ISUPPORTS(nsWindowsShellService, nsIToolkitShellService,
                  nsIShellService, nsIWindowsShellService)

/* Enable logging by setting MOZ_LOG to "nsWindowsShellService:5" for debugging
 * purposes. */
static LazyLogModule sLog("nsWindowsShellService");

static bool PollAppsFolderForShortcut(const nsAString& aAppUserModelId,
                                      const TimeDuration aTimeout);
static nsresult PinCurrentAppToTaskbarWin10(bool aCheckOnly,
                                            const nsAString& aAppUserModelId,
                                            const nsAString& aShortcutPath);
static nsresult WriteBitmap(nsIFile* aFile, imgIContainer* aImage);

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
  virtual HRESULT STDMETHODCALLTYPE Launch(HWND hWndParent, LPCWSTR lpszPath,
                                           int flags) = 0;
};

NS_IMETHODIMP
nsWindowsShellService::LaunchOpenWithDefaultPickerForFileType(
    const nsAString& aFileType) {
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

  // The flag is a bit of a mystery; on Win11+ 0x84 gives ideal messaging, on
  // Win10 we use 0x2004.
  int flag = mozilla::IsWin11OrLater() ? 0x84 : 0x2004;
  hr = pOWL->Launch(nullptr, aFileType.Data(), flag);

  return SUCCEEDED(hr) ? NS_OK : NS_ERROR_FAILURE;
}

NS_IMETHODIMP
nsWindowsShellService::LaunchModernSettingsDialogDefaultApps() {
  return ::LaunchModernSettingsDialogDefaultApps() ? NS_OK : NS_ERROR_FAILURE;
}

static void FocusSetDefaultBrowserButton() {
  nsCOMPtr<nsISerialEventTarget> serialEventTarget;
  const nsresult nsr{NS_CreateBackgroundTaskQueue(
      "FocusSetDefaultBrowserButtonQueue", getter_AddRefs(serialEventTarget))};
  if (NS_FAILED(nsr)) {
    return;
  }

  auto attempts{std::make_shared<int>(0)};
  auto timer{std::make_shared<nsCOMPtr<nsITimer>>()};
  auto timerCallback{[attempts, timer](nsITimer* aTimer) {
    const int kMaxAttempts{40};
    if (++(*attempts) > kMaxAttempts) {
      aTimer->Cancel();
      return;
    }
    auto [window, button]{FindSetDefaultBrowserButton()};
    if (window && button) {
      FocusElement(window, button);
      aTimer->Cancel();
    }
  }};
  const uint32_t kRetryDelayMs{500};
  NS_NewTimerWithCallback(getter_AddRefs(*timer), timerCallback, kRetryDelayMs,
                          nsITimer::TYPE_REPEATING_SLACK,
                          "FocusSetDefaultBrowserButtonTimer"_ns,
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
      if (Preferences::GetBool("browser.shell.focusSetDefaultBrowserButton",
                               false)) {
        FocusSetDefaultBrowserButton();
      }
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

static nsresult WriteBitmap(nsIFile* aFile, imgIContainer* aImage) {
  nsresult rv;

  RefPtr<gfx::SourceSurface> surface = aImage->GetFrame(
      imgIContainer::FRAME_FIRST,
      imgIContainer::FLAG_SYNC_DECODE | imgIContainer::FLAG_ASYNC_NOTIFY);
  NS_ENSURE_TRUE(surface, NS_ERROR_FAILURE);

  // For either of the following formats we want to set the biBitCount member
  // of the BITMAPINFOHEADER struct to 32, below. For that value the bitmap
  // format defines that the A8/X8 WORDs in the bitmap byte stream be ignored
  // for the BI_RGB value we use for the biCompression member.
  MOZ_ASSERT(surface->GetFormat() == gfx::SurfaceFormat::B8G8R8A8 ||
             surface->GetFormat() == gfx::SurfaceFormat::B8G8R8X8);

  RefPtr<gfx::DataSourceSurface> dataSurface = surface->GetDataSurface();
  NS_ENSURE_TRUE(dataSurface, NS_ERROR_FAILURE);

  int32_t width = dataSurface->GetSize().width;
  int32_t height = dataSurface->GetSize().height;
  int32_t bytesPerPixel = 4 * sizeof(uint8_t);
  uint32_t bytesPerRow = bytesPerPixel * width;

  // initialize these bitmap structs which we will later
  // serialize directly to the head of the bitmap file
  BITMAPINFOHEADER bmi;
  bmi.biSize = sizeof(BITMAPINFOHEADER);
  bmi.biWidth = width;
  bmi.biHeight = height;
  bmi.biPlanes = 1;
  bmi.biBitCount = (WORD)bytesPerPixel * 8;
  bmi.biCompression = BI_RGB;
  bmi.biSizeImage = bytesPerRow * height;
  bmi.biXPelsPerMeter = 0;
  bmi.biYPelsPerMeter = 0;
  bmi.biClrUsed = 0;
  bmi.biClrImportant = 0;

  BITMAPFILEHEADER bf;
  bf.bfType = 0x4D42;  // 'BM'
  bf.bfReserved1 = 0;
  bf.bfReserved2 = 0;
  bf.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
  bf.bfSize = bf.bfOffBits + bmi.biSizeImage;

  // get a file output stream
  nsCOMPtr<nsIOutputStream> stream;
  rv = NS_NewLocalFileOutputStream(getter_AddRefs(stream), aFile);
  NS_ENSURE_SUCCESS(rv, rv);

  // (redundant) guard clause to assert stream is initialized
  if (NS_WARN_IF(!stream)) {
    MOZ_ASSERT(stream, "rv should have failed when stream is not initialized.");
    return NS_ERROR_FAILURE;
  }

  gfx::DataSourceSurface::MappedSurface map;
  if (!dataSurface->Map(gfx::DataSourceSurface::MapType::READ, &map)) {
    // removal of file created handled later
    rv = NS_ERROR_FAILURE;
  }

  // enter only if datasurface mapping succeeded
  if (NS_SUCCEEDED(rv)) {
    // write the bitmap headers and rgb pixel data to the file
    uint32_t written;
    rv = stream->Write((const char*)&bf, sizeof(BITMAPFILEHEADER), &written);
    if (NS_SUCCEEDED(rv)) {
      rv = stream->Write((const char*)&bmi, sizeof(BITMAPINFOHEADER), &written);
      if (NS_SUCCEEDED(rv)) {
        // write out the image data backwards because the desktop won't
        // show bitmaps with negative heights for top-to-bottom
        uint32_t i = map.mStride * height;
        do {
          i -= map.mStride;
          rv = stream->Write(((const char*)map.mData) + i, bytesPerRow,
                             &written);
          if (NS_FAILED(rv)) {
            break;
          }
        } while (i != 0);
      }
    }

    dataSurface->Unmap();
  }

  stream->Close();

  // Obtaining the file output stream results in a newly created file or
  // truncates the file if it already exists. As such, it is necessary to
  // remove the file if the write fails for some reason.
  if (NS_FAILED(rv)) {
    if (NS_WARN_IF(NS_FAILED(aFile->Remove(PR_FALSE)))) {
      MOZ_LOG(sLog, LogLevel::Warning,
              ("Failed to remove empty bitmap file : %s",
               aFile->HumanReadablePath().get()));
    }
  }

  return rv;
}

NS_IMETHODIMP
nsWindowsShellService::SetDesktopBackground(dom::Element* aElement,
                                            int32_t aPosition,
                                            const nsACString& aImageName) {
  if (!aElement || !aElement->IsHTMLElement(nsGkAtoms::img)) {
    // XXX write background loading stuff!
    return NS_ERROR_NOT_AVAILABLE;
  }

  nsresult rv;
  nsCOMPtr<nsIImageLoadingContent> imageContent =
      do_QueryInterface(aElement, &rv);
  if (!imageContent) return rv;

  // get the image container
  nsCOMPtr<imgIRequest> request;
  rv = imageContent->GetRequest(nsIImageLoadingContent::CURRENT_REQUEST,
                                getter_AddRefs(request));
  if (!request) return rv;

  nsCOMPtr<imgIContainer> container;
  rv = request->GetImage(getter_AddRefs(container));
  if (!container) return NS_ERROR_FAILURE;

  // get the file name from localized strings, e.g. "Desktop Background", then
  // append the extension (".bmp").
  nsTArray<nsCString> resIds = {
      "browser/setDesktopBackground.ftl"_ns,
  };
  RefPtr<Localization> l10n = Localization::Create(resIds, true);
  nsAutoCString fileLeafNameUtf8;
  IgnoredErrorResult locRv;
  l10n->FormatValueSync("set-desktop-background-filename"_ns, {},
                        fileLeafNameUtf8, locRv);
  nsAutoString fileLeafName = NS_ConvertUTF8toUTF16(fileLeafNameUtf8);
  fileLeafName.AppendLiteral(".bmp");

  // get the profile root directory
  nsCOMPtr<nsIFile> file;
  rv = NS_GetSpecialDirectory(NS_APP_APPLICATION_REGISTRY_DIR,
                              getter_AddRefs(file));
  NS_ENSURE_SUCCESS(rv, rv);

  // eventually, the path is "%APPDATA%\Mozilla\Firefox\Desktop Background.bmp"
  rv = file->Append(fileLeafName);
  NS_ENSURE_SUCCESS(rv, rv);

  nsAutoString path;
  rv = file->GetPath(path);
  NS_ENSURE_SUCCESS(rv, rv);

  // write the bitmap to a file in the profile directory.
  // We have to write old bitmap format for Windows 7 wallpaper support.
  rv = WriteBitmap(file, container);

  // if the file was written successfully, set it as the system wallpaper
  if (NS_SUCCEEDED(rv)) {
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
  }
  return rv;
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
                                    /* out */ nsAutoString& aShortcutPath) {
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
                                     nsAutoString& aShortcutPath) {
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
            bool rv = false;
            HRESULT hr = CoInitialize(nullptr);

            if (SUCCEEDED(hr)) {
              nsAutoString shortcutSubstring;
              shortcutSubstring.AssignLiteral(MOZ_APP_DISPLAYNAME);
              rv = HasPinnableShortcutImpl(aAppUserModelId, aPrivateBrowsing,
                                           shortcutSubstring);
              CoUninitialize();
            }

            NS_DispatchToMainThread(NS_NewRunnableFunction(
                "HasPinnableShortcut callback",
                [rv, promiseHolder = std::move(promiseHolder)] {
                  dom::Promise* promise = promiseHolder.get()->get();

                  promise->MaybeResolve(rv);
                }));
          }),
      NS_DISPATCH_EVENT_MAY_BLOCK);

  promise.forget(aPromise);
  return NS_OK;
}

static bool IsCurrentAppPinnedToTaskbarSync(const nsAString& aumid) {
  // Use new Windows pinning APIs to determine whether or not we're pinned.
  // If these fail we can safely fall back to the old method for regular
  // installs however MSIX will always return false.

  // Bug 1911343: Add a check for whether we're looking for a regular pin
  // or PB pin based on the AUMID value once private browser pinning
  // is supported on MSIX.
  // Right now only run this check on MSIX to avoid
  // false positives when only private browsing is pinned.
  if (widget::WinUtils::HasPackageIdentity()) {
    auto pinWithWin11TaskbarAPIResults = IsCurrentAppPinnedToTaskbarWin11();
    switch (pinWithWin11TaskbarAPIResults.result) {
      case Win11PinToTaskBarResultStatus::NotPinned:
        return false;
        break;
      case Win11PinToTaskBarResultStatus::AlreadyPinned:
        return true;
        break;
      default:
        // Fall through to the old mechanism.
        // The old mechanism should continue working for non-MSIX
        // builds.
        break;
    }
  }

  // There are two shortcut targets that we created. One always matches the
  // binary we're running as (eg: firefox.exe). The other is the wrapper
  // for launching in Private Browsing mode. We need to inspect shortcuts
  // that point at either of these to accurately judge whether or not
  // the app is pinned with the given AUMID.
  wchar_t exePath[MAXPATHLEN] = {};
  wchar_t pbExePath[MAXPATHLEN] = {};

  if (NS_WARN_IF(NS_FAILED(BinaryPath::GetLong(exePath)))) {
    return false;
  }

  wcscpy_s(pbExePath, MAXPATHLEN, exePath);
  if (!PathRemoveFileSpecW(pbExePath)) {
    return false;
  }
  if (!PathAppendW(pbExePath, L"private_browsing.exe")) {
    return false;
  }

  wchar_t folderChars[MAX_PATH] = {};
  HRESULT hr = SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr,
                                SHGFP_TYPE_CURRENT, folderChars);
  if (NS_WARN_IF(FAILED(hr))) {
    return false;
  }

  nsAutoString folder;
  folder.Assign(folderChars);
  if (NS_WARN_IF(folder.IsEmpty())) {
    return false;
  }
  if (folder[folder.Length() - 1] != '\\') {
    folder.AppendLiteral("\\");
  }
  folder.AppendLiteral(
      "Microsoft\\Internet Explorer\\Quick Launch\\User Pinned\\TaskBar");
  nsAutoString pattern;
  pattern.Assign(folder);
  pattern.AppendLiteral("\\*.lnk");

  WIN32_FIND_DATAW findData = {};
  HANDLE hFindFile = FindFirstFileW(pattern.get(), &findData);
  if (hFindFile == INVALID_HANDLE_VALUE) {
    (void)NS_WARN_IF(GetLastError() != ERROR_FILE_NOT_FOUND);
    return false;
  }
  // Past this point we don't return until the end of the function,
  // when FindClose() is called.

  // Check all shortcuts until a match is found
  bool isPinned = false;
  do {
    nsAutoString fileName;
    fileName.Assign(folder);
    fileName.AppendLiteral("\\");
    fileName.Append(findData.cFileName);

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

    hr = persist->Load(fileName.get(), STGM_READ);
    if (NS_WARN_IF(FAILED(hr))) {
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
    // NOTE: Because this compares the path directly, it is possible to
    // have a false negative mismatch.
    if (wcsnicmp(storedExePath, exePath, MAXPATHLEN) == 0 ||
        wcsnicmp(storedExePath, pbExePath, MAXPATHLEN) == 0) {
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

      if (aumid.Equals(storedAUMID)) {
        isPinned = true;
        break;
      }
    }
  } while (FindNextFileW(hFindFile, &findData));

  FindClose(hFindFile);

  return isPinned;
}

static nsresult ManageShortcutTaskbarPins(bool aCheckOnly, bool aPinType,
                                          const nsAString& aShortcutPath) {
  // This enum is likely only used for Windows telemetry, INT_MAX is chosen to
  // avoid confusion with existing uses.
  enum PINNEDLISTMODIFYCALLER { PLMC_INT_MAX = INT_MAX };

  // The types below, and the idea of using IPinnedList3::Modify,
  // are thanks to Gee Law <https://geelaw.blog/entries/msedge-pins/>
  static constexpr GUID CLSID_TaskbandPin = {
      0x90aa3a4e,
      0x1cba,
      0x4233,
      {0xb8, 0xbb, 0x53, 0x57, 0x73, 0xd4, 0x84, 0x49}};

  static constexpr GUID IID_IPinnedList3 = {
      0x0dd79ae2,
      0xd156,
      0x45d4,
      {0x9e, 0xeb, 0x3b, 0x54, 0x97, 0x69, 0xe9, 0x40}};

  struct IPinnedList3Vtbl;
  struct IPinnedList3 {
    IPinnedList3Vtbl* vtbl;
  };

  typedef ULONG STDMETHODCALLTYPE ReleaseFunc(IPinnedList3 * that);
  typedef HRESULT STDMETHODCALLTYPE ModifyFunc(
      IPinnedList3 * that, PCIDLIST_ABSOLUTE unpin, PCIDLIST_ABSOLUTE pin,
      PINNEDLISTMODIFYCALLER caller);

  struct IPinnedList3Vtbl {
    void* QueryInterface;  // 0
    void* AddRef;          // 1
    ReleaseFunc* Release;  // 2
    void* Other[13];       // 3-15
    ModifyFunc* Modify;    // 16
  };

  struct ILFreeDeleter {
    void operator()(LPITEMIDLIST aPtr) {
      if (aPtr) {
        ILFree(aPtr);
      }
    }
  };

  mozilla::UniquePtr<__unaligned ITEMIDLIST, ILFreeDeleter> path(
      ILCreateFromPathW(nsString(aShortcutPath).get()));
  if (NS_WARN_IF(!path)) {
    return NS_ERROR_FILE_NOT_FOUND;
  }

  IPinnedList3* pinnedList = nullptr;
  HRESULT hr = CoCreateInstance(CLSID_TaskbandPin, NULL, CLSCTX_INPROC_SERVER,
                                IID_IPinnedList3, (void**)&pinnedList);
  if (FAILED(hr) || !pinnedList) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  if (!aCheckOnly) {
    hr = pinnedList->vtbl->Modify(pinnedList, aPinType ? NULL : path.get(),
                                  aPinType ? path.get() : NULL, PLMC_INT_MAX);
  }

  pinnedList->vtbl->Release(pinnedList);

  if (FAILED(hr)) {
    return NS_ERROR_FILE_ACCESS_DENIED;
  }
  return NS_OK;
}

static nsresult PinShortcutToTaskbarImpl(bool aCheckOnly,
                                         const nsAString& aAppUserModelId,
                                         const nsAString& aShortcutPath,
                                         const bool aFireAndForget = false) {
  // Verify shortcut is visible to `shell:appsfolder`. Shortcut creation -
  // during install or runtime - causes a race between it propagating to the
  // virtual `shell:appsfolder` and attempts to pin via `ITaskbarManager`,
  // resulting in pin failures when the latter occurs before the former. We can
  // skip this when we're in a MSIX build or only checking whether we're pinned.
  if (!widget::WinUtils::HasPackageIdentity() && !aCheckOnly &&
      !PollAppsFolderForShortcut(aAppUserModelId,
                                 TimeDuration::FromSeconds(15))) {
    return NS_ERROR_FILE_NOT_FOUND;
  }

  auto pinWithWin11TaskbarAPIResults =
      PinCurrentAppToTaskbarWin11(aCheckOnly, aAppUserModelId, aFireAndForget);
  switch (pinWithWin11TaskbarAPIResults.result) {
    case Win11PinToTaskBarResultStatus::NotSupported:
      // Fall through to the win 10 mechanism
      break;

    case Win11PinToTaskBarResultStatus::Success:
    case Win11PinToTaskBarResultStatus::AlreadyPinned:
      return NS_OK;

    case Win11PinToTaskBarResultStatus::NotPinned:
    case Win11PinToTaskBarResultStatus::NotCurrentlyAllowed:
    case Win11PinToTaskBarResultStatus::Failed:
      // return NS_ERROR_FAILURE;

      // Fall through to the old mechanism for now
      // In future, we should be sending telemetry for when
      // an error occurs or for when pinning is not allowed
      // with the Win 11 APIs.
      break;
  }

  return PinCurrentAppToTaskbarWin10(aCheckOnly, aAppUserModelId,
                                     aShortcutPath);
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
            nsresult rv = NS_ERROR_FAILURE;
            HRESULT hr = CoInitialize(nullptr);

            if (SUCCEEDED(hr)) {
              rv = PinShortcutToTaskbarImpl(
                  false, aumid, location.shortcutFile->NativePath());
              CoUninitialize();
            }

            NS_DispatchToMainThread(NS_NewRunnableFunction(
                "pinShortcutToTaskbar callback",
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

NS_IMETHODIMP
nsWindowsShellService::UnpinShortcutFromTaskbar(
    const nsAString& aShortcutFolder, const nsAString& aShortcutRelativePath) {
  const bool pinType = false;  // false means unpin
  const bool runInTestMode = false;

  ShortcutLocations location =
      MOZ_TRY(GetShortcutPaths(aShortcutFolder, aShortcutRelativePath));

  return ManageShortcutTaskbarPins(runInTestMode, pinType,
                                   location.shortcutFile->NativePath());
}

static nsresult PinCurrentAppToTaskbarWin10(bool aCheckOnly,
                                            const nsAString& aAppUserModelId,
                                            const nsAString& aShortcutPath) {
  // The behavior here is identical if we're only checking or if we try to pin
  // but the app is already pinned so we update the variable accordingly.
  if (!aCheckOnly) {
    aCheckOnly = IsCurrentAppPinnedToTaskbarSync(aAppUserModelId);
  }
  const bool pinType = true;  // true means pin
  return ManageShortcutTaskbarPins(aCheckOnly, pinType, aShortcutPath);
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

static nsresult PinCurrentAppToTaskbarImpl(
    bool aCheckOnly, bool aPrivateBrowsing, const bool aFireAndForget,
    const nsAString& aAppUserModelId, const nsAString& aShortcutName,
    const nsAString& aShortcutSubstring, nsIFile* aGreDir,
    const ShortcutLocations& location) {
  MOZ_DIAGNOSTIC_ASSERT(
      !NS_IsMainThread(),
      "PinCurrentAppToTaskbarImpl should be called off main thread only");

  nsAutoString shortcutPath;
  nsresult rv = FindPinnableShortcut(aAppUserModelId, aShortcutSubstring,
                                     aPrivateBrowsing, shortcutPath);
  if (NS_FAILED(rv)) {
    shortcutPath.Truncate();
  }
  if (shortcutPath.IsEmpty()) {
    if (aCheckOnly) {
      // Later checks rely on a shortcut already existing.
      // We don't want to create a shortcut in check only mode
      // so the best we can do is assume those parts will work.
      return NS_OK;
    }

    nsAutoString linkName(aShortcutName);

    nsCOMPtr<nsIFile> exeFile(aGreDir);
    if (aPrivateBrowsing) {
      nsAutoString pbExeStr(PRIVATE_BROWSING_BINARY);
      nsresult rv = exeFile->Append(pbExeStr);
      if (!NS_SUCCEEDED(rv)) {
        return NS_ERROR_FAILURE;
      }
    } else {
      wchar_t exePath[MAXPATHLEN] = {};
      if (NS_WARN_IF(NS_FAILED(BinaryPath::GetLong(exePath)))) {
        return NS_ERROR_FAILURE;
      }
      nsAutoString exeStr(exePath);
      nsresult rv = NS_NewLocalFile(exeStr, getter_AddRefs(exeFile));
      if (!NS_SUCCEEDED(rv)) {
        return NS_ERROR_FILE_NOT_FOUND;
      }
    }

    nsTArray<nsString> arguments;
    rv = CreateShortcutImpl(exeFile, arguments, aShortcutName, exeFile,
                            // Icon indexes are defined as Resource IDs, but
                            // CreateShortcutImpl needs an index.
                            IDI_APPICON - 1, aAppUserModelId, location,
                            linkName);
    if (!NS_SUCCEEDED(rv)) {
      return NS_ERROR_FILE_NOT_FOUND;
    }
  }
  return PinShortcutToTaskbarImpl(aCheckOnly, aAppUserModelId, shortcutPath,
                                  aFireAndForget);
}

static nsresult PinCurrentAppToTaskbarAsyncImpl(
    bool aCheckOnly, bool aPrivateBrowsing, JSContext* aCx,
    dom::Promise** aPromise, const bool aFireAndForget = false) {
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
      "CheckPinCurrentAppToTaskbarAsync promise", promise);

  NS_DispatchBackgroundTask(
      NS_NewRunnableFunction(
          "CheckPinCurrentAppToTaskbarAsync",
          [aCheckOnly, aPrivateBrowsing, aFireAndForget, shortcutName,
           aumid = nsString{aumid}, greDir, location = std::move(location),
           promiseHolder = std::move(promiseHolder)] {
            nsresult rv = NS_ERROR_FAILURE;
            HRESULT hr = CoInitialize(nullptr);

            if (SUCCEEDED(hr)) {
              nsAutoString shortcutSubstring;
              shortcutSubstring.AssignLiteral(MOZ_APP_DISPLAYNAME);
              rv = PinCurrentAppToTaskbarImpl(
                  aCheckOnly, aPrivateBrowsing, aFireAndForget, aumid,
                  shortcutName, shortcutSubstring, greDir.get(), location);
              CoUninitialize();
            }

            NS_DispatchToMainThread(NS_NewRunnableFunction(
                "CheckPinCurrentAppToTaskbarAsync callback",
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

NS_IMETHODIMP
nsWindowsShellService::PinCurrentAppToTaskbarAsync(bool aPrivateBrowsing,
                                                   bool aFireAndForget,
                                                   JSContext* aCx,
                                                   dom::Promise** aPromise) {
  return PinCurrentAppToTaskbarAsyncImpl(
      /* aCheckOnly */ false, aPrivateBrowsing, aCx, aPromise, aFireAndForget);
}

NS_IMETHODIMP
nsWindowsShellService::CheckPinCurrentAppToTaskbarAsync(
    bool aPrivateBrowsing, JSContext* aCx, dom::Promise** aPromise) {
  return PinCurrentAppToTaskbarAsyncImpl(
      /* aCheckOnly = */ true, aPrivateBrowsing, aCx, aPromise);
}

NS_IMETHODIMP
nsWindowsShellService::IsCurrentAppPinnedToTaskbarAsync(
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

  // A holder to pass the promise through the background task and back to
  // the main thread when finished.
  auto promiseHolder = MakeRefPtr<nsMainThreadPtrHolder<dom::Promise>>(
      "IsCurrentAppPinnedToTaskbarAsync promise", promise);

  // nsAString can't be captured by a lambda because it does not have a
  // public copy constructor
  nsAutoString capturedAumid(aumid);
  NS_DispatchBackgroundTask(
      NS_NewRunnableFunction(
          "IsCurrentAppPinnedToTaskbarAsync",
          [capturedAumid, promiseHolder = std::move(promiseHolder)] {
            bool isPinned = false;

            HRESULT hr = CoInitialize(nullptr);
            if (SUCCEEDED(hr)) {
              isPinned = IsCurrentAppPinnedToTaskbarSync(capturedAumid);
              CoUninitialize();
            }

            // Dispatch back to the main thread to resolve the promise.
            NS_DispatchToMainThread(NS_NewRunnableFunction(
                "IsCurrentAppPinnedToTaskbarAsync callback",
                [isPinned, promiseHolder = std::move(promiseHolder)] {
                  promiseHolder.get()->get()->MaybeResolve(isPinned);
                }));
          }),
      NS_DISPATCH_EVENT_MAY_BLOCK);

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

static void EnableLaunchOnLoginMSIXAsyncImpl(
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

static void DisableLaunchOnLoginMSIXAsyncImpl(
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

static void GetLaunchOnLoginEnabledMSIXAsyncImpl(
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
nsWindowsShellService::EnableLaunchOnLoginMSIXAsync(
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
      "EnableLaunchOnLoginMSIXAsync promise", promise);

  NS_DispatchBackgroundTask(NS_NewRunnableFunction(
      "EnableLaunchOnLoginMSIXAsync",
      [taskId = nsString(aTaskId), promiseHolder] {
        EnableLaunchOnLoginMSIXAsyncImpl(taskId, promiseHolder);
      }));

  promise.forget(aPromise);
  return NS_OK;
}

NS_IMETHODIMP
nsWindowsShellService::DisableLaunchOnLoginMSIXAsync(
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
      "DisableLaunchOnLoginMSIXAsync promise", promise);

  NS_DispatchBackgroundTask(NS_NewRunnableFunction(
      "DisableLaunchOnLoginMSIXAsync",
      [taskId = nsString(aTaskId), promiseHolder] {
        DisableLaunchOnLoginMSIXAsyncImpl(taskId, promiseHolder);
      }));

  promise.forget(aPromise);
  return NS_OK;
}

NS_IMETHODIMP
nsWindowsShellService::GetLaunchOnLoginEnabledMSIXAsync(
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
      "GetLaunchOnLoginEnabledMSIXAsync promise", promise);

  NS_DispatchBackgroundTask(NS_NewRunnableFunction(
      "GetLaunchOnLoginEnabledMSIXAsync",
      [taskId = nsString(aTaskId), promiseHolder] {
        GetLaunchOnLoginEnabledMSIXAsyncImpl(taskId, promiseHolder);
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

static void PinCurrentAppToStartMenuAsyncImpl(
    bool aCheckOnly,
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
          [promiseHolder, aCheckOnly](
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
                    [promiseHolder, entry, startScreenManager, aCheckOnly](
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
                      // If we're already pinned we can return early
                      // Ditto if we're just checking whether we *can* pin
                      if (isAlreadyPinned || aCheckOnly) {
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
                                  REJECT_AND_RETURN(promiseHolder,
                                                    NS_ERROR_FAILURE, E_FAIL);
                                };
                                boolean pinSuccess;
                                HRESULT hr = operation->GetResults(&pinSuccess);
                                if (FAILED(hr)) {
                                  REJECT_AND_RETURN(promiseHolder,
                                                    NS_ERROR_FAILURE, E_FAIL);
                                }
                                RESOLVE_AND_RETURN(promiseHolder,
                                                   pinSuccess ? true : false,
                                                   S_OK);
                              });
                      hr = pinOperation->put_Completed(
                          pinOperationCallback.Get());
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
nsWindowsShellService::PinCurrentAppToStartMenuAsync(bool aCheckOnly,
                                                     JSContext* aCx,
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
      "PinCurrentAppToStartMenuAsync promise", promise);
  NS_DispatchBackgroundTask(NS_NewRunnableFunction(
      "PinCurrentAppToStartMenuAsync", [aCheckOnly, promiseHolder] {
        PinCurrentAppToStartMenuAsyncImpl(aCheckOnly, promiseHolder);
      }));
  promise.forget(aPromise);
  return NS_OK;
}

static void IsCurrentAppPinnedToStartMenuAsyncImpl(
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
nsWindowsShellService::IsCurrentAppPinnedToStartMenuAsync(
    JSContext* aCx, dom::Promise** aPromise) {
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
      "IsCurrentAppPinnedToStartMenuAsync promise", promise);
  NS_DispatchBackgroundTask(NS_NewRunnableFunction(
      "IsCurrentAppPinnedToStartMenuAsync", [promiseHolder] {
        IsCurrentAppPinnedToStartMenuAsyncImpl(promiseHolder);
      }));
  promise.forget(aPromise);
  return NS_OK;
}

#else
NS_IMETHODIMP
nsWindowsShellService::EnableLaunchOnLoginMSIXAsync(
    const nsAString& aTaskId, JSContext* aCx,
    /* out */ dom::Promise** aPromise) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsWindowsShellService::DisableLaunchOnLoginMSIXAsync(
    const nsAString& aTaskId, JSContext* aCx,
    /* out */ dom::Promise** aPromise) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsWindowsShellService::GetLaunchOnLoginEnabledMSIXAsync(
    const nsAString& aTaskId, JSContext* aCx,
    /* out */ dom::Promise** aPromise) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsWindowsShellService::PinCurrentAppToStartMenuAsync(bool aCheckOnly,
                                                     JSContext* aCx,
                                                     dom::Promise** aPromise) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsWindowsShellService::IsCurrentAppPinnedToStartMenuAsync(
    JSContext* aCx, dom::Promise** aPromise) {
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

nsWindowsShellService::nsWindowsShellService() {}

nsWindowsShellService::~nsWindowsShellService() {}
