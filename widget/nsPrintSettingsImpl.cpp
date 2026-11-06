/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsPrintSettingsImpl.h"

#include "mozilla/DebugOnly.h"
#include "mozilla/RefPtr.h"
#include "nsCoord.h"
#include "nsPaper.h"
#include "nsReadableUtils.h"
#include "prenv.h"

using namespace mozilla;

#define DEFAULT_MARGIN_WIDTH 0.5

NS_IMPL_ISUPPORTS(nsPrintSettings, nsIPrintSettings)

nsPrintSettings::nsPrintSettings() {
  /* member initializers and constructor code */
  int32_t marginWidth = NS_INCHES_TO_INT_TWIPS(DEFAULT_MARGIN_WIDTH);
  mMargin.SizeTo(marginWidth, marginWidth, marginWidth, marginWidth);
  mEdge.SizeTo(0, 0, 0, 0);
  mUnwriteableMargin.SizeTo(0, 0, 0, 0);

  mHeaderStrs[0].AssignLiteral("&T");
  mHeaderStrs[2].AssignLiteral("&U");

  mFooterStrs[0].AssignLiteral(
      "&PT");  // Use &P (Page Num Only) or &PT (Page Num of Page Total)
  mFooterStrs[2].AssignLiteral("&D");
}

void nsPrintSettings::InitWithInitializer(
    const PrintSettingsInitializer& aSettings) {
  const double kInchesPerPoint = 1.0 / 72.0;

  SetPrinterName(aSettings.mPrinter);
  SetPrintInColor(aSettings.mPrintInColor);
  SetResolution(aSettings.mResolution);
  SetNumCopies(aSettings.mNumCopies);
  SetDuplex(aSettings.mDuplex);
  // The paper ID used by nsPrintSettings is the non-localizable identifier
  // exposed as "id" by the paper, not the potentially localized human-friendly
  // "name", which could change, e.g. if the user changes their system locale.
  SetPaperId(aSettings.mPaperInfo.mId);

  // Set the paper sizes to match the unit.
  SetPaperSizeUnit(aSettings.mPaperSizeUnit);
  double sizeUnitsPerPoint =
      aSettings.mPaperSizeUnit == kPaperSizeInches ? 1.0 / 72.0 : 25.4 / 72.0;
  SetPaperWidth(aSettings.mPaperInfo.mSize.width * sizeUnitsPerPoint);
  SetPaperHeight(aSettings.mPaperInfo.mSize.height * sizeUnitsPerPoint);

  // If our initializer says that we're producing portrait-mode sheets of
  // paper, then our page format must also be portrait-mode; unless we've got
  // a pages-per-sheet value with orthogonal pages/sheets, in which case it's
  // reversed.
  const bool areSheetsOfPaperPortraitMode =
      (aSettings.mSheetOrientation == kPortraitOrientation);
  const bool arePagesPortraitMode =
      (areSheetsOfPaperPortraitMode != HasOrthogonalPagesPerSheet());
  SetOrientation(arePagesPortraitMode ? kPortraitOrientation
                                      : kLandscapeOrientation);

  if (aSettings.mPaperInfo.mUnwriteableMargin) {
    const auto& margin = aSettings.mPaperInfo.mUnwriteableMargin.value();
    // Margins are stored internally in TWIPS, but the setters expect inches.
    SetUnwriteableMarginTop(margin.top * kInchesPerPoint);
    SetUnwriteableMarginRight(margin.right * kInchesPerPoint);
    SetUnwriteableMarginBottom(margin.bottom * kInchesPerPoint);
    SetUnwriteableMarginLeft(margin.left * kInchesPerPoint);
  }

  // Set this last because other setters may overwrite its value.
  SetIsInitializedFromPrinter(true);
}

nsPrintSettings::nsPrintSettings(const nsPrintSettings& aPS) { *this = aPS; }

nsPrintSettings::~nsPrintSettings() = default;

NS_IMETHODIMP nsPrintSettings::GetPrintInColor(bool* aPrintInColor) {
  *aPrintInColor = mPrintInColor;
  return NS_OK;
}
NS_IMETHODIMP nsPrintSettings::SetPrintInColor(bool aPrintInColor) {
  mPrintInColor = aPrintInColor;
  return NS_OK;
}

NS_IMETHODIMP nsPrintSettings::GetOrientation(int32_t* aOrientation) {
  *aOrientation = mOrientation;
  return NS_OK;
}
NS_IMETHODIMP nsPrintSettings::SetOrientation(int32_t aOrientation) {
  mOrientation = aOrientation;
  return NS_OK;
}

NS_IMETHODIMP nsPrintSettings::GetResolution(int32_t* aResolution) {
  NS_ENSURE_ARG_POINTER(aResolution);
  *aResolution = mResolution;
  return NS_OK;
}
NS_IMETHODIMP nsPrintSettings::SetResolution(const int32_t aResolution) {
  mResolution = aResolution;
  return NS_OK;
}

