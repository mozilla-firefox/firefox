/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsDeviceContextSpecWin.h"

#include <wchar.h>
#include <windef.h>
#include <winspool.h>

#include <algorithm>
#include <array>

#include "mozilla/Logging.h"
#include "mozilla/Preferences.h"
#include "mozilla/RefPtr.h"
#include "mozilla/gfx/PrintPromise.h"
#include "mozilla/gfx/PrintTargetSkPDF.h"
#include "mozilla/gfx/PrintTargetWindows.h"
#include "mozilla/glean/PrintingMetrics.h"
#include "nsAnonymousTemporaryFile.h"

// winspool.h pollutes the global namespace, failing unified builds in e.g.
// nsIFormControl::SetForm. Undo the damage.
#undef AddForm
#undef DeleteForm
#undef EnumForms
#undef GetForm
#undef SetForm

#include "mozilla/gfx/Logging.h"
#include "nsComponentManagerUtils.h"
#include "nsIFileStreams.h"
#include "nsIPrintSettingsWin.h"
#include "nsIWidget.h"
#include "nsPrinterWin.h"
#include "nsReadableUtils.h"
#include "nsString.h"
#include "nsTArray.h"
#include "nsWindowsHelpers.h"

extern mozilla::LazyLogModule gPrintingLog;
#define PR_PL(_p1) MOZ_LOG(gPrintingLog, mozilla::LogLevel::Debug, _p1)

using namespace mozilla;
using namespace mozilla::gfx;

using namespace mozilla::widget;

static const wchar_t kDriverName[] = L"WINSPOOL";

static constexpr std::array<nsLiteralString, 15> kPDFPrinterPrefixes = {
    u"Microsoft Print to PDF"_ns,
    u"Adobe PDF"_ns,
    u"Bullzip PDF Printer"_ns,
    u"CutePDF Writer"_ns,
    u"doPDF"_ns,
    u"Foxit Reader PDF Printer"_ns,
    u"Nitro PDF Creator"_ns,
    u"novaPDF"_ns,
    u"PDF-XChange"_ns,
    u"PDF24 PDF"_ns,
    u"PDFCreator"_ns,
    u"PrimoPDF"_ns,
    u"Soda PDF"_ns,
    u"Solid PDF Creator"_ns,
    u"Universal Document Converter"_ns,
};

//----------------------------------------------------------------------------------
//---------------
// static members
//----------------------------------------------------------------------------------
nsDeviceContextSpecWin::nsDeviceContextSpecWin() = default;

//----------------------------------------------------------------------------------

NS_IMPL_ISUPPORTS(nsDeviceContextSpecWin, nsIDeviceContextSpec)

nsDeviceContextSpecWin::~nsDeviceContextSpecWin() {
  SetDevMode(nullptr);

  if (mTempFile) {
    mTempFile->Remove(/* recursive = */ false);
  }

  if (nsCOMPtr<nsIPrintSettingsWin> ps = do_QueryInterface(mPrintSettings)) {
    ps->SetDeviceName(u""_ns);
    ps->SetDriverName(u""_ns);
    ps->SetDevMode(nullptr);
  }
}

static bool GetDefaultPrinterName(nsAString& aDefaultPrinterName) {
  DWORD length = 0;
  GetDefaultPrinterW(nullptr, &length);

  if (length) {
    aDefaultPrinterName.SetLength(length);
    if (GetDefaultPrinterW((LPWSTR)aDefaultPrinterName.BeginWriting(),
                           &length)) {
      // `length` includes the terminating null, so we subtract that from our
      // string length.
      aDefaultPrinterName.SetLength(length - 1);
      PR_PL(("DEFAULT PRINTER [%s]\n",
             NS_ConvertUTF16toUTF8(aDefaultPrinterName).get()));
      return true;
    }
  }

  aDefaultPrinterName.Truncate();
  PR_PL(("NO DEFAULT PRINTER\n"));
  return false;
}

