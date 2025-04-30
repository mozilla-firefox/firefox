/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";
import { Region } from "resource://gre/modules/Region.sys.mjs";
import { AddressMetaDataLoader } from "resource://gre/modules/shared/AddressMetaDataLoader.sys.mjs";

const AUTOFILL_ADDRESSES_AVAILABLE_PREF =
  "extensions.formautofill.addresses.supported";
// This pref should be refactored after the migration of the old bool pref
const AUTOFILL_CREDITCARDS_AVAILABLE_PREF =
  "extensions.formautofill.creditCards.supported";
const BROWSER_SEARCH_REGION_PREF = "browser.search.region";
const CREDITCARDS_AUTOFILL_SUPPORTED_COUNTRIES_PREF =
  "extensions.formautofill.creditCards.supportedCountries";
const ENABLED_AUTOFILL_ADDRESSES_PREF =
  "extensions.formautofill.addresses.enabled";
const ENABLED_AUTOFILL_ADDRESSES_CAPTURE_PREF =
  "extensions.formautofill.addresses.capture.enabled";
const ENABLED_AUTOFILL_ADDRESSES_CAPTURE_REQUIRED_FIELDS_PREF =
  "extensions.formautofill.addresses.capture.requiredFields";
const ENABLED_AUTOFILL_ADDRESSES_SUPPORTED_COUNTRIES_PREF =
  "extensions.formautofill.addresses.supportedCountries";
const ENABLED_AUTOFILL_CREDITCARDS_PREF =
  "extensions.formautofill.creditCards.enabled";
const AUTOFILL_CREDITCARDS_REAUTH_PREF =
  "extensions.formautofill.creditCards.reauth.optout";
const AUTOFILL_CREDITCARDS_HIDE_UI_PREF =
  "extensions.formautofill.creditCards.hideui";
const FORM_AUTOFILL_SUPPORT_RTL_PREF = "extensions.formautofill.supportRTL";
const AUTOFILL_CREDITCARDS_AUTOCOMPLETE_OFF_PREF =
  "extensions.formautofill.creditCards.ignoreAutocompleteOff";
const AUTOFILL_ADDRESSES_AUTOCOMPLETE_OFF_PREF =
  "extensions.formautofill.addresses.ignoreAutocompleteOff";
const ENABLED_AUTOFILL_CAPTURE_ON_FORM_REMOVAL_PREF =
  "extensions.formautofill.heuristics.captureOnFormRemoval";
const ENABLED_AUTOFILL_CAPTURE_ON_PAGE_NAVIGATION_PREF =
  "extensions.formautofill.heuristics.captureOnPageNavigation";
const ENABLED_AUTOFILL_SAME_ORIGIN_WITH_TOP =
  "extensions.formautofill.heuristics.autofillSameOriginWithTop";
const ENABLED_AUTOFILL_DETECT_DYNAMIC_FORM_CHANGES_PREF =
  "extensions.formautofill.heuristics.detectDynamicFormChanges";
const AUTOFILL_FILL_ON_DYNAMIC_FORM_CHANGES_TIMEOUT_PREF =
  "extensions.formautofill.heuristics.fillOnDynamicFormChanges.timeout";
const AUTOFILL_FILL_ON_DYNAMIC_FORM_CHANGES_PREF =
  "extensions.formautofill.heuristics.fillOnDynamicFormChanges";
const AUTOFILL_REFILL_ON_SITE_CLEARING_VALUE_PREF =
  "extensions.formautofill.heuristics.refillOnSiteClearingFields";
const AUTOFILL_REFILL_ON_SITE_CLEARING_VALUE_TIMEOUT_PREF =
  "extensions.formautofill.heuristics.refillOnSiteClearingFields.timeout";