NS_IMETHODIMP nsPrintSettings::GetDuplex(int32_t* aDuplex) {
  NS_ENSURE_ARG_POINTER(aDuplex);
  *aDuplex = mDuplex;
  return NS_OK;
}
NS_IMETHODIMP nsPrintSettings::SetDuplex(const int32_t aDuplex) {
  mDuplex = aDuplex;
  return NS_OK;
}

NS_IMETHODIMP nsPrintSettings::GetPrinterName(nsAString& aPrinter) {
  aPrinter = mPrinter;
  return NS_OK;
}

NS_IMETHODIMP nsPrintSettings::SetPrinterName(const nsAString& aPrinter) {
  if (!mPrinter.Equals(aPrinter)) {
    mIsInitedFromPrinter = false;
    mIsInitedFromPrefs = false;
  }

  mPrinter.Assign(aPrinter);
  return NS_OK;
}

NS_IMETHODIMP nsPrintSettings::GetNumCopies(int32_t* aNumCopies) {
  NS_ENSURE_ARG_POINTER(aNumCopies);
  *aNumCopies = mNumCopies;
  return NS_OK;
}
NS_IMETHODIMP nsPrintSettings::SetNumCopies(int32_t aNumCopies) {
  mNumCopies = aNumCopies;
  return NS_OK;
}

NS_IMETHODIMP nsPrintSettings::GetNumPagesPerSheet(int32_t* aNumPagesPerSheet) {
  NS_ENSURE_ARG_POINTER(aNumPagesPerSheet);
  *aNumPagesPerSheet = mNumPagesPerSheet;
  return NS_OK;
}
NS_IMETHODIMP nsPrintSettings::SetNumPagesPerSheet(int32_t aNumPagesPerSheet) {
  mNumPagesPerSheet = aNumPagesPerSheet;
  return NS_OK;
}

NS_IMETHODIMP nsPrintSettings::GetOutputDestination(
    OutputDestinationType* aDestination) {
  *aDestination = mOutputDestination;
  return NS_OK;
}

NS_IMETHODIMP nsPrintSettings::SetOutputDestination(
    OutputDestinationType aDestination) {
  mOutputDestination = aDestination;
  return NS_OK;
}

NS_IMETHODIMP nsPrintSettings::SetOutputStream(nsIOutputStream* aStream) {
  mOutputStream = aStream;
  return NS_OK;
}

NS_IMETHODIMP nsPrintSettings::GetOutputStream(nsIOutputStream** aStream) {
  NS_IF_ADDREF(*aStream = mOutputStream.get());
  return NS_OK;
}

NS_IMETHODIMP nsPrintSettings::GetToFileName(nsAString& aToFileName) {
  aToFileName = mToFileName;
  return NS_OK;
}
NS_IMETHODIMP nsPrintSettings::SetToFileName(const nsAString& aToFileName) {
  mToFileName = aToFileName;
  return NS_OK;
}

NS_IMETHODIMP nsPrintSettings::GetOutputFormat(int16_t* aOutputFormat) {
  NS_ENSURE_ARG_POINTER(aOutputFormat);
  *aOutputFormat = mOutputFormat;
  return NS_OK;
}
NS_IMETHODIMP nsPrintSettings::SetOutputFormat(int16_t aOutputFormat) {
  mOutputFormat = aOutputFormat;
  return NS_OK;
}

NS_IMETHODIMP nsPrintSettings::GetPrintPageDelay(int32_t* aPrintPageDelay) {
  *aPrintPageDelay = mPrintPageDelay;
  return NS_OK;
}
NS_IMETHODIMP nsPrintSettings::SetPrintPageDelay(int32_t aPrintPageDelay) {
  mPrintPageDelay = aPrintPageDelay;
  return NS_OK;
}

NS_IMETHODIMP nsPrintSettings::GetIsInitializedFromPrinter(
    bool* aIsInitializedFromPrinter) {
  NS_ENSURE_ARG_POINTER(aIsInitializedFromPrinter);
  *aIsInitializedFromPrinter = mIsInitedFromPrinter;
  return NS_OK;
}
NS_IMETHODIMP nsPrintSettings::SetIsInitializedFromPrinter(
    bool aIsInitializedFromPrinter) {
  mIsInitedFromPrinter = aIsInitializedFromPrinter;
  return NS_OK;
}

NS_IMETHODIMP nsPrintSettings::GetIsInitializedFromPrefs(
    bool* aInitializedFromPrefs) {
  NS_ENSURE_ARG_POINTER(aInitializedFromPrefs);
  *aInitializedFromPrefs = mIsInitedFromPrefs;
  return NS_OK;
}
NS_IMETHODIMP nsPrintSettings::SetIsInitializedFromPrefs(
    bool aInitializedFromPrefs) {
  mIsInitedFromPrefs = aInitializedFromPrefs;
  return NS_OK;
}

NS_IMETHODIMP nsPrintSettings::GetMarginTop(double* aMarginTop) {
  NS_ENSURE_ARG_POINTER(aMarginTop);
  *aMarginTop = NS_TWIPS_TO_INCHES(mMargin.top);
  return NS_OK;
}
NS_IMETHODIMP nsPrintSettings::SetMarginTop(double aMarginTop) {
  mMargin.top = NS_INCHES_TO_INT_TWIPS(float(aMarginTop));
  return NS_OK;
}

