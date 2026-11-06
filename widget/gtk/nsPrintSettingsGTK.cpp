/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsPrintSettingsGTK.h"

#include "nsIFile.h"
#include "nsNetUtil.h"

// These constants are the the strings that GTK expects as key-value pairs for
// setting CUPS duplex modes. These are not universal to all CUPS systems, which
// is why they are local to this file.
static constexpr gchar kCupsDuplex[] = "cups-Duplex";
static constexpr gchar kCupsDuplexNone[] = "None";
static constexpr gchar kCupsDuplexNoTumble[] = "DuplexNoTumble";
static constexpr gchar kCupsDuplexTumble[] = "DuplexTumble";

static GtkPaperSize* moz_gtk_paper_size_copy_to_new_custom(
    GtkPaperSize* oldPaperSize) {
  // We make a "custom-ified" copy of the paper size so it can be changed later.
  return gtk_paper_size_new_custom(
      gtk_paper_size_get_name(oldPaperSize),
      gtk_paper_size_get_display_name(oldPaperSize),
      gtk_paper_size_get_width(oldPaperSize, GTK_UNIT_INCH),
      gtk_paper_size_get_height(oldPaperSize, GTK_UNIT_INCH), GTK_UNIT_INCH);
}

NS_IMPL_ISUPPORTS_INHERITED(nsPrintSettingsGTK, nsPrintSettings,
                            nsPrintSettingsGTK)

/** ---------------------------------------------------
 */
nsPrintSettingsGTK::nsPrintSettingsGTK()
    : mPageSetup(nullptr), mPrintSettings(nullptr), mGTKPrinter(nullptr) {
  // The aim here is to set up the objects enough that silent printing works
  // well. These will be replaced anyway if the print dialog is used.
  mPrintSettings = gtk_print_settings_new();
  GtkPageSetup* pageSetup = gtk_page_setup_new();
  SetGtkPageSetup(pageSetup);
  g_object_unref(pageSetup);

  SetOutputFormat(nsIPrintSettings::kOutputFormatNative);
}

already_AddRefed<nsIPrintSettings> CreatePlatformPrintSettings(
    const mozilla::PrintSettingsInitializer& aSettings) {
  auto settings = mozilla::MakeRefPtr<nsPrintSettingsGTK>();
  settings->InitWithInitializer(aSettings);
  settings->SetDefaultFileName();
  return settings.forget();
}

/** ---------------------------------------------------
 */
nsPrintSettingsGTK::~nsPrintSettingsGTK() {
  if (mPageSetup) {
    g_object_unref(mPageSetup);
    mPageSetup = nullptr;
  }
  if (mPrintSettings) {
    g_object_unref(mPrintSettings);
    mPrintSettings = nullptr;
  }
  if (mGTKPrinter) {
    g_object_unref(mGTKPrinter);
    mGTKPrinter = nullptr;
  }
}

/** ---------------------------------------------------
 */
nsPrintSettingsGTK::nsPrintSettingsGTK(const nsPrintSettingsGTK& aPS)
    : mPageSetup(nullptr), mPrintSettings(nullptr), mGTKPrinter(nullptr) {
  *this = aPS;
}

/** ---------------------------------------------------
 */
nsPrintSettingsGTK& nsPrintSettingsGTK::operator=(
    const nsPrintSettingsGTK& rhs) {
  if (this == &rhs) {
    return *this;
  }

  nsPrintSettings::operator=(rhs);

  if (mPageSetup) g_object_unref(mPageSetup);
  mPageSetup = gtk_page_setup_copy(rhs.mPageSetup);
  // NOTE: No need to re-initialize mUnwriteableMargin here (even
  // though mPageSetup is changing). It'll be copied correctly by
  // nsPrintSettings::operator=.

  if (mPrintSettings) g_object_unref(mPrintSettings);
  mPrintSettings = gtk_print_settings_copy(rhs.mPrintSettings);

  if (mGTKPrinter) g_object_unref(mGTKPrinter);

  if (rhs.mGTKPrinter) {
    g_object_ref(rhs.mGTKPrinter);
  }
  mGTKPrinter = rhs.mGTKPrinter;

  return *this;
}

/** -------------------------------------------
 */