export const FormAutofill = {
  ENABLED_AUTOFILL_ADDRESSES_PREF,
  ENABLED_AUTOFILL_ADDRESSES_CAPTURE_PREF,
  ENABLED_AUTOFILL_CAPTURE_ON_FORM_REMOVAL_PREF,
  ENABLED_AUTOFILL_CAPTURE_ON_PAGE_NAVIGATION_PREF,
  ENABLED_AUTOFILL_SAME_ORIGIN_WITH_TOP,
  ENABLED_AUTOFILL_CREDITCARDS_PREF,
  ENABLED_AUTOFILL_DETECT_DYNAMIC_FORM_CHANGES_PREF,
  AUTOFILL_CREDITCARDS_REAUTH_PREF,
  AUTOFILL_CREDITCARDS_AUTOCOMPLETE_OFF_PREF,
  AUTOFILL_ADDRESSES_AUTOCOMPLETE_OFF_PREF,
  AUTOFILL_FILL_ON_DYNAMIC_FORM_CHANGES_PREF,
  AUTOFILL_FILL_ON_DYNAMIC_FORM_CHANGES_TIMEOUT_PREF,
  AUTOFILL_REFILL_ON_SITE_CLEARING_VALUE_PREF,
  AUTOFILL_REFILL_ON_SITE_CLEARING_VALUE_TIMEOUT_PREF,

  _region: null,

  get DEFAULT_REGION() {
    return this._region || Region.home || "US";
  },

  set DEFAULT_REGION(region) {
    this._region = region;
  },

  /**
   * Determines if an autofill feature should be enabled based on the "available"
   * and "supportedCountries" parameters.
   *
   * @param {string} available Available can be one of the following: "on", "detect", "off".
   * "on" forces the particular Form Autofill feature on, while "detect" utilizes the supported countries
   * to see if the feature should be available.
   * @param {string[]} supportedCountries
   * @returns {boolean} `true` if autofill feature is supported in the current browser search region
   */
  _isSupportedRegion(available, supportedCountries) {
    if (available == "on") {
      return true;
    } else if (available == "detect") {
      if (!FormAutofill.supportRTL && Services.locale.isAppLocaleRTL) {
        return false;
      }

      return supportedCountries.includes(FormAutofill.browserSearchRegion);
    }
    return false;
  },

  /**
   * Return true if address autofill is available for a specific country.
   */
  isAutofillAddressesAvailableInCountry(country) {
    if (FormAutofill._isAutofillAddressesAvailableInExperiment) {
      return true;
    }

    let available = FormAutofill._isAutofillAddressesAvailable;
    if (country && available == "detect") {
      return FormAutofill._addressAutofillSupportedCountries.includes(
        country.toUpperCase()
      );
    }
    return available == "on";
  },
  get isAutofillEnabled() {
    return this.isAutofillAddressesEnabled || this.isAutofillCreditCardsEnabled;
  },
  /**
   * Determines if the credit card autofill feature is available to use in the browser.
   * If the feature is not available, then there are no user facing ways to enable it.
   *
   * @returns {boolean} `true` if credit card autofill is available
   */
  get isAutofillCreditCardsAvailable() {
    return this._isSupportedRegion(
      FormAutofill._isAutofillCreditCardsAvailable,
      FormAutofill._creditCardAutofillSupportedCountries
    );
  },
  /**
   * Determines if the address autofill feature is available to use in the browser.
   * If the feature is not available, then there are no user facing ways to enable it.
   * Two conditions must be met for the autofill feature to be considered available:
   *   1. Address autofill support is confirmed when:
   *      - `extensions.formautofill.addresses.supported` is set to `on`.
   *      - The user is located in a region supported by the feature
   *        (`extensions.formautofill.creditCards.supportedCountries`).
   *   2. Address autofill is enabled through a Nimbus experiment:
   *      - The experiment pref `extensions.formautofill.addresses.experiments.enabled` is set to true.
   *
   * @returns {boolean} `true` if address autofill is available
   */
  get isAutofillAddressesAvailable() {
    const isUserInSupportedRegion = this._isSupportedRegion(
      FormAutofill._isAutofillAddressesAvailable,
      FormAutofill._addressAutofillSupportedCountries
    );
    return (
      isUserInSupportedRegion ||
      FormAutofill._isAutofillAddressesAvailableInExperiment
    );
  },
  /**
   * Determines if the user has enabled or disabled credit card autofill.
   *
   * @returns {boolean} `true` if credit card autofill is enabled
   */
  get isAutofillCreditCardsEnabled() {
    return (
      this.isAutofillCreditCardsAvailable &&
      FormAutofill._isAutofillCreditCardsEnabled
    );
  },
  /**
   * Determines if credit card autofill is locked by policy.
   *
   * @returns {boolean} `true` if credit card autofill is locked
   */
  get isAutofillCreditCardsLocked() {
    return Services.prefs.prefIsLocked(ENABLED_AUTOFILL_CREDITCARDS_PREF);
  },
  /**
   * Determines if the user has enabled or disabled address autofill.
   *
   * @returns {boolean} `true` if address autofill is enabled
   */
  get isAutofillAddressesEnabled() {
    return (
      this.isAutofillAddressesAvailable &&
      FormAutofill._isAutofillAddressesEnabled
    );
  },
  /**
   * Determines if address autofill is locked by policy.
   *
   * @returns {boolean} `true` if address autofill is locked
   */
  get isAutofillAddressesLocked() {
    return Services.prefs.prefIsLocked(ENABLED_AUTOFILL_ADDRESSES_PREF);
  },

  defineLogGetter(scope, logPrefix) {
    // A logging helper for debug logging to avoid creating Console objects
    // or triggering expensive JS -> C++ calls when debug logging is not
    // enabled.
    //
    // Console objects, even natively-implemented ones, can consume a lot of
    // memory, and since this code may run in every content process, that
    // memory can add up quickly. And, even when debug-level messages are
    // being ignored, console.debug() calls can be expensive.
    //
    // This helper avoids both of those problems by never touching the
    // console object unless debug logging is enabled.
    scope.debug = function debug() {
      if (FormAutofill.logLevel.toLowerCase() == "debug") {
        this.log.debug(...arguments);
      }
    };

    let { ConsoleAPI } = ChromeUtils.importESModule(
      "resource://gre/modules/Console.sys.mjs"
    );
    return new ConsoleAPI({
      maxLogLevelPref: "extensions.formautofill.loglevel",
      prefix: logPrefix,
    });
  },

  get isMLExperimentEnabled() {
    return FormAutofill._isMLEnabled && FormAutofill._isMLExperimentEnabled;
  },
};

