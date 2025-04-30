/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

// Tests that registering an exposureResults pref and triggering a match causes
// the exposure event to be recorded on the UrlbarResults.

ChromeUtils.defineESModuleGetters(this, {
  UrlbarProviderQuickSuggest:
    "resource:///modules/UrlbarProviderQuickSuggest.sys.mjs",
});

add_setup(async function setup() {
  await QuickSuggestTestUtils.ensureQuickSuggestInit({
    remoteSettingsRecords: [
      {
        collection: QuickSuggestTestUtils.RS_COLLECTION.AMP,
        type: QuickSuggestTestUtils.RS_TYPE.AMP,
        attachment: [
          QuickSuggestTestUtils.ampRemoteSettings({
            keywords: ["amp", "amp and wikipedia"],
          }),
        ],
      },
      {
        collection: QuickSuggestTestUtils.RS_COLLECTION.OTHER,
        type: QuickSuggestTestUtils.RS_TYPE.WIKIPEDIA,
        attachment: [
          QuickSuggestTestUtils.wikipediaRemoteSettings({
            keywords: ["wikipedia", "amp and wikipedia"],
          }),
        ],
      },
    ],
    prefs: [
      ["suggest.quicksuggest.nonsponsored", true],
      ["suggest.quicksuggest.sponsored", true],
      ["quicksuggest.ampTopPickCharThreshold", 0],
    ],
  });
});

add_task(async function oneExposureResult_shown_matched() {
  UrlbarPrefs.set("exposureResults", suggestResultType("adm_sponsored"));
  UrlbarPrefs.set("showExposureResults", true);

  let context = createContext("amp", {
    providers: [UrlbarProviderQuickSuggest.name],
    isPrivate: false,
  });

  await check_results({
    context,
    matches: [
      {
        ...QuickSuggestTestUtils.ampResult(),
        exposureTelemetry: UrlbarUtils.EXPOSURE_TELEMETRY.SHOWN,
      },
    ],
  });
});

add_task(async function oneExposureResult_shown_notMatched() {
  UrlbarPrefs.set("exposureResults", suggestResultType("adm_sponsored"));
  UrlbarPrefs.set("showExposureResults", true);

  let context = createContext("wikipedia", {
    providers: [UrlbarProviderQuickSuggest.name],
    isPrivate: false,
  });

  await check_results({
    context,
    matches: [
      {
        ...QuickSuggestTestUtils.wikipediaResult(),
        exposureTelemetry: UrlbarUtils.EXPOSURE_TELEMETRY.NONE,
      },
    ],
  });
});

add_task(async function oneExposureResult_hidden_matched() {
  UrlbarPrefs.set("exposureResults", suggestResultType("adm_sponsored"));
  UrlbarPrefs.set("showExposureResults", false);

  let context = createContext("amp", {
    providers: [UrlbarProviderQuickSuggest.name],
    isPrivate: false,
  });

  await check_results({
    context,
    matches: [
      {
        ...QuickSuggestTestUtils.ampResult(),
        exposureTelemetry: UrlbarUtils.EXPOSURE_TELEMETRY.HIDDEN,
      },
    ],
  });
});

add_task(async function oneExposureResult_hidden_notMatched() {
  UrlbarPrefs.set("exposureResults", suggestResultType("adm_sponsored"));
  UrlbarPrefs.set("showExposureResults", false);

  let context = createContext("wikipedia", {
    providers: [UrlbarProviderQuickSuggest.name],
    isPrivate: false,
  });

  await check_results({
    context,
    matches: [
      {
        ...QuickSuggestTestUtils.wikipediaResult(),
        exposureTelemetry: UrlbarUtils.EXPOSURE_TELEMETRY.NONE,
      },
    ],
  });
});

add_task(async function manyExposureResults_shown_oneMatched_1() {
  UrlbarPrefs.set(
    "exposureResults",
    [
      suggestResultType("adm_sponsored"),
      suggestResultType("adm_nonsponsored"),
    ].join(",")
  );
  UrlbarPrefs.set("showExposureResults", true);

  let context = createContext("amp", {
    providers: [UrlbarProviderQuickSuggest.name],
    isPrivate: false,
  });
  await check_results({
    context,
    matches: [
      {
        ...QuickSuggestTestUtils.ampResult(),
        exposureTelemetry: UrlbarUtils.EXPOSURE_TELEMETRY.SHOWN,
      },
    ],
  });
});