//----------------------------------------------------------------------------------
NS_IMETHODIMP nsDeviceContextSpecWin::Init(nsIPrintSettings* aPrintSettings,
                                           bool aIsPrintPreview) {
  mPrintSettings = aPrintSettings;

  // Get the Printer Name to be used and output format.
  nsAutoString printerName;
  if (mPrintSettings) {
    mOutputFormat = mPrintSettings->GetOutputFormat();
    mPrintSettings->GetPrinterName(printerName);
  }

  // If there is no name then use the default printer
  if (printerName.IsEmpty()) {
    GetDefaultPrinterName(printerName);
  }

  // Gather telemetry on the print target type.
  //
  // Unfortunately, if we're not using our own internal save-to-pdf codepaths,
  // there isn't a good way to determine whether a print is going to be to a
  // physical printer or to a file or some other non-physical output. We do our
  // best by checking for what seems to be the most common save-to-PDF virtual
  // printers.
  //
  // We use StringBeginsWith below, since printer names are often followed by a
  // version number or other product differentiating string.  (True for doPDF,
  // novaPDF, PDF-XChange and Soda PDF, for example.)
  if (mOutputFormat == nsIPrintSettings::kOutputFormatPDF) {
    glean::printing::target_type
        .EnumGet(glean::printing::TargetTypeLabel::ePdfFile)
        .Add(1);
  } else if (std::any_of(kPDFPrinterPrefixes.begin(), kPDFPrinterPrefixes.end(),
                         [&](const auto& prefix) {
                           return StringBeginsWith(printerName, prefix);
                         })) {
    glean::printing::target_type
        .EnumGet(glean::printing::TargetTypeLabel::ePdfFile)
        .Add(1);
  } else if (printerName.EqualsLiteral("Microsoft XPS Document Writer")) {
    glean::printing::target_type
        .EnumGet(glean::printing::TargetTypeLabel::eXpsFile)
        .Add(1);
  } else {
    nsAString::const_iterator start, end;
    printerName.BeginReading(start);
    printerName.EndReading(end);
    if (CaseInsensitiveFindInReadable(u"pdf"_ns, start, end)) {
      glean::printing::target_type
          .EnumGet(glean::printing::TargetTypeLabel::ePdfUnknown)
          .Add(1);
    } else {
      glean::printing::target_type
          .EnumGet(glean::printing::TargetTypeLabel::eUnknown)
          .Add(1);
    }
  }

  nsresult rv = NS_ERROR_GFX_PRINTER_NO_PRINTER_AVAILABLE;
  if (aPrintSettings) {
    // If we're in the child or we're printing to PDF we only need information
    // from the print settings.
    if (XRE_IsContentProcess() ||
        mOutputFormat == nsIPrintSettings::kOutputFormatPDF) {
      return NS_OK;
    }

    nsCOMPtr<nsIPrintSettingsWin> psWin(do_QueryInterface(aPrintSettings));
    if (psWin) {
      nsAutoString deviceName;
      nsAutoString driverName;
      psWin->GetDeviceName(deviceName);
      psWin->GetDriverName(driverName);

      LPDEVMODEW devMode;
      psWin->GetDevMode(&devMode);  // creates new memory (makes a copy)

      if (!deviceName.IsEmpty() && !driverName.IsEmpty() && devMode) {
        // Scaling is special, it is one of the few
        // devMode items that we control in layout
        if (devMode->dmFields & DM_SCALE) {
          double scale = double(devMode->dmScale) / 100.0f;
          if (scale != 1.0) {
            aPrintSettings->SetScaling(scale);
            devMode->dmScale = 100;
          }
        }

        SetDeviceName(deviceName);
        SetDriverName(driverName);
        SetDevMode(devMode);

        return NS_OK;
      } else {
        PR_PL(
            ("***** nsDeviceContextSpecWin::Init - "
             "deviceName/driverName/devMode was NULL!\n"));
        if (devMode) ::HeapFree(::GetProcessHeap(), 0, devMode);
      }
    }
  } else {
    PR_PL(("***** nsDeviceContextSpecWin::Init - aPrintSettingswas NULL!\n"));
  }

  if (printerName.IsEmpty()) {
    return rv;
  }

  return GetDataFromPrinter(printerName, mPrintSettings);
}

