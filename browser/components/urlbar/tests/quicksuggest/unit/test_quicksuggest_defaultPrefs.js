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

const EN_LOCALES = ["en-CA", "en-GB", "en-US", "en-ZA"];

// Expected prefs when Suggest is disabled.
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
  "weather.featureGate": false,
  "wikipedia.featureGate": false,
  "yelp.featureGate": false,
};

// Base set of expected prefs for native locales in regions where Suggest is
// enabled, e.g., the `en-US` locale in the U.S., the `de` locale in Germany.
// Countries may have slightly different actual expected prefs, which is why
// this is a base set.
const EXPECTED_PREFS_BASE_NATIVE = {
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

// Expected prefs for `en` locales in EU countries (e.g., `en-US` locale in
// Germany).
const EXPECTED_PREFS_EU_EN = {
  ...EXPECTED_PREFS_SUGGEST_DISABLED,
  "quicksuggest.enabled": true,
  "importantDates.featureGate": true,
};

// Region -> locale -> expected prefs when Suggest is enabled
const EXPECTED_PREFS_BY_LOCALE_BY_REGION = {
  DE: {
    de: EXPECTED_PREFS_BASE_NATIVE,
    ...Object.fromEntries(
      EN_LOCALES.map(locale => [locale, EXPECTED_PREFS_EU_EN])
    ),
  },
  FR: {
    fr: EXPECTED_PREFS_BASE_NATIVE,
    ...Object.fromEntries(
      EN_LOCALES.map(locale => [locale, EXPECTED_PREFS_EU_EN])
    ),
  },
  GB: Object.fromEntries(
    EN_LOCALES.map(locale => [locale, EXPECTED_PREFS_BASE_NATIVE])
  ),
  IT: {
    it: EXPECTED_PREFS_BASE_NATIVE,
    ...Object.fromEntries(
      EN_LOCALES.map(locale => [locale, EXPECTED_PREFS_EU_EN])
    ),
  },
  US: Object.fromEntries(
    EN_LOCALES.map(locale => [
      locale,
      {
        ...EXPECTED_PREFS_BASE_NATIVE,
        "addons.featureGate": true,
        "mdn.featureGate": true,
        "yelp.featureGate": true,
      },
    ])
  ),
};

add_setup(async () => {
  await UrlbarTestUtils.initNimbusFeature();
});

add_task(async function primary() {
  let tests = [
    // Regions/locales where Suggest should be enabled to some extent
    { region: "DE", locale: "de" },
    { region: "DE", locale: "en-GB" },
    { region: "DE", locale: "en-US" },

    { region: "FR", locale: "fr" },
    { region: "FR", locale: "en-GB" },
    { region: "FR", locale: "en-US" },

    { region: "GB", locale: "en-US" },
    { region: "GB", locale: "en-CA" },
    { region: "GB", locale: "en-GB" },

    { region: "IT", locale: "it" },
    { region: "IT", locale: "en-GB" },
    { region: "IT", locale: "en-US" },

    { region: "US", locale: "en-US" },
    { region: "US", locale: "en-CA" },
    { region: "US", locale: "en-GB" },

    // Regions/locales where Suggest should be completely disabled
    { region: "CA", locale: "en-US" },
    { region: "CA", locale: "en-CA" },
    { region: "GB", locale: "de" },
    { region: "US", locale: "de" },
  ];

  for (let { locale, region } of tests) {
    await doPrimaryTest({ locale, region });
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
 */
async function doPrimaryTest({ locale, region }) {
  let expectedPrefs =
    EXPECTED_PREFS_BY_LOCALE_BY_REGION[region]?.[locale] ??
    EXPECTED_PREFS_SUGGEST_DISABLED;

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
