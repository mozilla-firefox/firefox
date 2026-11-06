/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsPrintSettingsGTK_h_
#define nsPrintSettingsGTK_h_

#include "nsPrintSettingsImpl.h"

extern "C" {
#include <gtk/gtk.h>
#include <gtk/gtkunixprint.h>
}

#define NS_PRINTSETTINGSGTK_IID \
  {0x758df520, 0xc7c3, 0x11dc, {0x95, 0xff, 0x08, 0x00, 0x20, 0x0c, 0x9a, 0x66}}

//*****************************************************************************
//***    nsPrintSettingsGTK
//*****************************************************************************

class nsPrintSettingsGTK : public nsPrintSettings {
 public:
  NS_DECL_ISUPPORTS_INHERITED
  NS_INLINE_DECL_STATIC_IID(NS_PRINTSETTINGSGTK_IID)

  nsPrintSettingsGTK();
  explicit nsPrintSettingsGTK(const PrintSettingsInitializer& aSettings);

  static nsPrintSettingsGTK* From(nsIPrintSettings* aPrintSettings) {
    return static_cast<nsPrintSettingsGTK*>(aPrintSettings);
  }

  // We're overriding these methods because we want to read/write with GTK
  // objects, not local variables. This allows a simpler settings implementation
  // between Gecko and GTK.

  GtkPageSetup* GetGtkPageSetup() { return mPageSetup; };
  void SetGtkPageSetup(GtkPageSetup* aPageSetup);

  GtkPrintSettings* GetGtkPrintSettings() { return mPrintSettings; };
  void SetGtkPrintSettings(GtkPrintSettings* aPrintSettings);

  GtkPrinter* GetGtkPrinter() { return mGTKPrinter; };
  void SetGtkPrinter(GtkPrinter* aPrinter);

  // Color, orientation and file name are all stored in the
  // GtkPrintSettings. Orientation is also stored in the GtkPageSetup and its
  // setting takes priority when getting the orientation.
  NS_IMETHOD GetPrintInColor(bool* aPrintInColor) override;
  NS_IMETHOD SetPrintInColor(bool aPrintInColor) override;

  NS_IMETHOD GetOrientation(int32_t* aOrientation) override;
  NS_IMETHOD SetOrientation(int32_t aOrientation) override;

  NS_IMETHOD GetToFileName(nsAString& aToFileName) override;
  NS_IMETHOD SetToFileName(const nsAString& aToFileName) override;

  // Gets/Sets the printer name in the GtkPrintSettings. If no printer name is
  // specified there, you will get back the name of the current internal
  // GtkPrinter.
  NS_IMETHOD GetPrinterName(nsAString& Printer) override;
  NS_IMETHOD SetPrinterName(const nsAString& aPrinter) override;

  // Number of copies is stored/gotten from the GtkPrintSettings.
  NS_IMETHOD GetNumCopies(int32_t* aNumCopies) override;
  NS_IMETHOD SetNumCopies(int32_t aNumCopies) override;

  NS_IMETHOD GetScaling(double* aScaling) override;
  NS_IMETHOD SetScaling(double aScaling) override;

  // A name recognised by GTK is strongly advised here, as this is used to
  // create a GtkPaperSize.
  NS_IMETHOD GetPaperId(nsAString& aPaperId) override;
  NS_IMETHOD SetPaperId(const nsAString& aPaperId) override;

  NS_IMETHOD SetUnwriteableMarginInTwips(
      nsIntMargin& aUnwriteableMargin) override;
  NS_IMETHOD SetUnwriteableMarginTop(double aUnwriteableMarginTop) override;
  NS_IMETHOD SetUnwriteableMarginLeft(double aUnwriteableMarginLeft) override;
  NS_IMETHOD SetUnwriteableMarginBottom(
      double aUnwriteableMarginBottom) override;
  NS_IMETHOD SetUnwriteableMarginRight(double aUnwriteableMarginRight) override;

  NS_IMETHOD GetPaperWidth(double* aPaperWidth) override;
  NS_IMETHOD SetPaperWidth(double aPaperWidth) override;

  NS_IMETHOD GetPaperHeight(double* aPaperHeight) override;
  NS_IMETHOD SetPaperHeight(double aPaperHeight) override;

  NS_IMETHOD SetPaperSizeUnit(int16_t aPaperSizeUnit) override;

  NS_IMETHOD GetEffectivePageSize(double* aWidth, double* aHeight) override;

  NS_IMETHOD SetPageRanges(const nsTArray<int32_t>&) override;
  NS_IMETHOD GetPageRanges(nsTArray<int32_t>&) override;

  NS_IMETHOD GetResolution(int32_t* aResolution) override;
  NS_IMETHOD SetResolution(int32_t aResolution) override;

  NS_IMETHOD GetDuplex(int32_t* aDuplex) override;
  NS_IMETHOD SetDuplex(int32_t aDuplex) override;

  NS_IMETHOD GetOutputFormat(int16_t* aOutputFormat) override;

 protected:
  virtual ~nsPrintSettingsGTK();

  nsPrintSettingsGTK(const nsPrintSettingsGTK& src);
  nsPrintSettingsGTK& operator=(const nsPrintSettingsGTK& rhs);

  virtual nsresult _Clone(nsIPrintSettings** _retval) override;
  virtual nsresult _Assign(nsIPrintSettings* aPS) override;

  GtkUnit GetGTKUnit(int16_t aGeckoUnit);
  void SaveNewPageSize();

  /**
   * Re-initialize mUnwriteableMargin with values from mPageSetup.
   * Should be called whenever mPageSetup is initialized or overwritten.
   */
  void InitUnwriteableMargin();

  /**
   * On construction:
   * - mPrintSettings and mPageSetup are just new objects with defaults
   *   determined by GTK.
   * - mGTKPrinter is nullptr!!! Remember to be careful when accessing this
   *   property.
   */
  GtkPageSetup* mPageSetup;
  GtkPrintSettings* mPrintSettings;
  GtkPrinter* mGTKPrinter;
};

#endif  // nsPrintSettingsGTK_h_