// TODO: Bug 1747284. Use Region.home instead of reading "browser.serach.region"
// by default. However, Region.home doesn't observe preference change at this point,
// we should also fix that issue.
XPCOMUtils.defineLazyPreferenceGetter(
  FormAutofill,
  "browserSearchRegion",
  BROWSER_SEARCH_REGION_PREF,
  FormAutofill.DEFAULT_REGION
);
XPCOMUtils.defineLazyPreferenceGetter(
  FormAutofill,
  "logLevel",
  "extensions.formautofill.loglevel",
  "Warn"
);

XPCOMUtils.defineLazyPreferenceGetter(
  FormAutofill,
  "_isAutofillAddressesAvailable",
  AUTOFILL_ADDRESSES_AVAILABLE_PREF
);
XPCOMUtils.defineLazyPreferenceGetter(
  FormAutofill,
  "_isAutofillAddressesEnabled",
  ENABLED_AUTOFILL_ADDRESSES_PREF
);
XPCOMUtils.defineLazyPreferenceGetter(
  FormAutofill,
  "isAutofillAddressesCaptureEnabled",
  ENABLED_AUTOFILL_ADDRESSES_CAPTURE_PREF
);
XPCOMUtils.defineLazyPreferenceGetter(
  FormAutofill,
  "_isAutofillCreditCardsAvailable",
  AUTOFILL_CREDITCARDS_AVAILABLE_PREF
);
XPCOMUtils.defineLazyPreferenceGetter(
  FormAutofill,
  "_isAutofillCreditCardsEnabled",
  ENABLED_AUTOFILL_CREDITCARDS_PREF
);
XPCOMUtils.defineLazyPreferenceGetter(
  FormAutofill,
  "isAutofillCreditCardsHideUI",
  AUTOFILL_CREDITCARDS_HIDE_UI_PREF
);
XPCOMUtils.defineLazyPreferenceGetter(
  FormAutofill,
  "_addressAutofillSupportedCountries",
  ENABLED_AUTOFILL_ADDRESSES_SUPPORTED_COUNTRIES_PREF,
  null,
  val => val.split(",")
);
XPCOMUtils.defineLazyPreferenceGetter(
  FormAutofill,
  "_creditCardAutofillSupportedCountries",
  CREDITCARDS_AUTOFILL_SUPPORTED_COUNTRIES_PREF,
  null,
  null,
  val => val.split(",")
);
XPCOMUtils.defineLazyPreferenceGetter(
  FormAutofill,
  "supportRTL",
  FORM_AUTOFILL_SUPPORT_RTL_PREF
);
XPCOMUtils.defineLazyPreferenceGetter(
  FormAutofill,
  "creditCardsAutocompleteOff",
  AUTOFILL_CREDITCARDS_AUTOCOMPLETE_OFF_PREF
);
XPCOMUtils.defineLazyPreferenceGetter(
  FormAutofill,
  "addressesAutocompleteOff",
  AUTOFILL_ADDRESSES_AUTOCOMPLETE_OFF_PREF
);
XPCOMUtils.defineLazyPreferenceGetter(
  FormAutofill,
  "captureOnFormRemoval",
  ENABLED_AUTOFILL_CAPTURE_ON_FORM_REMOVAL_PREF
);
XPCOMUtils.defineLazyPreferenceGetter(
  FormAutofill,
  "captureOnPageNavigation",
  ENABLED_AUTOFILL_CAPTURE_ON_PAGE_NAVIGATION_PREF
);
XPCOMUtils.defineLazyPreferenceGetter(
  FormAutofill,
  "addressCaptureRequiredFields",
  ENABLED_AUTOFILL_ADDRESSES_CAPTURE_REQUIRED_FIELDS_PREF,
  null,
  null,
  val => val?.split(",").filter(v => !!v)
);
XPCOMUtils.defineLazyPreferenceGetter(
  FormAutofill,
  "autofillSameOriginWithTop",
  ENABLED_AUTOFILL_SAME_ORIGIN_WITH_TOP
);

