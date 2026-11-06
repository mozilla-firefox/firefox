/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsPrintSettingsImpl_h_
#define nsPrintSettingsImpl_h_

#include "nsIPrintSettings.h"
#include "nsIWeakReferenceUtils.h"
#include "nsMargin.h"
#include "nsPaper.h"
#include "nsProxyRelease.h"
#include "nsString.h"

#define NUM_HEAD_FOOT 3

//*****************************************************************************
//***    nsPrintSettings
//*****************************************************************************

class nsPrintSettings;

namespace mozilla {

/**
 * A struct that can be used off the main thread to collect printer-specific
 * info that can be used to initialized a default nsIPrintSettings object.
 */
struct PrintSettingsInitializer {
  nsString mPrinter;
  PaperInfo mPaperInfo;
  int16_t mPaperSizeUnit = nsIPrintSettings::kPaperSizeInches;
  // If we fail to obtain printer capabilities, being given the option to print
  // in color to your monochrome printer is a lot less annoying than not being
  // given the option to print in color to your color printer.
  bool mPrintInColor = true;
  int mResolution = 0;
  int mSheetOrientation = nsIPrintSettings::kPortraitOrientation;
  int mNumCopies = 1;
  int mDuplex = nsIPrintSettings::kDuplexNone;

  // This is to hold a reference to a newly cloned settings object that will
  // then be initialized by the other values in the initializer that may have
  // been changed on a background thread.
  nsMainThreadPtrHandle<nsPrintSettings> mPrintSettings;

#ifdef XP_WIN
  CopyableTArray<uint8_t> mDevmodeWStorage;
#endif
};

}  // namespace mozilla

class nsPrintSettings : public nsIPrintSettings {
 public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIPRINTSETTINGS
  using PrintSettingsInitializer = mozilla::PrintSettingsInitializer;

  nsPrintSettings();
  nsPrintSettings(const nsPrintSettings& aPS);

  /**
   * Initialize relevant members from the PrintSettingsInitializer.
   * This is specifically not a constructor so that we can ensure that the
   * relevant setters are dynamically dispatched to derived classes.
   */
  virtual void InitWithInitializer(const PrintSettingsInitializer& aSettings);

  nsPrintSettings& operator=(const nsPrintSettings& rhs);

  // Sets a default file name for the print settings.
  void SetDefaultFileName();

 protected:
  virtual ~nsPrintSettings();

  // May be implemented by the platform-specific derived class
  virtual nsresult _Clone(nsIPrintSettings** _retval);
  virtual nsresult _Assign(nsIPrintSettings* aPS);

  // Members
  nsWeakPtr mSession;  // Should never be touched by Clone or Assign

  // mMargin, mEdge, and mUnwriteableMargin are stored in twips
  nsIntMargin mMargin;
  nsIntMargin mEdge;
  nsIntMargin mUnwriteableMargin;

  nsTArray<int32_t> mPageRanges;

  double mScaling = 1.0;
  bool mPrintBGColors = false;
  bool mPrintBGImages = false;

  bool mPrintSilent = false;
  bool mShrinkToFit = true;
  bool mShowMarginGuides = false;
  bool mHonorPageRuleMargins = true;
  bool mUsePageRuleSizeAsPaperSize = false;
  bool mIgnoreUnwriteableMargins = false;
  bool mPrintSelectionOnly = false;

  int32_t mPrintPageDelay = 50;  // XXX Do we really want this?

  nsString mTitle;
  nsString mURL;
  nsString mHeaderStrs[NUM_HEAD_FOOT];
  nsString mFooterStrs[NUM_HEAD_FOOT];

  nsString mPaperId;
  double mPaperWidth = 8.5;
  double mPaperHeight = 11.0;
  int16_t mPaperSizeUnit = kPaperSizeInches;

  bool mPrintInColor = true;
  int32_t mOrientation = kPortraitOrientation;
  int32_t mResolution = 0;
  int32_t mDuplex = kDuplexNone;
  int32_t mNumCopies = 1;
  int32_t mNumPagesPerSheet = 1;
  int16_t mOutputFormat = kOutputFormatNative;
  OutputDestinationType mOutputDestination = kOutputDestinationPrinter;
  nsString mPrinter;
  nsString mToFileName;
  nsCOMPtr<nsIOutputStream> mOutputStream;
  bool mIsInitedFromPrinter = false;
  bool mIsInitedFromPrefs = false;
};

#endif /* nsPrintSettings_h__ */