NS_IMETHODIMP nsPrintSettings::GetMarginLeft(double* aMarginLeft) {
  NS_ENSURE_ARG_POINTER(aMarginLeft);
  *aMarginLeft = NS_TWIPS_TO_INCHES(mMargin.left);
  return NS_OK;
}
NS_IMETHODIMP nsPrintSettings::SetMarginLeft(double aMarginLeft) {
  mMargin.left = NS_INCHES_TO_INT_TWIPS(float(aMarginLeft));
  return NS_OK;
}

NS_IMETHODIMP nsPrintSettings::GetMarginBottom(double* aMarginBottom) {
  NS_ENSURE_ARG_POINTER(aMarginBottom);
  *aMarginBottom = NS_TWIPS_TO_INCHES(mMargin.bottom);
  return NS_OK;
}
NS_IMETHODIMP nsPrintSettings::SetMarginBottom(double aMarginBottom) {
  mMargin.bottom = NS_INCHES_TO_INT_TWIPS(float(aMarginBottom));
  return NS_OK;
}

NS_IMETHODIMP nsPrintSettings::GetMarginRight(double* aMarginRight) {
  NS_ENSURE_ARG_POINTER(aMarginRight);
  *aMarginRight = NS_TWIPS_TO_INCHES(mMargin.right);
  return NS_OK;
}
NS_IMETHODIMP nsPrintSettings::SetMarginRight(double aMarginRight) {
  mMargin.right = NS_INCHES_TO_INT_TWIPS(float(aMarginRight));
  return NS_OK;
}

NS_IMETHODIMP nsPrintSettings::GetEdgeTop(double* aEdgeTop) {
  NS_ENSURE_ARG_POINTER(aEdgeTop);
  *aEdgeTop = NS_TWIPS_TO_INCHES(mEdge.top);
  return NS_OK;
}
NS_IMETHODIMP nsPrintSettings::SetEdgeTop(double aEdgeTop) {
  mEdge.top = NS_INCHES_TO_INT_TWIPS(float(aEdgeTop));
  return NS_OK;
}

NS_IMETHODIMP nsPrintSettings::GetEdgeLeft(double* aEdgeLeft) {
  NS_ENSURE_ARG_POINTER(aEdgeLeft);
  *aEdgeLeft = NS_TWIPS_TO_INCHES(mEdge.left);
  return NS_OK;
}
NS_IMETHODIMP nsPrintSettings::SetEdgeLeft(double aEdgeLeft) {
  mEdge.left = NS_INCHES_TO_INT_TWIPS(float(aEdgeLeft));
  return NS_OK;
}

NS_IMETHODIMP nsPrintSettings::GetEdgeBottom(double* aEdgeBottom) {
  NS_ENSURE_ARG_POINTER(aEdgeBottom);
  *aEdgeBottom = NS_TWIPS_TO_INCHES(mEdge.bottom);
  return NS_OK;
}
NS_IMETHODIMP nsPrintSettings::SetEdgeBottom(double aEdgeBottom) {
  mEdge.bottom = NS_INCHES_TO_INT_TWIPS(float(aEdgeBottom));
  return NS_OK;
}

NS_IMETHODIMP nsPrintSettings::GetEdgeRight(double* aEdgeRight) {
  NS_ENSURE_ARG_POINTER(aEdgeRight);
  *aEdgeRight = NS_TWIPS_TO_INCHES(mEdge.right);
  return NS_OK;
}
NS_IMETHODIMP nsPrintSettings::SetEdgeRight(double aEdgeRight) {
  mEdge.right = NS_INCHES_TO_INT_TWIPS(float(aEdgeRight));
  return NS_OK;
}

NS_IMETHODIMP nsPrintSettings::GetUnwriteableMarginTop(
    double* aUnwriteableMarginTop) {
  NS_ENSURE_ARG_POINTER(aUnwriteableMarginTop);
  *aUnwriteableMarginTop = NS_TWIPS_TO_INCHES(mUnwriteableMargin.top);
  return NS_OK;
}
NS_IMETHODIMP nsPrintSettings::SetUnwriteableMarginTop(
    double aUnwriteableMarginTop) {
  if (aUnwriteableMarginTop >= 0.0) {
    mUnwriteableMargin.top = NS_INCHES_TO_INT_TWIPS(aUnwriteableMarginTop);
  }
  return NS_OK;
}

NS_IMETHODIMP nsPrintSettings::GetUnwriteableMarginLeft(
    double* aUnwriteableMarginLeft) {
  NS_ENSURE_ARG_POINTER(aUnwriteableMarginLeft);
  *aUnwriteableMarginLeft = NS_TWIPS_TO_INCHES(mUnwriteableMargin.left);
  return NS_OK;
}
NS_IMETHODIMP nsPrintSettings::SetUnwriteableMarginLeft(
    double aUnwriteableMarginLeft) {
  if (aUnwriteableMarginLeft >= 0.0) {
    mUnwriteableMargin.left = NS_INCHES_TO_INT_TWIPS(aUnwriteableMarginLeft);
  }
  return NS_OK;
}

