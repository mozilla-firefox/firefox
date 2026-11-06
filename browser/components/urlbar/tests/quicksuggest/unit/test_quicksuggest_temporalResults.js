/* Any copyright is dedicated to the Public Domain.
 * https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Tests temporal search suggestion results in the zero prefix view.

ChromeUtils.defineESModuleGetters(this, {
  NimbusFeatures: "resource://nimbus/ExperimentAPI.sys.mjs",
  TelemetryReportingPolicy:
    "resource://gre/modules/TelemetryReportingPolicy.sys.mjs",
});

const QUERIES = ["q1", "q2", "q3", "q4", "q5", "q6", "q7"];

let gEngine;
let gFeature;

add_setup(async function () {
  gEngine = await addTestSuggestionsEngine();
  let oldDefaultEngine = SearchService.defaultEngine;
  await SearchService.setDefault(
    gEngine,
    SearchService.CHANGE_REASON.ADDON_INSTALL
  );

  await QuickSuggestTestUtils.ensureQuickSuggestInit({
    remoteSettingsRecords: [makeRemoteSettingsRecord()],
    prefs: [["temporal.featureGate", true]],
  });
  gFeature = QuickSuggest.getFeature("TemporalSuggestions");
  // Always pick the first remaining query so results are deterministic.
  let randomStub = sinon.stub(gFeature, "_random").returns(0);

  UrlbarPrefs.set("suggest.searches", true);
  UrlbarPrefs.set("suggest.topsites", false);
  UrlbarTestUtils.stubNowZonedDateTime("2026-11-15T12:00[America/New_York]");

  registerCleanupFunction(async () => {
    UrlbarPrefs.clear("suggest.searches");
    UrlbarPrefs.clear("suggest.topsites");
    UrlbarTestUtils.stubNowZonedDateTime(null);
    gFeature._reset();
    randomStub.restore();
    await SearchService.setDefault(
      oldDefaultEngine,
      SearchService.CHANGE_REASON.ADDON_INSTALL
    );
  });
});

function makeData(query, overrides = {}) {
  return {
    group: "test-group",
    title: "Test group",
    type: "seasonal",
    startDate: "2026-11-12",
    endDate: "2026-11-27",
    engines: [gEngine.id],
    query,
    ...overrides,
  };
}

function makeRemoteSettingsRecord(overrides = {}) {
  return {
    type: "dynamic-suggestions",
    suggestion_type: "temporal",
    attachment: QUERIES.map(query => ({
      keywords: [""],
      dismissal_key: query,
      data: makeData(query, overrides),
    })),
  };
}

async function setRecords(overrides = {}) {
  await QuickSuggestTestUtils.setRemoteSettingsRecords([
    makeRemoteSettingsRecord(overrides),
  ]);
}

function newController() {
  let controller = UrlbarTestUtils.mockChildController();
  controller.setView({
    visibleResults: [],
    controller: {
      removeResult() {},
    },
    clearTopSitesCache() {},
  });
  return controller;
}

function makeExpectedResult(query) {
  let result = makeSearchResult(createContext("", { isPrivate: false }), {
    suggestion: query,
    query: null,
    engineName: gEngine.name,
    engineIconUri: UrlbarShared.ICON.SEARCH_GLASS,
    providerName: UrlbarProviderQuickSuggest.name,
  });
  Object.assign(result.payload, {
    source: "rust",
    provider: "Dynamic",
    suggestionType: "temporal",
    telemetryType: "temporal",
    isSponsored: false,
  });
  return result;
}

/**
 * Does a query and checks that it returns temporal results for the expected
 * queries.
 *
 * @param {string[]} expectedQueries
 *   The expected queries in pick order.
 * @param {object} [options]
 * @param {string} [options.searchString]
 * @param {object} [options.properties]
 *   Additional properties for the query context.
 */