//----------------------------------------------------------

already_AddRefed<PrintTarget> nsDeviceContextSpecWin::MakePrintTarget() {
  NS_ASSERTION(mDevMode || mOutputFormat == nsIPrintSettings::kOutputFormatPDF,
               "DevMode can't be NULL here unless we're printing to PDF.");
  if (mOutputFormat == nsIPrintSettings::kOutputFormatPDF) {
    auto stream = [&]() -> nsCOMPtr<nsIOutputStream> {
      if (mPrintSettings->GetOutputDestination() ==
          nsIPrintSettings::kOutputDestinationStream) {
        nsCOMPtr<nsIOutputStream> out;
        mPrintSettings->GetOutputStream(getter_AddRefs(out));
        return out;
      }

      // Even if the destination may be a named path, write to a temp file -
      // this is consistent with behaviour of `PrintTarget` on other platforms.
      nsCOMPtr<nsIFile> file;
      nsresult rv = NS_OpenAnonymousTemporaryNsIFile(getter_AddRefs(mTempFile));
      file = mTempFile;

      if (NS_WARN_IF(NS_FAILED(rv))) {
        return nullptr;
      }

      nsCOMPtr<nsIFileOutputStream> stream =
          do_CreateInstance("@mozilla.org/network/file-output-stream;1");
      if (NS_FAILED(stream->Init(file, -1, -1, 0))) {
        return nullptr;
      }
      return stream;
    }();

    return PrintTargetSkPDF::CreateOrNull(stream);
  }

  if (mDevMode) {
    NS_WARNING_ASSERTION(!mDriverName.IsEmpty(), "No driver!");
    HDC dc =
        ::CreateDCW(mDriverName.get(), mDeviceName.get(), nullptr, mDevMode);
    if (!dc) {
      gfxCriticalError(gfxCriticalError::DefaultOptions(false))
          << "Failed to create device context in GetSurfaceForPrinter";
      return nullptr;
    }

    // The PrintTargetWindows takes over ownership of this DC
    return PrintTargetWindows::CreateOrNull(dc);
  }

  return nullptr;
}

RefPtr<PrintEndDocumentPromise> nsDeviceContextSpecWin::EndDocument() {
  if (mPrintSettings->GetOutputDestination() !=
          nsIPrintSettings::kOutputDestinationFile ||
      mOutputFormat != nsIPrintSettings::kOutputFormatPDF) {
    return PrintEndDocumentPromise::CreateAndResolve(true, __func__);
  }

  MOZ_ASSERT(mTempFile, "No handle to temporary PDF file.");

  nsAutoString targetPath;
  mPrintSettings->GetToFileName(targetPath);

  if (targetPath.IsEmpty()) {
    return PrintEndDocumentPromise::CreateAndResolve(true, __func__);
  }

  // We still need to move the file to its actual destination.
  nsCOMPtr<nsIFile> destFile;
  auto rv = NS_NewLocalFile(targetPath, getter_AddRefs(destFile));
  if (NS_FAILED(rv)) {
    return PrintEndDocumentPromise::CreateAndReject(rv, __func__);
  }

  return nsIDeviceContextSpec::EndDocumentAsync(
      __func__,
      [destFile = std::move(destFile),
       tempFile = std::move(mTempFile)]() -> nsresult {
        nsAutoString destLeafName;
        MOZ_TRY(destFile->GetLeafName(destLeafName));

        nsCOMPtr<nsIFile> destDir;
        MOZ_TRY(destFile->GetParent(getter_AddRefs(destDir)));

        // This should be fine - Windows API calls usually prevent moving
        // between different volumes (See Win32 API's `MOVEFILE_COPY_ALLOWED`
        // flag), but we handle that down this call.
        MOZ_TRY(tempFile->MoveTo(destDir, destLeafName));
        return NS_OK;
      });
}

//----------------------------------------------------------------------------------
void nsDeviceContextSpecWin::SetDeviceName(const nsAString& aDeviceName) {
  mDeviceName = aDeviceName;
}

