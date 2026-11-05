/* Any copyright is dedicated to the Public Domain.
 * https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Tests validation, eligibility and selection of temporal search suggestions.

const TODAY = Temporal.PlainDate.from("2026-11-15");

let gFeature;
let gRandomStub;

add_setup(async function () {
  await QuickSuggestTestUtils.ensureQuickSuggestInit();
  gFeature = QuickSuggest.getFeature("TemporalSuggestions");
  // Selection picks a random index into the queries not yet picked. Returning
  // 0 always picks the first one, so queries are picked in the given order and
  // the expected picks are deterministic.
  gRandomStub = sinon.stub(gFeature, "_random").returns(0);
  registerCleanupFunction(() => {
    gRandomStub.restore();
    gFeature._reset();
  });
});

// Returns valid suggestion data, eligible on `TODAY` for the "google" engine,
// with `overrides` applied.
function makeData(overrides = {}) {
  return {
    group: "test-group",
    title: "Test group",
    type: "seasonal",
    startDate: "2026-11-12",
    endDate: "2026-11-27",
    engines: ["google"],
    query: "one",
    ...overrides,
  };
}

// Wraps data as a dynamic Rust suggestion.
function makeSuggestion(data) {
  return {
    source: "rust",
    provider: "Dynamic",
    suggestionType: "temporal",
    dismissalKey: data.query,
    score: 0.3,
    data,
  };
}

function eligible(data, today = TODAY, engineId = "google") {
  return gFeature.isEligible(data, {
    today: Temporal.PlainDate.from(today),
    engineId,
  });
}

// Returns `count` queries named q1, q2, etc.
function makeQueries(count) {
  return Array.from({ length: count }, (_, i) => `q${i + 1}`);
}

add_task(async function invalidData() {
  let invalid = [
    { type: "time-of-day" },
    { type: undefined },
    { group: "" },
    { group: undefined },
    { title: "" },
    { title: 1 },
    { query: "" },
    { query: " " },
    { query: undefined },
    { startDate: undefined },
    { startDate: "2026-13-01" },
    { startDate: "2026-02-30" },
    { startDate: "11/12/2026" },
    { startDate: "2026-11-28" },
    { engines: [] },
    { engines: "google" },
    { engines: [""] },
  ];

  Assert.ok(eligible(makeData()), "The unmodified data is valid");
  for (let overrides of invalid) {
    info("Checking invalid data: " + JSON.stringify(overrides));
    Assert.ok(!eligible(makeData(overrides)), "Data is ignored");
  }
  Assert.ok(!eligible(null), "Null data is ignored");
});

add_task(async function dateBoundaries() {
  let data = makeData();
  Assert.ok(!eligible(data, "2026-11-11"), "Day before start");
  Assert.ok(eligible(data, "2026-11-12"), "Start day");
  Assert.ok(eligible(data, "2026-11-27"), "End day");
  Assert.ok(!eligible(data, "2026-11-28"), "Day after end");

  Assert.ok(
    eligible(
      makeData({ startDate: "2026-11-12", endDate: "2026-11-12" }),
      "2026-11-12"
    ),
    "Single-day group"
  );
});

add_task(async function engines() {
  let data = makeData({ engines: ["bing", "google"] });
  Assert.ok(eligible(data, TODAY, "google"), "First engine");
  Assert.ok(eligible(data, TODAY, "bing"), "Second engine");
  Assert.ok(!eligible(data, TODAY, "ddg"), "Mismatched engine");
  Assert.ok(
    !eligible(data, TODAY, "5f1e8c4b-0d4a-4b5f-9c1e-3a7c2b9d8e6f"),
    "Non-config engine"
  );
});

// Data isn't eligible when the pref for its type is disabled.
add_task(async function typePref() {
  UrlbarPrefs.set("suggest.temporal.seasonal", false);
  Assert.ok(!eligible(makeData()), "Type pref disabled");
  UrlbarPrefs.clear("suggest.temporal.seasonal");
  Assert.ok(eligible(makeData()), "Type pref enabled");
});

// Queries already picked are skipped until every query has been picked. Then
// picking starts over, without repeating queries within the same pick.
add_task(async function rotation() {
  gFeature._reset();
  let queries = makeQueries(7);
  Assert.deepEqual(
    gFeature.selectQueries(queries, 3),
    ["q1", "q2", "q3"],
    "First pick"
  );
  Assert.deepEqual(
    gFeature.selectQueries(queries, 3),
    ["q4", "q5", "q6"],
    "Picked queries are skipped"
  );
  Assert.deepEqual(
    gFeature.selectQueries(queries, 3),
    ["q7", "q1", "q2"],
    "Picking starts over once every query has been picked"
  );
  Assert.deepEqual(
    gFeature.selectQueries(queries, 3),
    ["q3", "q4", "q5"],
    "Queries picked after starting over are skipped"
  );
});

// Fewer queries than the count returns all the queries each time.
add_task(async function fewerQueriesThanCount() {
  gFeature._reset();
  let queries = makeQueries(2);
  Assert.deepEqual(
    gFeature.selectQueries(queries, 3),
    ["q1", "q2"],
    "First pick"
  );
  Assert.deepEqual(
    gFeature.selectQueries(queries, 3),
    ["q1", "q2"],
    "Second pick"
  );
});

// Queries are returned in the order they were randomly picked, not the order
// they were given in.
add_task(async function pickOrder() {
  gFeature._reset();
  // Always pick the last remaining query.
  gRandomStub.returns(0.99);
  Assert.deepEqual(
    gFeature.selectQueries(makeQueries(5), 3),
    ["q5", "q4", "q3"],
    "Queries are in pick order"
  );
  gRandomStub.returns(0);
});

// No more than the requested count is picked.
add_task(async function count() {
  gFeature._reset();
  let queries = makeQueries(5);
  Assert.deepEqual(gFeature.selectQueries(queries, 1), ["q1"], "Count of 1");
  Assert.deepEqual(
    gFeature.selectQueries(queries, 10),
    ["q2", "q3", "q4", "q5", "q1"],
    "Count larger than the number of queries"
  );
});

// `filterSuggestions()` returns the suggestions of picked queries of the first
// eligible group, with the default engine set.
add_task(async function filterSuggestions() {
  gFeature._reset();
  let engine = await addTestSuggestionsEngine();
  let oldDefaultEngine = SearchService.defaultEngine;
  await SearchService.setDefault(
    engine,
    SearchService.CHANGE_REASON.ADDON_INSTALL
  );
  UrlbarTestUtils.stubNowZonedDateTime("2026-11-15T12:00[America/New_York]");

  let suggestions = [
    makeSuggestion(
      makeData({ engines: [engine.id], query: "invalid", type: "x" })
    ),
    ...makeQueries(7).map(query =>
      makeSuggestion(makeData({ engines: [engine.id], query }))
    ),
    makeSuggestion(
      makeData({ group: "other-group", engines: [engine.id], query: "other" })
    ),
  ];
  let filtered = await gFeature.filterSuggestions(suggestions);
  Assert.deepEqual(
    filtered.map(s => s.data.query),
    ["q1", "q2", "q3", "q4", "q5"],
    "rowCount queries of the first eligible group are picked"
  );
  for (let s of filtered) {
    Assert.equal(s.engine, engine.name, "Engine is set");
    Assert.equal(s.dismissalKey, s.data.query, "Dismissal key is kept");
    Assert.equal(s.score, 0.3, "Score is kept");
  }

  UrlbarPrefs.set("temporal.rowCount", 1);
  filtered = await gFeature.filterSuggestions(suggestions);
  Assert.deepEqual(
    filtered.map(s => s.data.query),
    ["q6"],
    "rowCount is respected and the picked queries are skipped"
  );
  UrlbarPrefs.clear("temporal.rowCount");

  Assert.deepEqual(
    await gFeature.filterSuggestions([]),
    [],
    "Nothing without suggestions"
  );

  UrlbarTestUtils.stubNowZonedDateTime("2026-11-28T12:00[America/New_York]");
  Assert.deepEqual(
    await gFeature.filterSuggestions(suggestions),
    [],
    "Nothing after the group ends"
  );

  UrlbarTestUtils.stubNowZonedDateTime(null);
  await SearchService.setDefault(
    oldDefaultEngine,
    SearchService.CHANGE_REASON.ADDON_INSTALL
  );
});
