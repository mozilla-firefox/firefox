/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Tests that Suggest is enabled by default for appropriate region-locales and
// disabled by default everywhere else.

"use strict";

ChromeUtils.defineESModuleGetters(this, {
  Preferences: "resource://gre/modules/Preferences.sys.mjs",
  TelemetryReportingPolicy:
    "resource://gre/modules/TelemetryReportingPolicy.sys.mjs",
});

const { SUGGEST_TOU_TIMESTAMP } = QuickSuggest;

// Expected prefs when Suggest is completely disabled.
const EXPECTED_PREFS_SUGGEST_DISABLED = {
  "quicksuggest.enabled": false,
  "quicksuggest.online.available": false,
  "quicksuggest.online.enabled": true,
  "quicksuggest.settingsUi": QuickSuggest.SETTINGS_UI.NONE,
  "suggest.quicksuggest.all": false,
  "suggest.quicksuggest.sponsored": false,
  "addons.featureGate": false,
  "amp.featureGate": false,
  "importantDates.featureGate": false,
  "mdn.featureGate": false,
  "temporal.featureGate": false,
  "weather.featureGate": false,
  "wikipedia.featureGate": false,
  "yelp.featureGate": false,
};

// Base set of expected prefs for US, GB, and the EU 3 (DE, FR, and IT).
const EXPECTED_PREFS_BASE_US_GB_EU_3 = {
  ...EXPECTED_PREFS_SUGGEST_DISABLED,
  "quicksuggest.enabled": true,
  "quicksuggest.settingsUi": QuickSuggest.SETTINGS_UI.OFFLINE_ONLY,
  "suggest.quicksuggest.all": true,
  "suggest.quicksuggest.sponsored": true,
  "amp.featureGate": true,
  "importantDates.featureGate": true,
  "weather.featureGate": true,
  "wikipedia.featureGate": true,
};

// Expected prefs for US.
const EXPECTED_PREFS_US = {
  ...EXPECTED_PREFS_BASE_US_GB_EU_3,
  "addons.featureGate": true,
  "mdn.featureGate": true,
  "yelp.featureGate": true,
};

// Expected prefs for `en` locales in the EU 3 (DE, FR, and IT), e.g., the
// `en-US` locale in Germany.
const EXPECTED_PREFS_EU_3_EN = {
  ...EXPECTED_PREFS_SUGGEST_DISABLED,
  "quicksuggest.enabled": true,
  "importantDates.featureGate": true,
};

// Expected prefs for the EU expansion in 157 (bug 2066294):
// AT, BE, CH, CZ, DK, ES, FI, HU, IE, LU, NL, NO, PL, PT, SE, SK
const EXPECTED_PREFS_EU_157 = {
  ...EXPECTED_PREFS_SUGGEST_DISABLED,
  "quicksuggest.enabled": true,
  "quicksuggest.settingsUi": QuickSuggest.SETTINGS_UI.OFFLINE_ONLY,
  "suggest.quicksuggest.all": true,
  "suggest.quicksuggest.sponsored": true,
  "amp.featureGate": true,
  "wikipedia.featureGate": true,
};

// Expected prefs for the CA expansion in 162 (bug 2090434)
const EXPECTED_PREFS_CA_162_EN = { ...EXPECTED_PREFS_EU_157 };
const EXPECTED_PREFS_CA_162 = {
  ...EXPECTED_PREFS_CA_162_EN,
  "suggest.quicksuggest.sponsored": false,
  "amp.featureGate": false,
};

add_setup(async () => {
  await UrlbarTestUtils.initNimbusFeature();
});