add_task(async function manyExposureResults_shown_oneMatched_2() {
  UrlbarPrefs.set(
    "exposureResults",
    [
      suggestResultType("adm_sponsored"),
      suggestResultType("adm_nonsponsored"),
    ].join(",")
  );
  UrlbarPrefs.set("showExposureResults", true);

  let context = createContext("wikipedia", {
    providers: [UrlbarProviderQuickSuggest.name],
    isPrivate: false,
  });
  await check_results({
    context,
    matches: [
      {
        ...QuickSuggestTestUtils.wikipediaResult(),
        exposureTelemetry: UrlbarUtils.EXPOSURE_TELEMETRY.SHOWN,
      },
    ],
  });
});

add_task(async function manyExposureResults_shown_manyMatched() {
  UrlbarPrefs.set(
    "exposureResults",
    [
      suggestResultType("adm_sponsored"),
      suggestResultType("adm_nonsponsored"),
    ].join(",")
  );
  UrlbarPrefs.set("showExposureResults", true);

  let keyword = "amp and wikipedia";
  let context = createContext(keyword, {
    providers: [UrlbarProviderQuickSuggest.name],
    isPrivate: false,
  });

  // Only one result should be added since exposures are shown and at most one
  // Suggest result should be shown.
  await check_results({
    context,
    matches: [
      {
        ...QuickSuggestTestUtils.ampResult({ keyword }),
        exposureTelemetry: UrlbarUtils.EXPOSURE_TELEMETRY.SHOWN,
      },
    ],
  });
});

add_task(async function manyExposureResults_hidden_oneMatched_1() {
  UrlbarPrefs.set(
    "exposureResults",
    [
      suggestResultType("adm_sponsored"),
      suggestResultType("adm_nonsponsored"),
    ].join(",")
  );
  UrlbarPrefs.set("showExposureResults", false);

  let context = createContext("amp", {
    providers: [UrlbarProviderQuickSuggest.name],
    isPrivate: false,
  });
  await check_results({
    context,
    matches: [
      {
        ...QuickSuggestTestUtils.ampResult(),
        exposureTelemetry: UrlbarUtils.EXPOSURE_TELEMETRY.HIDDEN,
      },
    ],
  });
});

add_task(async function manyExposureResults_hidden_oneMatched_2() {
  UrlbarPrefs.set(
    "exposureResults",
    [
      suggestResultType("adm_sponsored"),
      suggestResultType("adm_nonsponsored"),
    ].join(",")
  );
  UrlbarPrefs.set("showExposureResults", false);

  let context = createContext("wikipedia", {
    providers: [UrlbarProviderQuickSuggest.name],
    isPrivate: false,
  });
  await check_results({
    context,
    matches: [
      {
        ...QuickSuggestTestUtils.wikipediaResult(),
        exposureTelemetry: UrlbarUtils.EXPOSURE_TELEMETRY.HIDDEN,
      },
    ],
  });
});

add_task(async function manyExposureResults_hidden_manyMatched() {
  UrlbarPrefs.set(
    "exposureResults",
    [
      suggestResultType("adm_sponsored"),
      suggestResultType("adm_nonsponsored"),
    ].join(",")
  );
  UrlbarPrefs.set("showExposureResults", false);

  let keyword = "amp and wikipedia";
  let context = createContext(keyword, {
    providers: [UrlbarProviderQuickSuggest.name],
    isPrivate: false,
  });

  // Both results should be added since exposures are hidden and there's no
  // limit on the number of hidden-exposure Suggest results.
  await check_results({
    context,
    matches: [
      {
        ...QuickSuggestTestUtils.ampResult({ keyword }),
        exposureTelemetry: UrlbarUtils.EXPOSURE_TELEMETRY.HIDDEN,
      },
      {
        ...QuickSuggestTestUtils.wikipediaResult({ keyword }),
        exposureTelemetry: UrlbarUtils.EXPOSURE_TELEMETRY.HIDDEN,
      },
    ],
  });
});

function suggestResultType(typeWithoutSource) {
  return `rust_${typeWithoutSource}`;
}