nsresult nsPrintSettingsGTK::_Clone(nsIPrintSettings** _retval) {
  NS_ENSURE_ARG_POINTER(_retval);
  *_retval = nullptr;

  nsPrintSettingsGTK* newSettings = new nsPrintSettingsGTK(*this);
  if (!newSettings) return NS_ERROR_FAILURE;
  *_retval = newSettings;
  NS_ADDREF(*_retval);
  return NS_OK;
}

/** -------------------------------------------
 */
NS_IMETHODIMP
nsPrintSettingsGTK::_Assign(nsIPrintSettings* aPS) {
  nsPrintSettingsGTK* printSettingsGTK = static_cast<nsPrintSettingsGTK*>(aPS);
  if (!printSettingsGTK) return NS_ERROR_UNEXPECTED;
  *this = *printSettingsGTK;
  return NS_OK;
}

/** ---------------------------------------------------
 */
void nsPrintSettingsGTK::SetGtkPageSetup(GtkPageSetup* aPageSetup) {
  if (mPageSetup) g_object_unref(mPageSetup);

  mPageSetup = (GtkPageSetup*)g_object_ref(aPageSetup);
  InitUnwriteableMargin();

  // If the paper size is not custom, then we make a custom copy of the
  // GtkPaperSize, so it can be mutable. If a GtkPaperSize wasn't made as
  // custom, its properties are immutable.
  GtkPaperSize* paperSize = gtk_page_setup_get_paper_size(aPageSetup);
  if (!gtk_paper_size_is_custom(paperSize)) {
    GtkPaperSize* customPaperSize =
        moz_gtk_paper_size_copy_to_new_custom(paperSize);
    gtk_page_setup_set_paper_size(mPageSetup, customPaperSize);
    gtk_paper_size_free(customPaperSize);
  }
  SaveNewPageSize();
}

/** ---------------------------------------------------
 */
void nsPrintSettingsGTK::SetGtkPrintSettings(GtkPrintSettings* aPrintSettings) {
  if (mPrintSettings) g_object_unref(mPrintSettings);

  mPrintSettings = (GtkPrintSettings*)g_object_ref(aPrintSettings);

  GtkPaperSize* paperSize = gtk_print_settings_get_paper_size(aPrintSettings);
  if (paperSize) {
    GtkPaperSize* customPaperSize =
        moz_gtk_paper_size_copy_to_new_custom(paperSize);
    gtk_paper_size_free(paperSize);
    gtk_page_setup_set_paper_size(mPageSetup, customPaperSize);
    gtk_paper_size_free(customPaperSize);
  } else {
    // paperSize was null, and so we add the paper size in the GtkPageSetup to
    // the settings.
    SaveNewPageSize();
  }
}

/** ---------------------------------------------------
 */
void nsPrintSettingsGTK::SetGtkPrinter(GtkPrinter* aPrinter) {
  if (mGTKPrinter) g_object_unref(mGTKPrinter);

  mGTKPrinter = (GtkPrinter*)g_object_ref(aPrinter);
}

NS_IMETHODIMP nsPrintSettingsGTK::GetOutputFormat(int16_t* aOutputFormat) {
  NS_ENSURE_ARG_POINTER(aOutputFormat);

  int16_t format;
  nsresult rv = nsPrintSettings::GetOutputFormat(&format);
  if (NS_FAILED(rv)) {
    return rv;
  }

  if (format == nsIPrintSettings::kOutputFormatNative &&
      GTK_IS_PRINTER(mGTKPrinter)) {
    format = nsIPrintSettings::kOutputFormatPDF;
  }

  *aOutputFormat = format;
  return NS_OK;
}

/**
 * Reimplementation of nsPrintSettings functions so that we get the values
 * from the GTK objects rather than our own variables.
 */