NS_IMETHODIMP nsPrintSettings::GetUnwriteableMarginBottom(
    double* aUnwriteableMarginBottom) {
  NS_ENSURE_ARG_POINTER(aUnwriteableMarginBottom);
  *aUnwriteableMarginBottom = NS_TWIPS_TO_INCHES(mUnwriteableMargin.bottom);
  return NS_OK;
}
NS_IMETHODIMP nsPrintSettings::SetUnwriteableMarginBottom(
    double aUnwriteableMarginBottom) {
  if (aUnwriteableMarginBottom >= 0.0) {
    mUnwriteableMargin.bottom =
        NS_INCHES_TO_INT_TWIPS(aUnwriteableMarginBottom);
  }
  return NS_OK;
}

NS_IMETHODIMP nsPrintSettings::GetUnwriteableMarginRight(
    double* aUnwriteableMarginRight) {
  NS_ENSURE_ARG_POINTER(aUnwriteableMarginRight);
  *aUnwriteableMarginRight = NS_TWIPS_TO_INCHES(mUnwriteableMargin.right);
  return NS_OK;
}
NS_IMETHODIMP nsPrintSettings::SetUnwriteableMarginRight(
    double aUnwriteableMarginRight) {
  if (aUnwriteableMarginRight >= 0.0) {
    mUnwriteableMargin.right = NS_INCHES_TO_INT_TWIPS(aUnwriteableMarginRight);
  }
  return NS_OK;
}

NS_IMETHODIMP nsPrintSettings::GetScaling(double* aScaling) {
  NS_ENSURE_ARG_POINTER(aScaling);
  *aScaling = mScaling;
  return NS_OK;
}

NS_IMETHODIMP nsPrintSettings::SetScaling(double aScaling) {
  mScaling = aScaling;
  return NS_OK;
}

NS_IMETHODIMP nsPrintSettings::GetPrintBGColors(bool* aPrintBGColors) {
  NS_ENSURE_ARG_POINTER(aPrintBGColors);
  *aPrintBGColors = mPrintBGColors;
  return NS_OK;
}
NS_IMETHODIMP nsPrintSettings::SetPrintBGColors(bool aPrintBGColors) {
  mPrintBGColors = aPrintBGColors;
  return NS_OK;
}

NS_IMETHODIMP nsPrintSettings::GetPrintBGImages(bool* aPrintBGImages) {
  NS_ENSURE_ARG_POINTER(aPrintBGImages);
  *aPrintBGImages = mPrintBGImages;
  return NS_OK;
}
NS_IMETHODIMP nsPrintSettings::SetPrintBGImages(bool aPrintBGImages) {
  mPrintBGImages = aPrintBGImages;
  return NS_OK;
}

NS_IMETHODIMP nsPrintSettings::GetTitle(nsAString& aTitle) {
  aTitle = mTitle;
  return NS_OK;
}
NS_IMETHODIMP nsPrintSettings::SetTitle(const nsAString& aTitle) {
  mTitle = aTitle;
  return NS_OK;
}

NS_IMETHODIMP nsPrintSettings::GetDocURL(nsAString& aDocURL) {
  aDocURL = mURL;
  return NS_OK;
}
NS_IMETHODIMP nsPrintSettings::SetDocURL(const nsAString& aDocURL) {
  mURL = aDocURL;
  return NS_OK;
}

NS_IMETHODIMP nsPrintSettings::GetHeaderStrLeft(nsAString& aTitle) {
  aTitle = mHeaderStrs[0];
  return NS_OK;
}
NS_IMETHODIMP nsPrintSettings::SetHeaderStrLeft(const nsAString& aTitle) {
  mHeaderStrs[0] = aTitle;
  return NS_OK;
}

NS_IMETHODIMP nsPrintSettings::GetHeaderStrCenter(nsAString& aTitle) {
  aTitle = mHeaderStrs[1];
  return NS_OK;
}
NS_IMETHODIMP nsPrintSettings::SetHeaderStrCenter(const nsAString& aTitle) {
  mHeaderStrs[1] = aTitle;
  return NS_OK;
}

NS_IMETHODIMP nsPrintSettings::GetHeaderStrRight(nsAString& aTitle) {
  aTitle = mHeaderStrs[2];
  return NS_OK;
}
NS_IMETHODIMP nsPrintSettings::SetHeaderStrRight(const nsAString& aTitle) {
  mHeaderStrs[2] = aTitle;
  return NS_OK;
}

NS_IMETHODIMP nsPrintSettings::GetFooterStrLeft(nsAString& aTitle) {
  aTitle = mFooterStrs[0];
  return NS_OK;
}
NS_IMETHODIMP nsPrintSettings::SetFooterStrLeft(const nsAString& aTitle) {
  mFooterStrs[0] = aTitle;
  return NS_OK;
}

NS_IMETHODIMP nsPrintSettings::GetFooterStrCenter(nsAString& aTitle) {
  aTitle = mFooterStrs[1];
  return NS_OK;
}
NS_IMETHODIMP nsPrintSettings::SetFooterStrCenter(const nsAString& aTitle) {
  mFooterStrs[1] = aTitle;
  return NS_OK;
}