async function checkQuery(
  expectedQueries,
  { searchString = "", properties = {} } = {}
) {
  let context = createContext(searchString, {
    providers: [UrlbarProviderQuickSuggest.name, "UrlbarProviderTopSites"],
    isPrivate: false,
    ...properties,
  });
  await check_results({
    context,
    matches: expectedQueries.map(makeExpectedResult),
  });
  for (let result of context.results) {
    Assert.deepEqual(
      result.rowLabel,
      { id: "urlbar-group-temporal-seasonal" },
      "Row label"
    );
    Assert.deepEqual(
      result.commands,
      [
        {
          name: "dismiss",
          l10n: { id: "urlbar-result-menu-dismiss-suggestion2" },
        },
        {
          name: "not_interested",
          l10n: { id: "urlbar-result-menu-dont-show-temporal-seasonal" },
        },
      ],
      "Commands"
    );
  }
  return context;
}

// A zero prefix query returns `temporal.rowCount` temporal search results.
add_task(async function results() {
  gFeature._reset();
  await checkQuery(["q1", "q2", "q3", "q4", "q5"]);

  gFeature._reset();
  UrlbarPrefs.set("temporal.rowCount", 3);
  await checkQuery(["q1", "q2", "q3"]);
  UrlbarPrefs.clear("temporal.rowCount");
});

// Each zero prefix query picks queries other than the ones picked by the
// previous queries until every query has been picked, and then picking starts
// over.
add_task(async function rotation() {
  gFeature._reset();
  await checkQuery(["q1", "q2", "q3", "q4", "q5"]);
  await checkQuery(["q6", "q7", "q1", "q2", "q3"]);
  await checkQuery(["q4", "q5", "q1", "q2", "q3"]);
});

// No results are returned when there's a search string, in private windows,
// when a pref disables the feature, or when no group is eligible.
add_task(async function notShown() {
  for (let [searchString, properties] of [
    ["foo", {}],
    [" ", {}],
    ["", { isPrivate: true }],
  ]) {
    info(`Checking: ${JSON.stringify({ searchString, properties })}`);
    gFeature._reset();
    await checkQuery([], { searchString, properties });
  }

  for (let [pref, value] of [
    ["temporal.featureGate", false],
    ["suggest.temporal", false],
    ["suggest.temporal.seasonal", false],
    ["suggest.searches", false],
    ["browser.search.suggest.enabled", false],
  ]) {
    gFeature._reset();
    info(`Checking ${pref} = ${value}`);
    UrlbarPrefs.set(pref, value);
    await checkQuery([]);
    UrlbarPrefs.clear(pref);
  }
  UrlbarPrefs.set("temporal.featureGate", true);
  UrlbarPrefs.set("suggest.searches", true);
  await QuickSuggestTestUtils.forceSync();
  gFeature._reset();
  await checkQuery(["q1", "q2", "q3", "q4", "q5"]);

  info("Engine mismatch");
  await setRecords({ engines: ["google"] });
  await checkQuery([]);

  info("Group not started");
  await setRecords({ startDate: "2026-11-20" });
  await checkQuery([]);

  info("No records");
  await QuickSuggestTestUtils.setRemoteSettingsRecords([]);
  await checkQuery([]);

  await setRecords();
});