NS_IMETHODIMP
nsPrintSettingsGTK::SetPageRanges(const nsTArray<int32_t>& aRanges) {
  if (aRanges.Length() % 2 != 0) {
    return NS_ERROR_FAILURE;
  }

  gtk_print_settings_set_print_pages(
      mPrintSettings,
      aRanges.IsEmpty() ? GTK_PRINT_PAGES_ALL : GTK_PRINT_PAGES_RANGES);

  nsTArray<GtkPageRange> ranges;
  ranges.SetCapacity(aRanges.Length() / 2);
  for (size_t i = 0; i < aRanges.Length(); i += 2) {
    GtkPageRange* gtkRange = ranges.AppendElement();
    gtkRange->start = aRanges[i] - 1;
    gtkRange->end = aRanges[i + 1] - 1;
  }

  gtk_print_settings_set_page_ranges(mPrintSettings, ranges.Elements(),
                                     ranges.Length());
  return NS_OK;
}

NS_IMETHODIMP
nsPrintSettingsGTK::GetPrintInColor(bool* aPrintInColor) {
  *aPrintInColor = gtk_print_settings_get_use_color(mPrintSettings);
  return NS_OK;
}
NS_IMETHODIMP
nsPrintSettingsGTK::SetPrintInColor(bool aPrintInColor) {
  gtk_print_settings_set_use_color(mPrintSettings, aPrintInColor);
  return NS_OK;
}

NS_IMETHODIMP
nsPrintSettingsGTK::GetOrientation(int32_t* aOrientation) {
  NS_ENSURE_ARG_POINTER(aOrientation);

  GtkPageOrientation gtkOrient = gtk_page_setup_get_orientation(mPageSetup);
  switch (gtkOrient) {
    case GTK_PAGE_ORIENTATION_LANDSCAPE:
    case GTK_PAGE_ORIENTATION_REVERSE_LANDSCAPE:
      *aOrientation = kLandscapeOrientation;
      break;

    case GTK_PAGE_ORIENTATION_PORTRAIT:
    case GTK_PAGE_ORIENTATION_REVERSE_PORTRAIT:
    default:
      *aOrientation = kPortraitOrientation;
  }
  return NS_OK;
}
NS_IMETHODIMP
nsPrintSettingsGTK::SetOrientation(int32_t aOrientation) {
  GtkPageOrientation gtkOrient;
  if (aOrientation == kLandscapeOrientation)
    gtkOrient = GTK_PAGE_ORIENTATION_LANDSCAPE;
  else
    gtkOrient = GTK_PAGE_ORIENTATION_PORTRAIT;

  gtk_print_settings_set_orientation(mPrintSettings, gtkOrient);
  gtk_page_setup_set_orientation(mPageSetup, gtkOrient);
  return NS_OK;
}

NS_IMETHODIMP
nsPrintSettingsGTK::GetToFileName(nsAString& aToFileName) {
  // Get the gtk output filename
  const char* gtk_output_uri =
      gtk_print_settings_get(mPrintSettings, GTK_PRINT_SETTINGS_OUTPUT_URI);
  if (!gtk_output_uri) {
    aToFileName = mToFileName;
    return NS_OK;
  }

  // Convert to an nsIFile
  nsCOMPtr<nsIFile> file;
  nsresult rv = NS_GetFileFromURLSpec(nsDependentCString(gtk_output_uri),
                                      getter_AddRefs(file));
  if (NS_FAILED(rv)) return rv;

  // Extract the path
  return file->GetPath(aToFileName);
}

NS_IMETHODIMP
nsPrintSettingsGTK::SetToFileName(const nsAString& aToFileName) {
  if (aToFileName.IsEmpty()) {
    mToFileName.SetLength(0);
    gtk_print_settings_set(mPrintSettings, GTK_PRINT_SETTINGS_OUTPUT_URI,
                           nullptr);
    return NS_OK;
  }

  gtk_print_settings_set(mPrintSettings, GTK_PRINT_SETTINGS_OUTPUT_FILE_FORMAT,
                         "pdf");

  nsCOMPtr<nsIFile> file;
  nsresult rv = NS_NewLocalFile(aToFileName, getter_AddRefs(file));
  NS_ENSURE_SUCCESS(rv, rv);

  // Convert the nsIFile to a URL
  nsAutoCString url;
  rv = NS_GetURLSpecFromFile(file, url);
  NS_ENSURE_SUCCESS(rv, rv);

  gtk_print_settings_set(mPrintSettings, GTK_PRINT_SETTINGS_OUTPUT_URI,
                         url.get());
  mToFileName = aToFileName;

  return NS_OK;
}