//----------------------------------------------------------------------------------
void nsDeviceContextSpecWin::SetDriverName(const nsAString& aDriverName) {
  mDriverName = aDriverName;
}

//----------------------------------------------------------------------------------
void nsDeviceContextSpecWin::SetDevMode(LPDEVMODEW aDevMode) {
  if (mDevMode) {
    ::HeapFree(::GetProcessHeap(), 0, mDevMode);
  }

  mDevMode = aDevMode;
}

//------------------------------------------------------------------
void nsDeviceContextSpecWin::GetDevMode(LPDEVMODEW& aDevMode) {
  aDevMode = mDevMode;
}

#define DISPLAY_LAST_ERROR

//----------------------------------------------------------------------------------
// Setup the object's data member with the selected printer's data
nsresult nsDeviceContextSpecWin::GetDataFromPrinter(const nsAString& aName,
                                                    nsIPrintSettings* aPS) {
  nsresult rv = NS_ERROR_FAILURE;

  nsHPRINTER hPrinter = nullptr;
  const nsString& flat = PromiseFlatString(aName);
  wchar_t* name =
      (wchar_t*)flat.get();  // Windows APIs use non-const name argument

  BOOL status = ::OpenPrinterW(name, &hPrinter, nullptr);
  if (status) {
    nsAutoPrinter autoPrinter(hPrinter);

    LPDEVMODEW pDevMode;

    // Allocate a buffer of the correct size.
    LONG needed =
        ::DocumentPropertiesW(nullptr, hPrinter, name, nullptr, nullptr, 0);
    if (needed < 0) {
      PR_PL(
          ("**** nsDeviceContextSpecWin::GetDataFromPrinter - Couldn't get "
           "size of DEVMODE using DocumentPropertiesW(pDeviceName = \"%s\"). "
           "GetLastEror() = %08lx\n",
           NS_ConvertUTF16toUTF8(aName).get(), GetLastError()));
      return NS_ERROR_FAILURE;
    }

    // Some drivers do not return the correct size for their DEVMODE, so we
    // over-allocate to try and compensate.
    // (See https://bugzilla.mozilla.org/show_bug.cgi?id=1664530#c5)
    needed *= 2;
    pDevMode =
        (LPDEVMODEW)::HeapAlloc(::GetProcessHeap(), HEAP_ZERO_MEMORY, needed);
    if (!pDevMode) return NS_ERROR_FAILURE;

    // Get the default DevMode for the printer and modify it for our needs.
    LONG ret = ::DocumentPropertiesW(nullptr, hPrinter, name, pDevMode, nullptr,
                                     DM_OUT_BUFFER);

    if (ret == IDOK && aPS) {
      nsCOMPtr<nsIPrintSettingsWin> psWin = do_QueryInterface(aPS);
      MOZ_ASSERT(psWin);
      psWin->CopyToNative(pDevMode);
      // Sets back the changes we made to the DevMode into the Printer Driver
      ret = ::DocumentPropertiesW(nullptr, hPrinter, name, pDevMode, pDevMode,
                                  DM_IN_BUFFER | DM_OUT_BUFFER);

      // We need to copy the final DEVMODE settings back to our print settings,
      // because they may have been set from invalid prefs.
      if (ret == IDOK) {
        // We need to get information from the device as well.
        nsAutoHDC printerDC(::CreateICW(kDriverName, name, nullptr, pDevMode));
        if (NS_WARN_IF(!printerDC)) {
          ::HeapFree(::GetProcessHeap(), 0, pDevMode);
          return NS_ERROR_FAILURE;
        }

        psWin->CopyFromNative(printerDC, pDevMode);
      }
    }

    if (ret != IDOK) {
      ::HeapFree(::GetProcessHeap(), 0, pDevMode);
      PR_PL(
          ("***** nsDeviceContextSpecWin::GetDataFromPrinter - "
           "DocumentProperties call failed code: %ld/0x%lx\n",
           ret, ret));
      DISPLAY_LAST_ERROR
      return NS_ERROR_FAILURE;
    }

    SetDevMode(
        pDevMode);  // cache the pointer and takes responsibility for the memory

    SetDeviceName(aName);

    SetDriverName(nsDependentString(kDriverName));

    rv = NS_OK;
  } else {
    rv = NS_ERROR_GFX_PRINTER_NAME_NOT_FOUND;
    PR_PL(
        ("***** nsDeviceContextSpecWin::GetDataFromPrinter - Couldn't open "
         "printer: [%s]\n",
         NS_ConvertUTF16toUTF8(aName).get()));
    DISPLAY_LAST_ERROR
  }
  return rv;
}