// Enrolling with the `temporalFeatureGate` Nimbus variable sets
// `temporal.featureGate`, which enables temporal results and the zero prefix
// layout.
add_task(async function nimbus() {
  UrlbarPrefs.clear("temporal.featureGate");
  await QuickSuggestTestUtils.forceSync();
  gFeature._reset();
  Assert.ok(!gFeature.isEnabled, "Feature is disabled before enrollment");
  await checkQuery([]);

  let cleanUpNimbus = await UrlbarTestUtils.initNimbusFeature(
    { temporalFeatureGate: true },
    { isRollout: false }
  );
  await QuickSuggestTestUtils.forceSync();
  Assert.ok(
    UrlbarPrefs.get("temporal.featureGate"),
    "Enrollment sets temporal.featureGate"
  );
  Assert.ok(gFeature.isEnabled, "Feature is enabled after enrollment");
  gFeature._reset();
  let context = await checkQuery(["q1", "q2", "q3", "q4", "q5"]);
  Assert.equal(
    context.maxResultsActual,
    UrlbarPrefs.get("temporal.zeroPrefixMaxRichResults"),
    "Zero prefix layout is used after enrollment"
  );

  await cleanUpNimbus();
  await QuickSuggestTestUtils.forceSync();
  Assert.ok(
    !UrlbarPrefs.get("temporal.featureGate"),
    "Unenrollment restores temporal.featureGate"
  );
  Assert.ok(!gFeature.isEnabled, "Feature is disabled after unenrollment");
  gFeature._reset();
  await checkQuery([]);

  UrlbarPrefs.set("temporal.featureGate", true);
  await QuickSuggestTestUtils.forceSync();
});

// Temporal suggestions are part of Suggest, so they aren't shown when
// `suggest.quicksuggest.all` is false.
add_task(async function suggestAllDisabled() {
  gFeature._reset();
  await checkQuery(["q1", "q2", "q3", "q4", "q5"]);

  UrlbarPrefs.set("suggest.quicksuggest.all", false);
  Assert.ok(!gFeature.isEnabled, "Feature is disabled");
  gFeature._reset();
  await checkQuery([]);

  UrlbarPrefs.set("suggest.quicksuggest.all", true);
  await QuickSuggestTestUtils.forceSync();
  Assert.ok(gFeature.isEnabled, "Feature is enabled again");
  gFeature._reset();
  await checkQuery(["q1", "q2", "q3", "q4", "q5"]);
});

// Calls `makeResult()` for a temporal suggestion with the given context.
function makeResultForContext(context) {
  return gFeature.makeResult(context, {
    engine: gEngine.name,
    data: makeData("q1"),
  });
}

// `makeResult()` returns a result for a zero prefix query context.
add_task(async function makeResult() {
  let context = createContext("", { isPrivate: false });
  Assert.ok(await makeResultForContext(context), "Returns a result");
});

// `makeResult()` returns null when the search string is only whitespace.
add_task(async function makeResult_whitespace() {
  let context = createContext(" ", { isPrivate: false });
  Assert.equal(await makeResultForContext(context), null);
});

// `makeResult()` returns null outside the urlbar SAP.
add_task(async function makeResult_smartbar() {
  let context = createContext("", { isPrivate: false, sapName: "smartbar" });
  Assert.equal(await makeResultForContext(context), null);
});

// `makeResult()` returns null in search mode for the default engine.
add_task(async function makeResult_searchMode_default() {
  let context = createContext("", {
    isPrivate: false,
    searchMode: { engineName: gEngine.name },
  });
  Assert.equal(await makeResultForContext(context), null);
});

// `makeResult()` returns null in search mode for other engines.
add_task(async function makeResult_searchMode_nonDefault() {
  let context = createContext("", {
    isPrivate: false,
    searchMode: { engineName: "Other engine" },
  });
  Assert.equal(await makeResultForContext(context), null);
});

// `makeResult()` returns null in a local search mode.
add_task(async function makeResult_searchMode_local() {
  let context = createContext("", {
    isPrivate: false,
    searchMode: { source: UrlbarShared.RESULT_SOURCE.BOOKMARKS },
  });
  Assert.equal(await makeResultForContext(context), null);
});

// `makeResult()` returns a result when results are restricted to search.
add_task(async function makeResult_restrictSource() {
  let context = createContext("", { isPrivate: false });
  context.restrictSource = UrlbarShared.RESULT_SOURCE.SEARCH;
  Assert.ok(await makeResultForContext(context), "Returns a result");
});