NS_IMETHODIMP
nsPrintSettingsGTK::GetPrinterName(nsAString& aPrinter) {
  const char* gtkPrintName = gtk_print_settings_get_printer(mPrintSettings);
  if (!gtkPrintName) {
    if (GTK_IS_PRINTER(mGTKPrinter)) {
      gtkPrintName = gtk_printer_get_name(mGTKPrinter);
    } else {
      // This mimics what nsPrintSettingsImpl does when we try to Get before we
      // Set
      aPrinter.Truncate();
      return NS_OK;
    }
  }
  CopyUTF8toUTF16(mozilla::MakeStringSpan(gtkPrintName), aPrinter);
  return NS_OK;
}

NS_IMETHODIMP
nsPrintSettingsGTK::SetPrinterName(const nsAString& aPrinter) {
  NS_ConvertUTF16toUTF8 gtkPrinter(aPrinter);

  if (StringBeginsWith(gtkPrinter, "CUPS/"_ns)) {
    // Strip off "CUPS/"; GTK might recognize the rest
    gtkPrinter.Cut(0, strlen("CUPS/"));
  }

  // Give mPrintSettings the passed-in printer name if either...
  // - it has no printer name stored yet
  // - it has an existing printer name that's different from
  //   the name passed to this function.
  const char* oldPrinterName = gtk_print_settings_get_printer(mPrintSettings);
  if (!oldPrinterName || !gtkPrinter.Equals(oldPrinterName)) {
    mIsInitedFromPrinter = false;
    mIsInitedFromPrefs = false;
    gtk_print_settings_set_printer(mPrintSettings, gtkPrinter.get());
  }

  return NS_OK;
}

NS_IMETHODIMP
nsPrintSettingsGTK::GetNumCopies(int32_t* aNumCopies) {
  NS_ENSURE_ARG_POINTER(aNumCopies);
  *aNumCopies = gtk_print_settings_get_n_copies(mPrintSettings);
  return NS_OK;
}
NS_IMETHODIMP
nsPrintSettingsGTK::SetNumCopies(int32_t aNumCopies) {
  gtk_print_settings_set_n_copies(mPrintSettings, aNumCopies);
  return NS_OK;
}

NS_IMETHODIMP
nsPrintSettingsGTK::GetScaling(double* aScaling) {
  *aScaling = gtk_print_settings_get_scale(mPrintSettings) / 100.0;
  return NS_OK;
}

NS_IMETHODIMP
nsPrintSettingsGTK::SetScaling(double aScaling) {
  gtk_print_settings_set_scale(mPrintSettings, aScaling * 100.0);
  return NS_OK;
}

NS_IMETHODIMP
nsPrintSettingsGTK::GetPaperId(nsAString& aPaperId) {
  const gchar* name =
      gtk_paper_size_get_name(gtk_page_setup_get_paper_size(mPageSetup));
  CopyUTF8toUTF16(mozilla::MakeStringSpan(name), aPaperId);
  return NS_OK;
}
NS_IMETHODIMP
nsPrintSettingsGTK::SetPaperId(const nsAString& aPaperId) {
  NS_ConvertUTF16toUTF8 gtkPaperName(aPaperId);

  // Convert these Gecko names to GTK names
  // XXX (jfkthame): is this still relevant?
  if (gtkPaperName.EqualsIgnoreCase("letter"))
    gtkPaperName.AssignLiteral(GTK_PAPER_NAME_LETTER);
  else if (gtkPaperName.EqualsIgnoreCase("legal"))
    gtkPaperName.AssignLiteral(GTK_PAPER_NAME_LEGAL);

  GtkPaperSize* oldPaperSize = gtk_page_setup_get_paper_size(mPageSetup);
  gdouble width = gtk_paper_size_get_width(oldPaperSize, GTK_UNIT_INCH);
  gdouble height = gtk_paper_size_get_height(oldPaperSize, GTK_UNIT_INCH);

  // Try to get the display name from the name so our paper size fits in the
  // Page Setup dialog.
  GtkPaperSize* paperSize = gtk_paper_size_new(gtkPaperName.get());
  GtkPaperSize* customPaperSize = gtk_paper_size_new_custom(
      gtkPaperName.get(), gtk_paper_size_get_display_name(paperSize), width,
      height, GTK_UNIT_INCH);
  gtk_paper_size_free(paperSize);

  gtk_page_setup_set_paper_size(mPageSetup, customPaperSize);
  gtk_paper_size_free(customPaperSize);
  SaveNewPageSize();
  return NS_OK;
}