XPCOMUtils.defineLazyPreferenceGetter(
  FormAutofill,
  "_isAutofillAddressesAvailableInExperiment",
  "extensions.formautofill.addresses.experiments.enabled"
);

XPCOMUtils.defineLazyPreferenceGetter(
  FormAutofill,
  "_isMLEnabled",
  "browser.ml.enable",
  false
);

XPCOMUtils.defineLazyPreferenceGetter(
  FormAutofill,
  "_isMLExperimentEnabled",
  "extensions.formautofill.ml.experiment.enabled",
  false
);

XPCOMUtils.defineLazyPreferenceGetter(
  FormAutofill,
  "MLModelRevision",
  "extensions.formautofill.ml.experiment.modelRevision",
  null
);

XPCOMUtils.defineLazyPreferenceGetter(
  FormAutofill,
  "detectDynamicFormChanges",
  "extensions.formautofill.heuristics.detectDynamicFormChanges",
  false
);

XPCOMUtils.defineLazyPreferenceGetter(
  FormAutofill,
  "fillOnDynamicFormChanges",
  "extensions.formautofill.heuristics.fillOnDynamicFormChanges",
  false
);

XPCOMUtils.defineLazyPreferenceGetter(
  FormAutofill,
  "fillOnDynamicFormChangeTimeout",
  "extensions.formautofill.heuristics.fillOnDynamicFormChanges.timeout",
  0
);

XPCOMUtils.defineLazyPreferenceGetter(
  FormAutofill,
  "refillOnSiteClearingFields",
  "extensions.formautofill.heuristics.refillOnSiteClearingFields",
  false
);

XPCOMUtils.defineLazyPreferenceGetter(
  FormAutofill,
  "refillOnSiteClearingFieldsTimeout",
  "extensions.formautofill.heuristics.refillOnSiteClearingFields.timeout",
  0
);

ChromeUtils.defineLazyGetter(FormAutofill, "countries", () =>
  AddressMetaDataLoader.getCountries()
);