// `makeResult()` returns a result when the query has the search restriction
// token.
add_task(async function makeResult_restrictToken() {
  let context = createContext("", { isPrivate: false });
  context.restrictToken = { value: "?" };
  Assert.ok(await makeResultForContext(context), "Returns a result");
});

// The dismiss command dismisses the query, which is then filtered out by the
// Rust component, and the remaining queries are still shown.
add_task(async function dismissal() {
  gFeature._reset();
  UrlbarPrefs.set("temporal.rowCount", QUERIES.length);
  let context = createContext("", {
    providers: [UrlbarProviderQuickSuggest.name, "UrlbarProviderTopSites"],
    isPrivate: false,
  });
  await newController().startQuery(context);
  let result = context.results.find(r => r.payload.suggestion == "q1");
  Assert.ok(result, "q1 is shown before dismissal");

  let changedPromise = TestUtils.topicObserved(
    "quicksuggest-dismissals-changed"
  );
  triggerCommand({
    feature: gFeature,
    command: "dismiss",
    result,
    expectedCountsByCall: { removeResult: 1 },
  });
  await changedPromise;
  Assert.ok(await QuickSuggest.isResultDismissed(result), "q1 is dismissed");

  gFeature._reset();
  await checkQuery(QUERIES.filter(q => q != "q1"));

  await QuickSuggest.clearDismissedSuggestions();
  UrlbarPrefs.clear("temporal.rowCount");
});

// The group is shown from the start of its first local day through the end of
// its last local day.
add_task(async function dateBoundaries() {
  for (let [now, expected] of [
    ["2026-11-11T23:59[America/New_York]", []],
    ["2026-11-12T00:00[America/New_York]", ["q1", "q2", "q3", "q4", "q5"]],
    ["2026-11-27T23:59[America/New_York]", ["q1", "q2", "q3", "q4", "q5"]],
    ["2026-11-28T00:00[America/New_York]", []],
  ]) {
    info(`Checking now: ${now}`);
    gFeature._reset();
    UrlbarTestUtils.stubNowZonedDateTime(now);
    await checkQuery(expected);
  }
  UrlbarTestUtils.stubNowZonedDateTime("2026-11-15T12:00[America/New_York]");
});

add_task(async function notInterested() {
  gFeature._reset();
  let context = createContext("", {
    providers: [UrlbarProviderQuickSuggest.name, "UrlbarProviderTopSites"],
    isPrivate: false,
  });
  let controller = newController();
  await controller.startQuery(context);
  let results = context.results;
  Assert.equal(results.length, 5, "Has results");

  let parentController = controller.parentController;
  let removeResultStub = sinon.stub(parentController, "removeResult");
  let engaged = results[1];
  gFeature.onEngagement(context, parentController, {
    result: engaged,
    selType: "not_interested",
  });

  Assert.ok(
    !UrlbarPrefs.get("suggest.temporal.seasonal"),
    "suggest.temporal.seasonal is false"
  );
  Assert.ok(UrlbarPrefs.get("suggest.temporal"), "suggest.temporal is true");
  Assert.equal(
    removeResultStub.callCount,
    results.length,
    "All results are removed"
  );
  let lastCall = removeResultStub.lastCall;
  Assert.equal(lastCall.args[0], engaged, "Engaged result is removed last");
  Assert.deepEqual(
    lastCall.args[1],
    {
      acknowledgeDismissalL10n: {
        id: "urlbar-temporal-seasonal-dismissal-acknowledgment",
      },
    },
    "Acknowledgment is shown for the engaged result"
  );
  for (let call of removeResultStub.getCalls().slice(0, -1)) {
    Assert.notEqual(call.args[0], engaged, "Other result");
    Assert.equal(call.args[1], undefined, "No acknowledgment");
  }

  removeResultStub.restore();
  UrlbarPrefs.clear("suggest.temporal.seasonal");
  await QuickSuggestTestUtils.forceSync();
});