GtkUnit nsPrintSettingsGTK::GetGTKUnit(int16_t aGeckoUnit) {
  if (aGeckoUnit == kPaperSizeMillimeters)
    return GTK_UNIT_MM;
  else
    return GTK_UNIT_INCH;
}

void nsPrintSettingsGTK::SaveNewPageSize() {
  gtk_print_settings_set_paper_size(mPrintSettings,
                                    gtk_page_setup_get_paper_size(mPageSetup));
}

void nsPrintSettingsGTK::InitUnwriteableMargin() {
  mUnwriteableMargin.SizeTo(
      NS_INCHES_TO_INT_TWIPS(
          gtk_page_setup_get_top_margin(mPageSetup, GTK_UNIT_INCH)),
      NS_INCHES_TO_INT_TWIPS(
          gtk_page_setup_get_right_margin(mPageSetup, GTK_UNIT_INCH)),
      NS_INCHES_TO_INT_TWIPS(
          gtk_page_setup_get_bottom_margin(mPageSetup, GTK_UNIT_INCH)),
      NS_INCHES_TO_INT_TWIPS(
          gtk_page_setup_get_left_margin(mPageSetup, GTK_UNIT_INCH)));
}

/**
 * NOTE: Need a custom set of SetUnwriteableMargin functions, because
 * whenever we change mUnwriteableMargin, we must pass the change
 * down to our GTKPageSetup object.  (This is needed in order for us
 * to give the correct default values in nsPrintDialogGTK.)
 *
 * It's important that the following functions pass
 * mUnwriteableMargin values rather than aUnwriteableMargin values
 * to gtk_page_setup_set_[blank]_margin, because the two may not be
 * the same.  (Specifically, negative values of aUnwriteableMargin
 * are ignored by the nsPrintSettings::SetUnwriteableMargin functions.)
 */
NS_IMETHODIMP
nsPrintSettingsGTK::SetUnwriteableMarginInTwips(
    nsIntMargin& aUnwriteableMargin) {
  nsPrintSettings::SetUnwriteableMarginInTwips(aUnwriteableMargin);
  gtk_page_setup_set_top_margin(
      mPageSetup, NS_TWIPS_TO_INCHES(mUnwriteableMargin.top), GTK_UNIT_INCH);
  gtk_page_setup_set_left_margin(
      mPageSetup, NS_TWIPS_TO_INCHES(mUnwriteableMargin.left), GTK_UNIT_INCH);
  gtk_page_setup_set_bottom_margin(
      mPageSetup, NS_TWIPS_TO_INCHES(mUnwriteableMargin.bottom), GTK_UNIT_INCH);
  gtk_page_setup_set_right_margin(
      mPageSetup, NS_TWIPS_TO_INCHES(mUnwriteableMargin.right), GTK_UNIT_INCH);
  return NS_OK;
}

NS_IMETHODIMP
nsPrintSettingsGTK::SetUnwriteableMarginTop(double aUnwriteableMarginTop) {
  nsPrintSettings::SetUnwriteableMarginTop(aUnwriteableMarginTop);
  gtk_page_setup_set_top_margin(
      mPageSetup, NS_TWIPS_TO_INCHES(mUnwriteableMargin.top), GTK_UNIT_INCH);
  return NS_OK;
}

NS_IMETHODIMP
nsPrintSettingsGTK::SetUnwriteableMarginLeft(double aUnwriteableMarginLeft) {
  nsPrintSettings::SetUnwriteableMarginLeft(aUnwriteableMarginLeft);
  gtk_page_setup_set_left_margin(
      mPageSetup, NS_TWIPS_TO_INCHES(mUnwriteableMargin.left), GTK_UNIT_INCH);
  return NS_OK;
}