NS_IMETHODIMP nsPrintSettings::GetFooterStrRight(nsAString& aTitle) {
  aTitle = mFooterStrs[2];
  return NS_OK;
}
NS_IMETHODIMP nsPrintSettings::SetFooterStrRight(const nsAString& aTitle) {
  mFooterStrs[2] = aTitle;
  return NS_OK;
}

NS_IMETHODIMP nsPrintSettings::GetPrintSilent(bool* aPrintSilent) {
  NS_ENSURE_ARG_POINTER(aPrintSilent);
  *aPrintSilent = mPrintSilent;
  return NS_OK;
}
NS_IMETHODIMP nsPrintSettings::SetPrintSilent(bool aPrintSilent) {
  mPrintSilent = aPrintSilent;
  return NS_OK;
}

NS_IMETHODIMP nsPrintSettings::GetShrinkToFit(bool* aShrinkToFit) {
  NS_ENSURE_ARG_POINTER(aShrinkToFit);
  *aShrinkToFit = mShrinkToFit;
  return NS_OK;
}
NS_IMETHODIMP nsPrintSettings::SetShrinkToFit(bool aShrinkToFit) {
  mShrinkToFit = aShrinkToFit;
  return NS_OK;
}

NS_IMETHODIMP nsPrintSettings::GetShowMarginGuides(bool* aShowMarginGuides) {
  *aShowMarginGuides = mShowMarginGuides;
  return NS_OK;
}

NS_IMETHODIMP nsPrintSettings::SetShowMarginGuides(bool aShowMarginGuides) {
  mShowMarginGuides = aShowMarginGuides;
  return NS_OK;
}

NS_IMETHODIMP nsPrintSettings::GetHonorPageRuleMargins(bool* aResult) {
  *aResult = mHonorPageRuleMargins;
  return NS_OK;
}

NS_IMETHODIMP nsPrintSettings::SetHonorPageRuleMargins(bool aHonor) {
  mHonorPageRuleMargins = aHonor;
  return NS_OK;
}

NS_IMETHODIMP nsPrintSettings::GetUsePageRuleSizeAsPaperSize(bool* aResult) {
  *aResult = mUsePageRuleSizeAsPaperSize;
  return NS_OK;
}

NS_IMETHODIMP nsPrintSettings::SetUsePageRuleSizeAsPaperSize(bool aHonor) {
  mUsePageRuleSizeAsPaperSize = aHonor;
  return NS_OK;
}

NS_IMETHODIMP nsPrintSettings::GetIgnoreUnwriteableMargins(bool* aResult) {
  *aResult = mIgnoreUnwriteableMargins;
  return NS_OK;
}

NS_IMETHODIMP nsPrintSettings::SetIgnoreUnwriteableMargins(bool aIgnore) {
  mIgnoreUnwriteableMargins = aIgnore;
  return NS_OK;
}

NS_IMETHODIMP nsPrintSettings::GetPrintSelectionOnly(bool* aResult) {
  *aResult = mPrintSelectionOnly;
  return NS_OK;
}

NS_IMETHODIMP nsPrintSettings::SetPrintSelectionOnly(bool aSelectionOnly) {
  mPrintSelectionOnly = aSelectionOnly;
  return NS_OK;
}

NS_IMETHODIMP nsPrintSettings::GetPaperId(nsAString& aPaperId) {
  aPaperId = mPaperId;
  return NS_OK;
}
NS_IMETHODIMP nsPrintSettings::SetPaperId(const nsAString& aPaperId) {
  mPaperId = aPaperId;
  return NS_OK;
}

NS_IMETHODIMP nsPrintSettings::GetPaperWidth(double* aPaperWidth) {
  NS_ENSURE_ARG_POINTER(aPaperWidth);
  *aPaperWidth = mPaperWidth;
  return NS_OK;
}
NS_IMETHODIMP nsPrintSettings::SetPaperWidth(double aPaperWidth) {
  mPaperWidth = aPaperWidth;
  return NS_OK;
}

NS_IMETHODIMP nsPrintSettings::GetPaperHeight(double* aPaperHeight) {
  NS_ENSURE_ARG_POINTER(aPaperHeight);
  *aPaperHeight = mPaperHeight;
  return NS_OK;
}
NS_IMETHODIMP nsPrintSettings::SetPaperHeight(double aPaperHeight) {
  mPaperHeight = aPaperHeight;
  return NS_OK;
}

NS_IMETHODIMP nsPrintSettings::GetPaperSizeUnit(int16_t* aPaperSizeUnit) {
  NS_ENSURE_ARG_POINTER(aPaperSizeUnit);
  *aPaperSizeUnit = mPaperSizeUnit;
  return NS_OK;
}
NS_IMETHODIMP nsPrintSettings::SetPaperSizeUnit(int16_t aPaperSizeUnit) {
  mPaperSizeUnit = aPaperSizeUnit;
  return NS_OK;
}

/** ---------------------------------------------------
 *  See documentation in nsPrintSettingsService.h
 *	@update 6/21/00 dwc
 *	@update 1/12/01 rods
 */
NS_IMETHODIMP
nsPrintSettings::SetMarginInTwips(nsIntMargin& aMargin) {
  mMargin = aMargin;
  return NS_OK;
}