add_task(async function primary() {
  let tests = [
    // US: only `en` locales
    {
      region: "US",
      locale: "en-CA",
      expectedPrefs: EXPECTED_PREFS_US,
    },
    {
      region: "US",
      locale: "en-GB",
      expectedPrefs: EXPECTED_PREFS_US,
    },
    {
      region: "US",
      locale: "en-US",
      expectedPrefs: EXPECTED_PREFS_US,
    },
    {
      region: "US",
      locale: "es-MX",
      expectedPrefs: EXPECTED_PREFS_SUGGEST_DISABLED,
    },

    // GB and EU 3 (DE, FR, and IT): native and `en` locales
    {
      region: "DE",
      locale: "de",
      expectedPrefs: EXPECTED_PREFS_BASE_US_GB_EU_3,
    },
    {
      region: "DE",
      locale: "en-GB",
      expectedPrefs: EXPECTED_PREFS_EU_3_EN,
    },
    {
      region: "DE",
      locale: "en-US",
      expectedPrefs: EXPECTED_PREFS_EU_3_EN,
    },
    {
      region: "DE",
      locale: "xx",
      expectedPrefs: EXPECTED_PREFS_SUGGEST_DISABLED,
    },

    {
      region: "FR",
      locale: "fr",
      expectedPrefs: EXPECTED_PREFS_BASE_US_GB_EU_3,
    },
    {
      region: "FR",
      locale: "en-GB",
      expectedPrefs: EXPECTED_PREFS_EU_3_EN,
    },
    {
      region: "FR",
      locale: "en-US",
      expectedPrefs: EXPECTED_PREFS_EU_3_EN,
    },
    {
      region: "FR",
      locale: "xx",
      expectedPrefs: EXPECTED_PREFS_SUGGEST_DISABLED,
    },

    {
      region: "GB",
      locale: "en-GB",
      expectedPrefs: EXPECTED_PREFS_BASE_US_GB_EU_3,
    },
    {
      region: "GB",
      locale: "en-US",
      expectedPrefs: EXPECTED_PREFS_BASE_US_GB_EU_3,
    },
    {
      region: "GB",
      locale: "xx",
      expectedPrefs: EXPECTED_PREFS_SUGGEST_DISABLED,
    },

    {
      region: "IT",
      locale: "it",
      expectedPrefs: EXPECTED_PREFS_BASE_US_GB_EU_3,
    },
    {
      region: "IT",
      locale: "en-GB",
      expectedPrefs: EXPECTED_PREFS_EU_3_EN,
    },
    {
      region: "IT",
      locale: "en-US",
      expectedPrefs: EXPECTED_PREFS_EU_3_EN,
    },
    {
      region: "IT",
      locale: "xx",
      expectedPrefs: EXPECTED_PREFS_SUGGEST_DISABLED,
    },

    // EU expansion in 157 (bug 2066294): locale doesn't matter, only region
    {
      region: "AT",
      locale: "at",
      expectedPrefs: EXPECTED_PREFS_EU_157,
    },
    {
      region: "BE",
      locale: "be",
      expectedPrefs: EXPECTED_PREFS_EU_157,
    },
    {
      region: "CH",
      locale: "ch",
      expectedPrefs: EXPECTED_PREFS_EU_157,
    },
    {
      region: "CZ",
      locale: "cz",
      expectedPrefs: EXPECTED_PREFS_EU_157,
    },
    {
      region: "DK",
      locale: "dk",
      expectedPrefs: EXPECTED_PREFS_EU_157,
    },
    {
      region: "ES",
      locale: "es",
      expectedPrefs: EXPECTED_PREFS_EU_157,
    },
    {
      region: "FI",
      locale: "fi",
      expectedPrefs: EXPECTED_PREFS_EU_157,
    },
    {
      region: "HU",
      locale: "hu",
      expectedPrefs: EXPECTED_PREFS_EU_157,
    },
    {
      region: "IE",
      locale: "ie",
      expectedPrefs: EXPECTED_PREFS_EU_157,
    },
    {
      region: "LU",
      locale: "lu",
      expectedPrefs: EXPECTED_PREFS_EU_157,
    },
    {
      region: "NL",
      locale: "nl",
      expectedPrefs: EXPECTED_PREFS_EU_157,
    },
    {
      region: "NO",
      locale: "no",
      expectedPrefs: EXPECTED_PREFS_EU_157,
    },
    {
      region: "PL",
      locale: "pl",
      expectedPrefs: EXPECTED_PREFS_EU_157,
    },
    {
      region: "PT",
      locale: "pt",
      expectedPrefs: EXPECTED_PREFS_EU_157,
    },
    {
      region: "SE",
      locale: "se",
      expectedPrefs: EXPECTED_PREFS_EU_157,
    },
    {
      region: "SK",
      locale: "sk",
      expectedPrefs: EXPECTED_PREFS_EU_157,
    },

    {
      region: "AT",
      locale: "xx",
      expectedPrefs: EXPECTED_PREFS_EU_157,
    },

    // CA expansion in 162 (bug 2090434): sponsored only for `en` locales
    {
      region: "CA",
      locale: "en-CA",
      expectedPrefs: EXPECTED_PREFS_CA_162_EN,
    },
    {
      region: "CA",
      locale: "fr",
      expectedPrefs: EXPECTED_PREFS_CA_162,
    },
    {
      region: "CA",
      locale: "xx",
      expectedPrefs: EXPECTED_PREFS_CA_162,
    },

    // regions where Suggest should be completely disabled
    {
      region: "JP",
      locale: "ja",
      expectedPrefs: EXPECTED_PREFS_SUGGEST_DISABLED,
    },
  ];

  for (let { locale, region, expectedPrefs } of tests) {
    await doPrimaryTest({ locale, region, expectedPrefs });
  }
});