//***********************************************************
//  Printer List
//***********************************************************

nsPrinterListWin::~nsPrinterListWin() = default;

// Helper to get the array of PRINTER_INFO_4 records from the OS into a
// caller-supplied byte array; returns the number of records present.
static unsigned GetPrinterInfo4(nsTArray<BYTE>& aBuffer) {
  const DWORD kLevel = 4;
  DWORD needed = 0;
  DWORD count = 0;
  const DWORD kFlags = PRINTER_ENUM_LOCAL | PRINTER_ENUM_CONNECTIONS;
  BOOL ok = ::EnumPrintersW(kFlags,
                            nullptr,  // Name
                            kLevel,   // Level
                            nullptr,  // pPrinterEnum
                            0,        // cbBuf (buffer size)
                            &needed,  // Bytes needed in buffer
                            &count);
  if (needed > 0) {
    if (!aBuffer.SetLength(needed, fallible)) {
      return 0;
    }
    ok = ::EnumPrintersW(kFlags, nullptr, kLevel, aBuffer.Elements(),
                         aBuffer.Length(), &needed, &count);
  }
  if (!ok) {
    return 0;
  }
  return count;
}

nsTArray<nsPrinterListBase::PrinterInfo> nsPrinterListWin::Printers() const {
  PR_PL(("nsPrinterListWin::Printers\n"));

  AutoTArray<BYTE, 1024> buffer;
  unsigned count = GetPrinterInfo4(buffer);

  if (!count) {
    PR_PL(("[No printers found]\n"));
    return {};
  }

  const auto* printers =
      reinterpret_cast<const _PRINTER_INFO_4W*>(buffer.Elements());
  nsTArray<PrinterInfo> list;
  for (unsigned i = 0; i < count; i++) {
    // For LOCAL printers, we check whether OpenPrinter succeeds, and omit
    // them from the list if not. This avoids presenting printers that the
    // user cannot actually use (e.g. due to Windows permissions).
    // For NETWORK printers, this check may block for a long time (waiting for
    // network timeout), so we skip it; if the user tries to access a printer
    // that isn't available, we'll have to show an error later.
    // (We always need to be able to handle an error, anyhow, as the printer
    // could get disconnected after we've created the list, for example.)
    bool isAvailable = false;
    // PRINTER_ATTRIBUTE_NETWORK feeds the sortAfterLocal hint on Windows; see
    // the sortAfterLocal doc-comment in nsIPrinter.idl for why this isn't a
    // precise "auto-discovered" signal.
    const bool isNetwork =
        !!(printers[i].Attributes & PRINTER_ATTRIBUTE_NETWORK);
    if (isNetwork) {
      isAvailable = true;
    } else if (printers[i].Attributes & PRINTER_ATTRIBUTE_LOCAL) {
      HANDLE handle;
      if (::OpenPrinterW(printers[i].pPrinterName, &handle, nullptr)) {
        ::ClosePrinter(handle);
        isAvailable = true;
      }
    }
    if (isAvailable) {
      list.AppendElement(PrinterInfo{nsString(printers[i].pPrinterName),
                                     nullptr,
                                     /* mSortAfterLocal = */ isNetwork});
      PR_PL(("Printer Name: %s\n",
             NS_ConvertUTF16toUTF8(printers[i].pPrinterName).get()));
    }
  }

  if (list.IsEmpty()) {
    PR_PL(("[No usable printers found]\n"));
    return {};
  }

  list.Sort([](const PrinterInfo& a, const PrinterInfo& b) {
    size_t len = std::min(a.mName.Length(), b.mName.Length());
    int result = CaseInsensitiveCompare(a.mName.BeginReading(),
                                        b.mName.BeginReading(), len);
    return result ? result : int(a.mName.Length()) - int(b.mName.Length());
  });

  return list;
}

