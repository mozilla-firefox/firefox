/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Tests disable event telemetry.
 */

let currentTime;

const DISABLE_SUGGEST_EVENT_MAX_SECONDS = 300;
const ENGINE_ID =
  "other-browser_searchSuggestionEngine searchSuggestionEngine.xml";

ChromeUtils.defineESModuleGetters(lazy, {
  QuickSuggestTestUtils:
    "resource://testing-common/QuickSuggestTestUtils.sys.mjs",
  UrlbarSession: "moz-src:///browser/components/urlbar/UrlbarSession.sys.mjs",
});

add_setup(async function () {
  Services.prefs.setBoolPref("browser.search.suggest.enabled", false);

  await lazy.QuickSuggestTestUtils.ensureQuickSuggestInit({
    remoteSettingsRecords: [
      {
        collection: lazy.QuickSuggestTestUtils.RS_COLLECTION.AMP,
        type: lazy.QuickSuggestTestUtils.RS_TYPE.AMP,
        attachment: [lazy.QuickSuggestTestUtils.ampRemoteSettings()],
      },
      {
        collection: lazy.QuickSuggestTestUtils.RS_COLLECTION.OTHER,
        type: lazy.QuickSuggestTestUtils.RS_TYPE.WIKIPEDIA,
        attachment: [lazy.QuickSuggestTestUtils.wikipediaRemoteSettings()],
      },
    ],
    prefs: [["suggest.quicksuggest.sponsored", true]],
  });

  UrlbarPrefs.set(
    "events.disableSuggest.maxSecondsFromLastSearch",
    DISABLE_SUGGEST_EVENT_MAX_SECONDS
  );

  let oldDefaultEngine = await SearchService.getDefault();

  let root = gTestPath;
  let engineURL = new URL("../browser/searchSuggestionEngine.xml", root).href;

  await SearchTestUtils.installOpenSearchEngine({
    url: engineURL,
    setAsDefault: true,
  });

  // The disable-suggest threshold clock lives on the parent recorder; stub the
  // class so it's controllable whether the recorder is reached in-process or
  // over the actor.
  sinon.stub(lazy.UrlbarSession.prototype, "getCurrentTime").callsFake(() => {
    return currentTime;
  });

  await SpecialPowers.pushPrefEnv({
    set: [["browser.urlbar.scotchBonnet.enableOverride", true]],
  });

  registerCleanupFunction(async function () {
    await SearchService.setDefault(
      oldDefaultEngine,
      SearchService.CHANGE_REASON.UNKNOWN
    );
    sinon.restore();
    Services.prefs.clearUserPref("browser.search.suggest.enabled");
  });
});

function resetClock() {
  currentTime = 0;
}

function advanceClock(ms) {
  currentTime += ms;
}

function getSuggestPref(type) {
  if (type == "sponsored") {
    return "suggest.quicksuggest.sponsored";
  } else if (type == "nonsponsored") {
    return "suggest.quicksuggest.all";
  } else if (type == "suggest") {
    return "quicksuggest.enabled";
  }
  return null;
}

const TESTS = [
  {
    input: "amp",
    expectedResults: "search_engine,rust_adm_sponsored",
    expectedNumResults: 2,
    expectedSelectedResult: "rust_adm_sponsored",
    pref: "sponsored",
    time: 5000,
  },
  {
    input: "amp",
    expectedResults: "search_engine,rust_adm_sponsored",
    expectedNumResults: 2,
    expectedSelectedResult: "rust_adm_sponsored",
    pref: "sponsored",
    time: 500000,
  },
  {
    input: "wikipedia",
    expectedResults: "search_engine,action,rust_adm_nonsponsored",
    expectedNumResults: 3,
    expectedSelectedResult: "rust_adm_nonsponsored",
    pref: "nonsponsored",
    time: 10000,
  },
  {
    input: "amp",
    expectedResults: "search_engine,rust_adm_sponsored",
    expectedNumResults: 2,
    expectedSelectedResult: "rust_adm_sponsored",
    pref: "suggest",
    time: 5000,
  },
  {
    input: "wikipedia",
    expectedResults: "search_engine,action,rust_adm_nonsponsored",
    expectedNumResults: 3,
    expectedSelectedResult: "rust_adm_nonsponsored",
    pref: "suggest",
    time: 10000,
  },
];

add_task(async function test_disable_suggest_after_engagement() {
  for (const test of TESTS) {
    for (let engagedWithSuggest of [true, false]) {
      await doTest(async () => {
        resetClock();
        let tab = await BrowserTestUtils.openNewForegroundTab(window.gBrowser);

        await openPopup(test.input);

        if (engagedWithSuggest) {
          EventUtils.synthesizeKey("KEY_ArrowDown");
        }

        let selected_result = engagedWithSuggest
          ? test.expectedSelectedResult
          : "search_engine";

        await doClick();

        await assertEngagementTelemetry([
          {
            results: test.expectedResults,
            n_results: test.expectedNumResults,
            selected_result,
          },
        ]);

        advanceClock(test.time);

        UrlbarPrefs.set(getSuggestPref(test.pref), false);

        if (test.time < DISABLE_SUGGEST_EVENT_MAX_SECONDS * 1000) {
          assertDisableTelemetry([
            {
              feature: "suggest",
              selected_result,
              results: test.expectedResults,
              n_results: test.expectedNumResults,
              sap: "urlbar",
              interaction: "typed",
              search_mode: "",
              search_engine_default_id: ENGINE_ID,
              n_chars: test.input.length,
              n_words: "1",
              window_mode: "classic",
            },
          ]);
        } else {
          assertDisableTelemetry([]);
        }

        UrlbarPrefs.set(getSuggestPref(test.pref), true);

        BrowserTestUtils.removeTab(tab);
      });
    }
  }
});

add_task(async function test_disable_suggest_after_abandonment() {
  for (const test of TESTS) {
    await doTest(async () => {
      resetClock();
      let tab = await BrowserTestUtils.openNewForegroundTab(window.gBrowser);

      await openPopup(test.input);

      await doBlur();

      await assertAbandonmentTelemetry([
        {
          selectedResult: null,
          results: test.expectedResults,
          n_results: test.expectedNumResults,
        },
      ]);

      advanceClock(test.time);

      UrlbarPrefs.set(getSuggestPref(test.pref), false);

      if (test.time < DISABLE_SUGGEST_EVENT_MAX_SECONDS * 1000) {
        assertDisableTelemetry([
          {
            feature: "suggest",
            selected_result: "none",
            results: test.expectedResults,
            n_results: test.expectedNumResults,
            sap: "urlbar_newtab",
            interaction: "typed",
            search_mode: "",
            search_engine_default_id: ENGINE_ID,
            n_chars: test.input.length,
            n_words: "1",
            window_mode: "classic",
          },
        ]);
      } else {
        assertDisableTelemetry([]);
      }

      UrlbarPrefs.set(getSuggestPref(test.pref), true);
      BrowserTestUtils.removeTab(tab);
    });
  }
});