NS_IMETHODIMP
nsPrintSettingsGTK::SetUnwriteableMarginBottom(
    double aUnwriteableMarginBottom) {
  nsPrintSettings::SetUnwriteableMarginBottom(aUnwriteableMarginBottom);
  gtk_page_setup_set_bottom_margin(
      mPageSetup, NS_TWIPS_TO_INCHES(mUnwriteableMargin.bottom), GTK_UNIT_INCH);
  return NS_OK;
}

NS_IMETHODIMP
nsPrintSettingsGTK::SetUnwriteableMarginRight(double aUnwriteableMarginRight) {
  nsPrintSettings::SetUnwriteableMarginRight(aUnwriteableMarginRight);
  gtk_page_setup_set_right_margin(
      mPageSetup, NS_TWIPS_TO_INCHES(mUnwriteableMargin.right), GTK_UNIT_INCH);
  return NS_OK;
}

NS_IMETHODIMP
nsPrintSettingsGTK::GetPaperWidth(double* aPaperWidth) {
  NS_ENSURE_ARG_POINTER(aPaperWidth);
  GtkPaperSize* paperSize = gtk_page_setup_get_paper_size(mPageSetup);
  *aPaperWidth =
      gtk_paper_size_get_width(paperSize, GetGTKUnit(mPaperSizeUnit));
  return NS_OK;
}
NS_IMETHODIMP
nsPrintSettingsGTK::SetPaperWidth(double aPaperWidth) {
  GtkPaperSize* paperSize = gtk_page_setup_get_paper_size(mPageSetup);
  gtk_paper_size_set_size(
      paperSize, aPaperWidth,
      gtk_paper_size_get_height(paperSize, GetGTKUnit(mPaperSizeUnit)),
      GetGTKUnit(mPaperSizeUnit));
  SaveNewPageSize();
  return NS_OK;
}

NS_IMETHODIMP
nsPrintSettingsGTK::GetPaperHeight(double* aPaperHeight) {
  NS_ENSURE_ARG_POINTER(aPaperHeight);
  GtkPaperSize* paperSize = gtk_page_setup_get_paper_size(mPageSetup);
  *aPaperHeight =
      gtk_paper_size_get_height(paperSize, GetGTKUnit(mPaperSizeUnit));
  return NS_OK;
}
NS_IMETHODIMP
nsPrintSettingsGTK::SetPaperHeight(double aPaperHeight) {
  GtkPaperSize* paperSize = gtk_page_setup_get_paper_size(mPageSetup);
  gtk_paper_size_set_size(
      paperSize,
      gtk_paper_size_get_width(paperSize, GetGTKUnit(mPaperSizeUnit)),
      aPaperHeight, GetGTKUnit(mPaperSizeUnit));
  SaveNewPageSize();
  return NS_OK;
}

NS_IMETHODIMP
nsPrintSettingsGTK::SetPaperSizeUnit(int16_t aPaperSizeUnit) {
  // Convert units internally. e.g. they might have set the values while we're
  // still in mm but they change to inch just afterwards, expecting that their
  // sizes are in inches.
  GtkPaperSize* paperSize = gtk_page_setup_get_paper_size(mPageSetup);
  gtk_paper_size_set_size(
      paperSize,
      gtk_paper_size_get_width(paperSize, GetGTKUnit(mPaperSizeUnit)),
      gtk_paper_size_get_height(paperSize, GetGTKUnit(mPaperSizeUnit)),
      GetGTKUnit(aPaperSizeUnit));
  SaveNewPageSize();

  mPaperSizeUnit = aPaperSizeUnit;
  return NS_OK;
}

NS_IMETHODIMP
nsPrintSettingsGTK::GetEffectivePageSize(double* aWidth, double* aHeight) {
  GtkPaperSize* paperSize = gtk_page_setup_get_paper_size(mPageSetup);
  if (mPaperSizeUnit == kPaperSizeInches) {
    *aWidth =
        NS_INCHES_TO_TWIPS(gtk_paper_size_get_width(paperSize, GTK_UNIT_INCH));
    *aHeight =
        NS_INCHES_TO_TWIPS(gtk_paper_size_get_height(paperSize, GTK_UNIT_INCH));
  } else {
    MOZ_ASSERT(mPaperSizeUnit == kPaperSizeMillimeters,
               "unexpected paper size unit");
    *aWidth = NS_MILLIMETERS_TO_TWIPS(
        gtk_paper_size_get_width(paperSize, GTK_UNIT_MM));
    *aHeight = NS_MILLIMETERS_TO_TWIPS(
        gtk_paper_size_get_height(paperSize, GTK_UNIT_MM));
  }
  GtkPageOrientation gtkOrient = gtk_page_setup_get_orientation(mPageSetup);

  if (gtkOrient == GTK_PAGE_ORIENTATION_LANDSCAPE ||
      gtkOrient == GTK_PAGE_ORIENTATION_REVERSE_LANDSCAPE) {
    double temp = *aWidth;
    *aWidth = *aHeight;
    *aHeight = temp;
  }
  return NS_OK;
}