/**
 * Sets the app's locale and region, reinitializes Suggest, and asserts that the
 * pref values are correct.
 *
 * @param {object} options
 *   Options object.
 * @param {string} options.locale
 *   The locale to simulate.
 * @param {string} options.region
 *   The "home" region to simulate.
 * @param {object} options.expectedPrefs
 *   Map from pref names (relative to `browser.urlbar.`) to expected values.
 */
async function doPrimaryTest({ locale, region, expectedPrefs }) {
  let defaultBranch = new Preferences({
    branch: "browser.urlbar.",
    defaultBranch: true,
  });
  let userBranch = new Preferences({
    branch: "browser.urlbar.",
    defaultBranch: false,
  });

  // Setup: Clear any user values and save original default-branch values.
  let originalDefaults = {};
  for (let name of Object.keys(expectedPrefs)) {
    userBranch.reset(name);
    originalDefaults[name] = defaultBranch.get(name);
  }

  // Clear the migration version to simulate a new profile. `QuickSuggest` will
  // perform a full migration process, applying each migration version in turn
  // until it reaches the current version.
  userBranch.reset("quicksuggest.migrationVersion");

  // Set the region and locale, call the function, check the pref values.
  await QuickSuggestTestUtils.withRegionAndLocale({
    region,
    locale,
    callback: async () => {
      for (let [name, value] of Object.entries(expectedPrefs)) {
        // Check the default-branch value.
        Assert.strictEqual(
          defaultBranch.get(name),
          value,
          `Default pref value for ${name}, locale ${locale}, region ${region}`
        );

        // For good measure, also check the return value of `UrlbarPrefs.get`
        // since we use it everywhere. The value should be the same as the
        // default-branch value.
        UrlbarPrefs.get(
          name,
          value,
          `UrlbarPrefs.get() value for ${name}, locale ${locale}, region ${region}`
        );

        // Make sure migration didn't unexpectedly set any user-branch values.
        Assert.ok(
          !userBranch.isSet(name),
          "Pref should not be set on the user branch: " + name
        );
      }
    },
  });

  // Teardown: Restore original default-branch values for the next task.
  for (let [name, originalDefault] of Object.entries(originalDefaults)) {
    if (originalDefault === undefined) {
      Services.prefs.deleteBranch("browser.urlbar." + name);
    } else {
      defaultBranch.set(name, originalDefault);
    }
  }
}

// Online Suggest should be available at the time Suggest is initialized if: the
// the user has accepted ToU, and Suggest overall is enabled. Online Suggest
// should not be available otherwise.
add_task(async function onlineAvailable_init() {
  let tests = [
    // Online should be available iff ToU are accepted
    {
      touAcceptedDate: 0,
      expected: {
        "quicksuggest.online.available": false,
        "quicksuggest.settingsUi": QuickSuggest.SETTINGS_UI.OFFLINE_ONLY,
        "flightStatus.featureGate": false,
        "market.featureGate": false,
        "sports.featureGate": false,
      },
    },
    {
      touAcceptedDate: SUGGEST_TOU_TIMESTAMP - 1,
      expected: {
        "quicksuggest.online.available": false,
        "quicksuggest.settingsUi": QuickSuggest.SETTINGS_UI.OFFLINE_ONLY,
        "flightStatus.featureGate": false,
        "market.featureGate": false,
        "sports.featureGate": false,
      },
    },
    {
      touAcceptedDate: SUGGEST_TOU_TIMESTAMP,
      expected: {
        "quicksuggest.online.available": true,
        "quicksuggest.settingsUi": QuickSuggest.SETTINGS_UI.FULL,
        "flightStatus.featureGate": true,
        "market.featureGate": true,
        "sports.featureGate": true,
      },
    },

    // ToU accepted but region/locale where Suggest is not enabled: online
    // should be unavailable
    {
      region: "JP",
      locale: "ja",
      touAcceptedDate: SUGGEST_TOU_TIMESTAMP,
      expected: {
        "quicksuggest.online.available": false,
        "quicksuggest.settingsUi": QuickSuggest.SETTINGS_UI.NONE,
        "flightStatus.featureGate": false,
        "market.featureGate": false,
        "sports.featureGate": false,
      },
    },
  ];

  for (let { region, locale, touAcceptedDate, expected } of tests) {
    await doOnlineAvailableTest({
      region,
      locale,
      touAcceptedDate,
      expected,
    });
  }
});