NS_IMETHODIMP
nsPrintSettings::SetEdgeInTwips(nsIntMargin& aEdge) {
  mEdge = aEdge;
  return NS_OK;
}

// NOTE: Any subclass implementation of this function should make sure
// to check for negative margin values in aUnwriteableMargin (which
// would indicate that we should use the system default unwriteable margin.)
NS_IMETHODIMP
nsPrintSettings::SetUnwriteableMarginInTwips(nsIntMargin& aUnwriteableMargin) {
  if (aUnwriteableMargin.top >= 0) {
    mUnwriteableMargin.top = aUnwriteableMargin.top;
  }
  if (aUnwriteableMargin.left >= 0) {
    mUnwriteableMargin.left = aUnwriteableMargin.left;
  }
  if (aUnwriteableMargin.bottom >= 0) {
    mUnwriteableMargin.bottom = aUnwriteableMargin.bottom;
  }
  if (aUnwriteableMargin.right >= 0) {
    mUnwriteableMargin.right = aUnwriteableMargin.right;
  }
  return NS_OK;
}

nsIntMargin nsPrintSettings::GetMarginInTwips() { return mMargin; }

nsIntMargin nsPrintSettings::GetEdgeInTwips() { return mEdge; }

nsIntMargin nsPrintSettings::GetUnwriteableMarginInTwips() {
  return mUnwriteableMargin;
}

/** ---------------------------------------------------
 *  See documentation in nsPrintSettingsService.h
 */
NS_IMETHODIMP
nsPrintSettings::GetEffectivePageSize(double* aWidth, double* aHeight) {
  if (mPaperSizeUnit == kPaperSizeInches) {
    *aWidth = NS_INCHES_TO_TWIPS(float(mPaperWidth));
    *aHeight = NS_INCHES_TO_TWIPS(float(mPaperHeight));
  } else {
    MOZ_ASSERT(mPaperSizeUnit == kPaperSizeMillimeters,
               "unexpected paper size unit");
    *aWidth = NS_MILLIMETERS_TO_TWIPS(float(mPaperWidth));
    *aHeight = NS_MILLIMETERS_TO_TWIPS(float(mPaperHeight));
  }
  if (kLandscapeOrientation == mOrientation) {
    double temp = *aWidth;
    *aWidth = *aHeight;
    *aHeight = temp;
  }
  return NS_OK;
}

bool nsPrintSettings::HasOrthogonalPagesPerSheet() {
  return mNumPagesPerSheet == 2 || mNumPagesPerSheet == 6;
}

void nsPrintSettings::GetEffectiveSheetSize(double* aWidth, double* aHeight) {
  mozilla::DebugOnly<nsresult> rv = GetEffectivePageSize(aWidth, aHeight);

  // Our GetEffectivePageSize impls only return NS_OK, so this should hold:
  MOZ_ASSERT(NS_SUCCEEDED(rv), "Uh oh, GetEffectivePageSize failed");

  if (HasOrthogonalPagesPerSheet()) {
    std::swap(*aWidth, *aHeight);
  }
}

int32_t nsPrintSettings::GetSheetOrientation() {
  if (HasOrthogonalPagesPerSheet()) {
    // Sheet orientation is rotated with respect to the page orientation.
    return kLandscapeOrientation == mOrientation ? kPortraitOrientation
                                                 : kLandscapeOrientation;
  }

  // Sheet orientation is the same as the page orientation.
  return mOrientation;
}

NS_IMETHODIMP
nsPrintSettings::SetPageRanges(const nsTArray<int32_t>& aPages) {
  // Needs to be a set of (start, end) pairs.
  if (aPages.Length() % 2 != 0) {
    return NS_ERROR_FAILURE;
  }
  mPageRanges = aPages.Clone();
  return NS_OK;
}

NS_IMETHODIMP
nsPrintSettings::GetPageRanges(nsTArray<int32_t>& aPages) {
  aPages = mPageRanges.Clone();
  return NS_OK;
}

mozilla::gfx::IntSize nsIPrintSettings::GetEffectiveSheetSizeInPoints() {
  double width, height;
  GetEffectiveSheetSize(&width, &height);
  return mozilla::gfx::IntSize::Ceil(width / TWIPS_PER_POINT_FLOAT,
                                     height / TWIPS_PER_POINT_FLOAT);
}

bool nsIPrintSettings::IsPageSkipped(int32_t aPageNum,
                                     const nsTArray<int32_t>& aRanges) {
  MOZ_RELEASE_ASSERT(aRanges.Length() % 2 == 0);
  if (aRanges.IsEmpty()) {
    return false;
  }
  for (size_t i = 0; i < aRanges.Length(); i += 2) {
    if (aRanges[i] <= aPageNum && aPageNum <= aRanges[i + 1]) {
      // The page is included in this piece of the custom range,
      // so it's not skipped.
      return false;
    }
  }
  return true;
}

nsresult nsPrintSettings::_Clone(nsIPrintSettings** _retval) {
  auto printSettings = MakeRefPtr<nsPrintSettings>(*this);
  printSettings.forget(_retval);
  return NS_OK;
}