NS_IMETHODIMP
nsPrintSettingsGTK::GetPageRanges(nsTArray<int32_t>& aPages) {
  GtkPrintPages gtkRange = gtk_print_settings_get_print_pages(mPrintSettings);
  if (gtkRange != GTK_PRINT_PAGES_RANGES) {
    aPages.Clear();
    return NS_OK;
  }

  gint ctRanges;
  GtkPageRange* lstRanges =
      gtk_print_settings_get_page_ranges(mPrintSettings, &ctRanges);

  aPages.Clear();

  for (gint i = 0; i < ctRanges; i++) {
    aPages.AppendElement(lstRanges[i].start + 1);
    aPages.AppendElement(lstRanges[i].end + 1);
  }

  g_free(lstRanges);
  return NS_OK;
}

NS_IMETHODIMP
nsPrintSettingsGTK::GetResolution(int32_t* aResolution) {
  *aResolution = gtk_print_settings_get_resolution(mPrintSettings);
  return NS_OK;
}

NS_IMETHODIMP
nsPrintSettingsGTK::SetResolution(int32_t aResolution) {
  gtk_print_settings_set_resolution(mPrintSettings, aResolution);
  return NS_OK;
}

NS_IMETHODIMP
nsPrintSettingsGTK::GetDuplex(int32_t* aDuplex) {
  NS_ENSURE_ARG_POINTER(aDuplex);

  // Default to DuplexNone.
  *aDuplex = kDuplexNone;

  if (!gtk_print_settings_has_key(mPrintSettings, GTK_PRINT_SETTINGS_DUPLEX)) {
    return NS_OK;
  }

  switch (gtk_print_settings_get_duplex(mPrintSettings)) {
    case GTK_PRINT_DUPLEX_SIMPLEX:
      *aDuplex = kDuplexNone;
      break;
    case GTK_PRINT_DUPLEX_HORIZONTAL:
      *aDuplex = kDuplexFlipOnLongEdge;
      break;
    case GTK_PRINT_DUPLEX_VERTICAL:
      *aDuplex = kDuplexFlipOnShortEdge;
      break;
  }

  return NS_OK;
}

NS_IMETHODIMP
nsPrintSettingsGTK::SetDuplex(int32_t aDuplex) {
  uint32_t duplex = static_cast<uint32_t>(aDuplex);
  MOZ_ASSERT(duplex <= kDuplexFlipOnShortEdge,
             "value is out of bounds for duplex enum");

  // We want to set the GTK CUPS Duplex setting in addition to calling
  // gtk_print_settings_set_duplex(). Some systems may look for one, or the
  // other, so it is best to set them both consistently.
  switch (duplex) {
    case kDuplexNone:
      gtk_print_settings_set(mPrintSettings, kCupsDuplex, kCupsDuplexNone);
      gtk_print_settings_set_duplex(mPrintSettings, GTK_PRINT_DUPLEX_SIMPLEX);
      break;
    case kDuplexFlipOnLongEdge:
      gtk_print_settings_set(mPrintSettings, kCupsDuplex, kCupsDuplexNoTumble);
      gtk_print_settings_set_duplex(mPrintSettings,
                                    GTK_PRINT_DUPLEX_HORIZONTAL);
      break;
    case kDuplexFlipOnShortEdge:
      gtk_print_settings_set(mPrintSettings, kCupsDuplex, kCupsDuplexTumble);
      gtk_print_settings_set_duplex(mPrintSettings, GTK_PRINT_DUPLEX_VERTICAL);
      break;
  }

  return NS_OK;
}