Maybe<nsPrinterListBase::PrinterInfo> nsPrinterListWin::PrinterByName(
    nsString aName) const {
  Maybe<PrinterInfo> rv;

  AutoTArray<BYTE, 1024> buffer;
  unsigned count = GetPrinterInfo4(buffer);

  const auto* printers =
      reinterpret_cast<const _PRINTER_INFO_4W*>(buffer.Elements());
  for (unsigned i = 0; i < count; ++i) {
    if (aName.Equals(nsString(printers[i].pPrinterName))) {
      rv.emplace(
          PrinterInfo{aName, nullptr,
                      !!(printers[i].Attributes & PRINTER_ATTRIBUTE_NETWORK)});
      break;
    }
  }

  return rv;
}

Maybe<nsPrinterListBase::PrinterInfo> nsPrinterListWin::PrinterBySystemName(
    nsString aName) const {
  return PrinterByName(std::move(aName));
}

RefPtr<nsIPrinter> nsPrinterListWin::CreatePrinter(PrinterInfo aInfo) const {
  return nsPrinterWin::Create(mCommonPaperInfo, std::move(aInfo.mName),
                              aInfo.mSortAfterLocal);
}

nsresult nsPrinterListWin::SystemDefaultPrinterName(nsAString& aName) const {
  if (!GetDefaultPrinterName(aName)) {
    NS_WARNING("Uh oh, GetDefaultPrinterName failed");
    // Indicate failure by leaving aName untouched, i.e. the empty string.
  }
  return NS_OK;
}

NS_IMETHODIMP
nsPrinterListWin::InitPrintSettingsFromPrinter(
    const nsAString& aPrinterName, nsIPrintSettings* aPrintSettings) {
  NS_ENSURE_ARG_POINTER(aPrintSettings);

  if (aPrinterName.IsEmpty()) {
    return NS_OK;
  }

  // When printing to PDF on Windows there is no associated printer driver.
  int16_t outputFormat = aPrintSettings->GetOutputFormat();
  if (outputFormat == nsIPrintSettings::kOutputFormatPDF) {
    return NS_OK;
  }

  auto devSpecWin = MakeRefPtr<nsDeviceContextSpecWin>();

  // If the settings have already been initialized from prefs then pass these to
  // GetDataFromPrinter, so that they are saved to the printer.
  bool initializedFromPrefs;
  nsresult rv =
      aPrintSettings->GetIsInitializedFromPrefs(&initializedFromPrefs);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }
  if (initializedFromPrefs) {
    // If we pass in print settings to GetDataFromPrinter it already copies
    // things back to the settings, so we can return here.
    return devSpecWin->GetDataFromPrinter(aPrinterName, aPrintSettings);
  }

  devSpecWin->GetDataFromPrinter(aPrinterName);

  LPDEVMODEW devmode;
  devSpecWin->GetDevMode(devmode);
  if (NS_WARN_IF(!devmode)) {
    return NS_ERROR_FAILURE;
  }

  aPrintSettings->SetPrinterName(aPrinterName);

  // We need to get information from the device as well.
  const nsString& flat = PromiseFlatString(aPrinterName);
  char16ptr_t printerName = flat.get();
  HDC dc = ::CreateICW(kDriverName, printerName, nullptr, devmode);
  if (NS_WARN_IF(!dc)) {
    return NS_ERROR_FAILURE;
  }

  nsCOMPtr<nsIPrintSettingsWin> psWin = do_QueryInterface(aPrintSettings);
  MOZ_ASSERT(psWin);
  psWin->CopyFromNative(dc, devmode);
  ::DeleteDC(dc);

  return NS_OK;
}