// Online Suggest should become available at the time the user accepts ToU if
// Suggest overall is enabled. Online Suggest should remain unavailable
// otherwise.
add_task(async function onlineAvailable_onToUAccepted() {
  // `QuickSuggest.init` must be called so it adds itself as a `UrlbarPrefs`
  // observer.
  await QuickSuggest.init();

  let tests = [
    {
      region: "US",
      locale: "en-US",
      expectedBefore: {
        "quicksuggest.online.available": false,
        "quicksuggest.settingsUi": QuickSuggest.SETTINGS_UI.OFFLINE_ONLY,
        "flightStatus.featureGate": false,
        "market.featureGate": false,
        "sports.featureGate": false,
      },
      expectedAfter: {
        "quicksuggest.online.available": true,
        "quicksuggest.settingsUi": QuickSuggest.SETTINGS_UI.FULL,
        "flightStatus.featureGate": true,
        "market.featureGate": true,
        "sports.featureGate": true,
      },
    },
    // region/locale where Suggest is not enabled
    {
      region: "JP",
      locale: "ja",
      expectedBefore: {
        "quicksuggest.online.available": false,
        "quicksuggest.settingsUi": QuickSuggest.SETTINGS_UI.NONE,
        "flightStatus.featureGate": false,
        "market.featureGate": false,
        "sports.featureGate": false,
      },
      // same as `expectedBefore`
      expectedAfter: {
        "quicksuggest.online.available": false,
        "quicksuggest.settingsUi": QuickSuggest.SETTINGS_UI.NONE,
        "flightStatus.featureGate": false,
        "market.featureGate": false,
        "sports.featureGate": false,
      },
    },
  ];

  for (let { region, locale, expectedBefore, expectedAfter } of tests) {
    await doOnlineAvailableTest({
      region,
      locale,
      touAcceptedDate: 0,
      expected: expectedBefore,
      callback: async () => {
        info("Setting ToU accepted date");
        Services.prefs.setCharPref(
          TelemetryReportingPolicy.TOU_ACCEPTED_DATE_PREF,
          SUGGEST_TOU_TIMESTAMP
        );
        for (let [name, value] of Object.entries(expectedAfter)) {
          Assert.equal(
            UrlbarPrefs.get(name),
            value,
            "Pref should have expected value after accepting ToU: " + name
          );
        }
      },
    });
  }
});

async function doOnlineAvailableTest({
  touAcceptedDate,
  expected,
  region = "US",
  locale = "en-US",
  callback = null,
}) {
  info(
    "Doing online-available test: " +
      JSON.stringify({
        region,
        locale,
        touAcceptedDate,
        expected,
      })
  );

  // Set the ToU acceptance date.
  Services.prefs.setCharPref(
    TelemetryReportingPolicy.TOU_ACCEPTED_DATE_PREF,
    touAcceptedDate
  );

  await QuickSuggestTestUtils.withRegionAndLocale({
    region,
    locale,
    callback: async () => {
      for (let [name, value] of Object.entries(expected)) {
        Assert.equal(
          UrlbarPrefs.get(name),
          value,
          "Pref should have expected value: " + name
        );
      }
      await callback?.();
    },
  });

  Services.prefs.clearUserPref(TelemetryReportingPolicy.TOU_ACCEPTED_DATE_PREF);
}
