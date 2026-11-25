/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsPrintSettingsService_h
#define nsPrintSettingsService_h

#include "nsIPrintSettings.h"
#include "nsIPrintSettingsService.h"
#include "nsString.h"

/**
 * Class nsPrintSettingsService.  Base class for the platform specific widget
 * subclasses to inherit from.
 */
class nsPrintSettingsService : public nsIPrintSettingsService {
 public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIPRINTSETTINGSSERVICE

  nsPrintSettingsService() = default;

  // Copying is not supported.
  nsPrintSettingsService(const nsPrintSettingsService& x) = delete;
  nsPrintSettingsService& operator=(const nsPrintSettingsService& x) = delete;

  /**
   * method Init
   *  Initializes member variables. Every consumer that does manual
   *  creation (instead of do_CreateInstance) needs to call this method
   *  immediately after instantiation.
   */
  virtual nsresult Init();

 private:
 protected:
  virtual ~nsPrintSettingsService() = default;

  void ReadBitFieldPref(const char* aPrefId, int32_t anOption);
  void WriteBitFieldPref(const char* aPrefId, int32_t anOption);
  void ReadJustification(const char* aPrefId, int16_t& aJust,
                         int16_t aInitValue);
  void WriteJustification(const char* aPrefId, int16_t aJust);
  bool ReadInchesToTwipsPref(const char* aAttr, const nsAString& aPrinterName,
                             int32_t& aTwips);
  void WriteInchesFromTwipsPref(const char* aAttr,
                                const nsAString& aPrinterName, int32_t aTwips);
  bool ReadInchesIntToTwipsPref(const char* aAttr,
                                const nsAString& aPrinterName, int32_t& aTwips);
  void WriteInchesIntFromTwipsPref(const char* aAttr,
                                   const nsAString& aPrinterName,
                                   int32_t aTwips);

  nsresult ReadPrefDouble(const char* aAttr, const nsAString& aPrinterName,
                          double& aVal);
  nsresult WritePrefDouble(const char* aAttr, const nsAString& aPrinterName,
                           double aVal);

  /**
   * method ReadPrefs
   * @param aPS          a pointer to the printer settings
   * @param aPrinterName the name of the printer for which to read prefs
   * @param aFlags       flag specifying which prefs to read
   */
  virtual nsresult ReadPrefs(nsIPrintSettings* aPS,
                             const nsAString& aPrinterName, uint32_t aFlags);
  /**
   * method WritePrefs
   * @param aPS          a pointer to the printer settings
   * @param aPrinterName the name of the printer for which to write prefs
   * @param aFlags       flag specifying which prefs to read
   */
  virtual nsresult WritePrefs(nsIPrintSettings* aPS,
                              const nsAString& aPrinterName, uint32_t aFlags);

  /**
   * method _CreatePrintSettings
   *   May be implemented by the platform-specific derived class
   *
   * @return             printer settings instance
   */
  virtual nsresult _CreatePrintSettings(nsIPrintSettings** _retval) = 0;
};

#endif  // nsPrintSettingsService_h