NS_IMETHODIMP
nsPrintSettings::Clone(nsIPrintSettings** _retval) {
  NS_ENSURE_ARG_POINTER(_retval);
  return _Clone(_retval);
}

nsresult nsPrintSettings::_Assign(nsIPrintSettings* aPS) {
  nsPrintSettings* ps = static_cast<nsPrintSettings*>(aPS);
  *this = *ps;
  return NS_OK;
}

nsresult nsPrintSettings::EquivalentTo(nsIPrintSettings* aPrintSettings,
                                       bool* _retval) {
  MOZ_ASSERT(aPrintSettings);
  *_retval = false;
  auto* other = static_cast<nsPrintSettings*>(aPrintSettings);
  if (GetMarginInTwips() != aPrintSettings->GetMarginInTwips()) {
    return NS_OK;
  }
  if (GetEdgeInTwips() != aPrintSettings->GetEdgeInTwips()) {
    return NS_OK;
  }
  if (GetUnwriteableMarginInTwips() !=
      aPrintSettings->GetUnwriteableMarginInTwips()) {
    return NS_OK;
  }
  nsTArray<int32_t> ourPageRanges, otherPageRanges;
  if (NS_FAILED(GetPageRanges(ourPageRanges)) ||
      NS_FAILED(aPrintSettings->GetPageRanges(otherPageRanges)) ||
      ourPageRanges != otherPageRanges) {
    return NS_OK;
  }
  double ourScaling, otherScaling;
  if (NS_FAILED(GetScaling(&ourScaling)) ||
      NS_FAILED(aPrintSettings->GetScaling(&otherScaling)) ||
      ourScaling != otherScaling) {
    return NS_OK;
  }
  if (GetPrintBGColors() != aPrintSettings->GetPrintBGColors()) {
    return NS_OK;
  }
  if (GetPrintBGImages() != aPrintSettings->GetPrintBGImages()) {
    return NS_OK;
  }
  if (GetPrintSelectionOnly() != aPrintSettings->GetPrintSelectionOnly()) {
    return NS_OK;
  }
  if (GetShrinkToFit() != aPrintSettings->GetShrinkToFit()) {
    return NS_OK;
  }
  if (GetShowMarginGuides() != aPrintSettings->GetShowMarginGuides()) {
    return NS_OK;
  }
  if (GetHonorPageRuleMargins() != aPrintSettings->GetHonorPageRuleMargins()) {
    return NS_OK;
  }
  if (GetUsePageRuleSizeAsPaperSize() !=
      aPrintSettings->GetUsePageRuleSizeAsPaperSize()) {
    return NS_OK;
  }
  if (GetIgnoreUnwriteableMargins() !=
      aPrintSettings->GetIgnoreUnwriteableMargins()) {
    return NS_OK;
  }
  nsAutoString ourTitle, otherTitle;
  if (NS_FAILED(GetTitle(ourTitle)) ||
      NS_FAILED(aPrintSettings->GetTitle(otherTitle)) ||
      ourTitle != otherTitle) {
    return NS_OK;
  }
  nsAutoString ourUrl, otherUrl;
  if (NS_FAILED(GetDocURL(ourUrl)) ||
      NS_FAILED(aPrintSettings->GetDocURL(otherUrl)) || ourUrl != otherUrl) {
    return NS_OK;
  }
  if (!mozilla::ArrayEqual(mHeaderStrs, other->mHeaderStrs) ||
      !mozilla::ArrayEqual(mFooterStrs, other->mFooterStrs)) {
    return NS_OK;
  }
  nsAutoString ourPaperId, otherPaperId;
  if (NS_FAILED(GetPaperId(ourPaperId)) ||
      NS_FAILED(aPrintSettings->GetPaperId(otherPaperId)) ||
      ourPaperId != otherPaperId) {
    return NS_OK;
  }
  double ourWidth, ourHeight, otherWidth, otherHeight;
  if (NS_FAILED(GetEffectivePageSize(&ourWidth, &ourHeight)) ||
      NS_FAILED(other->GetEffectivePageSize(&otherWidth, &otherHeight)) ||
      std::abs(ourWidth - otherWidth) >= 1 ||
      std::abs(ourHeight - otherHeight) >= 1) {
    return NS_OK;
  }
  int32_t ourOrientation, otherOrientation;
  if (NS_FAILED(GetOrientation(&ourOrientation)) ||
      NS_FAILED(aPrintSettings->GetOrientation(&otherOrientation)) ||
      ourOrientation != otherOrientation) {
    return NS_OK;
  }
  int32_t ourResolution, otherResolution;
  if (NS_FAILED(GetResolution(&ourResolution)) ||
      NS_FAILED(aPrintSettings->GetResolution(&otherResolution)) ||
      ourResolution != otherResolution) {
    return NS_OK;
  }
  int32_t ourNumPagesPerSheet, otherNumPagesPerSheet;
  if (NS_FAILED(GetNumPagesPerSheet(&ourNumPagesPerSheet)) ||
      NS_FAILED(aPrintSettings->GetNumPagesPerSheet(&otherNumPagesPerSheet)) ||
      ourNumPagesPerSheet != otherNumPagesPerSheet) {
    return NS_OK;
  }

  *_retval = true;
  return NS_OK;
}

mozilla::PrintSettingsInitializer nsPrintSettings::GetSettingsInitializer() {
  mozilla::PrintSettingsInitializer settingsInitializer;
  settingsInitializer.mPrinter.Assign(mPrinter);
  settingsInitializer.mPaperInfo.mId = mPaperId;

  double pointsPerSizeUnit =
      mPaperSizeUnit == kPaperSizeInches ? 72.0 : 72.0 / 25.4;
  settingsInitializer.mPaperInfo.mSize = {mPaperWidth * pointsPerSizeUnit,
                                          mPaperHeight * pointsPerSizeUnit};

  // Unwritable margins are stored in TWIPS here and points in PaperInfo.
  settingsInitializer.mPaperInfo.mUnwriteableMargin =
      Some(mozilla::gfx::MarginDouble(
          mUnwriteableMargin.top / 20.0, mUnwriteableMargin.right / 20.0,
          mUnwriteableMargin.bottom / 20.0, mUnwriteableMargin.left / 20.0));

  settingsInitializer.mPrintInColor = mPrintInColor;
  settingsInitializer.mResolution = mResolution;
  settingsInitializer.mSheetOrientation = GetSheetOrientation();
  settingsInitializer.mNumCopies = mNumCopies;
  settingsInitializer.mDuplex = mDuplex;
  RefPtr<nsIPrintSettings> settingsToInitialize;
  MOZ_ALWAYS_SUCCEEDS(Clone(getter_AddRefs(settingsToInitialize)));
  settingsInitializer.mPrintSettings =
      new nsMainThreadPtrHolder<nsPrintSettings>(
          "PrintSettingsInitializer::mPrintSettings",
          settingsToInitialize.forget().downcast<nsPrintSettings>());
  return settingsInitializer;
}

NS_IMETHODIMP
nsPrintSettings::Assign(nsIPrintSettings* aPS) {
  NS_ENSURE_ARG(aPS);
  return _Assign(aPS);
}

//-------------------------------------------
nsPrintSettings& nsPrintSettings::operator=(const nsPrintSettings& rhs) {
  if (this == &rhs) {
    return *this;
  }

  mPageRanges = rhs.mPageRanges.Clone();
  mMargin = rhs.mMargin;
  mEdge = rhs.mEdge;
  mUnwriteableMargin = rhs.mUnwriteableMargin;
  mScaling = rhs.mScaling;
  mPrintBGColors = rhs.mPrintBGColors;
  mPrintBGImages = rhs.mPrintBGImages;
  mTitle = rhs.mTitle;
  mURL = rhs.mURL;
  mPrintSilent = rhs.mPrintSilent;
  mShrinkToFit = rhs.mShrinkToFit;
  mShowMarginGuides = rhs.mShowMarginGuides;
  mHonorPageRuleMargins = rhs.mHonorPageRuleMargins;
  mUsePageRuleSizeAsPaperSize = rhs.mUsePageRuleSizeAsPaperSize;
  mIgnoreUnwriteableMargins = rhs.mIgnoreUnwriteableMargins;
  mPrintSelectionOnly = rhs.mPrintSelectionOnly;
  mPaperId = rhs.mPaperId;
  mPaperWidth = rhs.mPaperWidth;
  mPaperHeight = rhs.mPaperHeight;
  mPaperSizeUnit = rhs.mPaperSizeUnit;
  mPrintInColor = rhs.mPrintInColor;
  mOrientation = rhs.mOrientation;
  mResolution = rhs.mResolution;
  mDuplex = rhs.mDuplex;
  mNumCopies = rhs.mNumCopies;
  mNumPagesPerSheet = rhs.mNumPagesPerSheet;
  mPrinter = rhs.mPrinter;
  mOutputDestination = rhs.mOutputDestination;
  mOutputStream = rhs.mOutputStream;
  mToFileName = rhs.mToFileName;
  mOutputFormat = rhs.mOutputFormat;
  mIsInitedFromPrinter = rhs.mIsInitedFromPrinter;
  mIsInitedFromPrefs = rhs.mIsInitedFromPrefs;
  mPrintPageDelay = rhs.mPrintPageDelay;

  for (int32_t i = 0; i < NUM_HEAD_FOOT; i++) {
    mHeaderStrs[i] = rhs.mHeaderStrs[i];
    mFooterStrs[i] = rhs.mFooterStrs[i];
  }

  return *this;
}

void nsPrintSettings::SetDefaultFileName() {
  nsAutoString filename;
  nsresult rv = GetToFileName(filename);
  if (NS_FAILED(rv) || filename.IsEmpty()) {
    const char* path = PR_GetEnv("PWD");
    if (!path) {
      path = PR_GetEnv("HOME");
    }

    if (path) {
      CopyUTF8toUTF16(mozilla::MakeStringSpan(path), filename);
      filename.AppendLiteral("/mozilla.pdf");
    } else {
      filename.AssignLiteral("mozilla.pdf");
    }

    SetToFileName(filename);
  }
}
