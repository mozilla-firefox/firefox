/* Any copyright is dedicated to the Public Domain.
http://creativecommons.org/publicdomain/zero/1.0/ */
"use strict";

const perfMetadata = {
  owner: "GenAI Team",
  name: "browser_smart_tab_grouping_perf.js",
  description:
    "End-to-end Smart Tab Grouping latency, measured through the tab group menu",
  options: {
    default: {
      perfherder: true,
      // Declared names are substring matchers, not exact names
      // (mozperftest/metrics/common.py:227), so one unsuffixed entry matches
      // both the NATIVE and WASM series that D323349 emits.
      perfherder_metrics: [
        {
          name: "STG-E2E-embedder-engine-creation-time-cold",
          unit: "ms",
          shouldAlert: true,
        },
        {
          name: "STG-E2E-embedder-engine-creation-time-first-use",
          unit: "ms",
          shouldAlert: false,
        },
        {
          name: "STG-E2E-embedder-engine-run-time-cold",
          unit: "ms",
          shouldAlert: true,
        },
        {
          name: "STG-E2E-embedder-engine-run-time-first-use",
          unit: "ms",
          shouldAlert: false,
        },
        {
          name: "STG-E2E-embedder-engine-run-time-warm",
          unit: "ms",
          shouldAlert: true,
        },
        {
          name: "STG-E2E-embedder-memory-after-run-cold",
          unit: "MiB",
          shouldAlert: true,
        },
        {
          name: "STG-E2E-embedder-memory-after-run-first-use",
          unit: "MiB",
          shouldAlert: false,
        },
        {
          name: "STG-E2E-embedder-memory-after-run-warm",
          unit: "MiB",
          shouldAlert: true,
        },
        {
          name: "STG-E2E-embedder-memory-before-run-cold",
          unit: "MiB",
          shouldAlert: true,
        },
        {
          name: "STG-E2E-embedder-memory-before-run-first-use",
          unit: "MiB",
          shouldAlert: false,
        },
        {
          name: "STG-E2E-embedder-memory-before-run-warm",
          unit: "MiB",
          shouldAlert: true,
        },
        {
          name: "STG-E2E-labelLatency-cold",
          unit: "ms",
          shouldAlert: true,
        },
        {
          name: "STG-E2E-labelLatency-first-use",
          unit: "ms",
          shouldAlert: false,
        },
        {
          name: "STG-E2E-labelLatency-warm",
          unit: "ms",
          shouldAlert: true,
        },
        {
          name: "STG-E2E-peak-memory",
          unit: "MiB",
          shouldAlert: true,
        },
        {
          name: "STG-E2E-suggestLatency-cold",
          unit: "ms",
          shouldAlert: true,
        },
        {
          name: "STG-E2E-suggestLatency-first-use",
          unit: "ms",
          shouldAlert: false,
        },
        {
          name: "STG-E2E-suggestLatency-warm",
          unit: "ms",
          shouldAlert: true,
        },
        {
          name: "STG-E2E-topic-engine-creation-time-cold",
          unit: "ms",
          shouldAlert: true,
        },
        {
          name: "STG-E2E-topic-engine-creation-time-first-use",
          unit: "ms",
          shouldAlert: false,
        },
        {
          name: "STG-E2E-topic-engine-run-time-cold",
          unit: "ms",
          shouldAlert: true,
        },
        {
          name: "STG-E2E-topic-engine-run-time-first-use",
          unit: "ms",
          shouldAlert: false,
        },
        {
          name: "STG-E2E-topic-engine-run-time-warm",
          unit: "ms",
          shouldAlert: true,
        },
        {
          name: "STG-E2E-topic-memory-after-run-cold",
          unit: "MiB",
          shouldAlert: true,
        },
        {
          name: "STG-E2E-topic-memory-after-run-first-use",
          unit: "MiB",
          shouldAlert: false,
        },
        {
          name: "STG-E2E-topic-memory-after-run-warm",
          unit: "MiB",
          shouldAlert: true,
        },
        {
          name: "STG-E2E-topic-memory-before-run-cold",
          unit: "MiB",
          shouldAlert: true,
        },
        {
          name: "STG-E2E-topic-memory-before-run-first-use",
          unit: "MiB",
          shouldAlert: false,
        },
        {
          name: "STG-E2E-topic-memory-before-run-warm",
          unit: "MiB",
          shouldAlert: true,
        },
      ],
      verbose: true,
      ml_services: true,
      manifest: "perftest.toml",
      manifest_flavor: "browser-chrome",
      try_platform: ["linux", "mac", "win"],
    },
  },
};

// 1 first-use + 5 cold + 2 warmups + 5 warm + 5 memory = 18 invocations,
// with a full engine teardown and model reload before 12 of them.
requestLongerTimeout(60);

const { sinon } = ChromeUtils.importESModule(
  "resource://testing-common/Sinon.sys.mjs"
);
const { MLPerfTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/MLPerfTestUtils.sys.mjs"
);
const { SmartTabGroupingManager } = ChromeUtils.importESModule(
  "moz-src:///browser/components/tabbrowser/SmartTabGrouping.sys.mjs"
);

MLPerfTestUtils.init(this);

const FIXTURE_PATH =
  "/browser/browser/components/tabbrowser/test/browser/smarttabgrouping/performance/data/e2e/";

/**
 * Real pages, opened as real tabs. Titles drive both the topic model and the
 * embeddings, so they are written to be as separable as a genuine browsing
 * session: two travel tabs seed the group, one travel tab is the only correct
 * suggestion, and the rest are unambiguously off-topic.
 *
 * The shipping suggestion method is logistic regression, and it scores a
 * candidate's base domain against the group's at roughly a third of the total
 * weight. Titles this short do not clear its threshold on embedding similarity
 * alone, so the layout mirrors how the feature is actually used: the trip is
 * researched on one site, and the unrelated tabs live elsewhere. The related
 * candidate is separated from the distractors by both signals at once, which
 * keeps a wide margin on either side of the threshold.
 */
const SEED_HOST = "https://example.com";
const UNRELATED_HOST = "https://example.org";

const SEED_TABS = [
  {
    url: `${SEED_HOST}${FIXTURE_PATH}flights.html`,
    title: "Cheap Flights, Airline Tickets & Airfare Deals",
  },
  {
    url: `${SEED_HOST}${FIXTURE_PATH}hotels.html`,
    title: "Hotel Deals: Save Big on Hotels and Resorts",
  },
];

const RELATED_TAB = {
  url: `${SEED_HOST}${FIXTURE_PATH}lisbon.html`,
  title: "Top 10 Things to Do in Lisbon - Travel Guide",
};

const UNRELATED_TABS = [
  {
    url: `${UNRELATED_HOST}${FIXTURE_PATH}mdn-array-map.html`,
    title: "Array.prototype.map() - JavaScript | MDN",
  },
  {
    url: `${UNRELATED_HOST}${FIXTURE_PATH}nba-scoreboard.html`,
    title: "NBA Scoreboard - Live Scores and Results",
  },
  {
    url: `${UNRELATED_HOST}${FIXTURE_PATH}lasagna.html`,
    title: "The Very Best Lasagna Recipe",
  },
];

/**
 * Records the timestamp at which the group's label became non-empty.
 *
 * `#setMlGroupLabel` assigns `group.label` immediately before writing the name
 * field, and the label setter fires `TabGroupUpdate` synchronously, so the
 * recorded time is the moment the generated label reaches the UI. Callers poll
 * for the value, but the measurement itself is taken in the listener, so
 * polling granularity stays out of the reported latency.
 *
 * @param {MozTabbrowserTabGroup} group
 * @returns {{when: number|null}}
 */
function observeGroupLabel(group) {
  const observed = { when: null };
  const onUpdate = () => {
    if (group.label && observed.when === null) {
      observed.when = performance.now();
      group.removeEventListener("TabGroupUpdate", onUpdate);
    }
  };
  group.addEventListener("TabGroupUpdate", onUpdate);
  return observed;
}

/**
 * Records the time of every batch of suggestion rows appended to the panel, so
 * the caller can pick out the moment the last row landed once it knows how many
 * tabs were suggested.
 *
 * @param {Element} container
 * @returns {{times: Array<{when: number, count: number}>, disconnect: Function}}
 */
function observeSuggestionRows(container) {
  const times = [];
  const observer = new MutationObserver(() => {
    times.push({
      when: performance.now(),
      count: container.querySelectorAll(".tab-group-suggestion-checkbox")
        .length,
    });
  });
  observer.observe(container, { childList: true });
  return { times, disconnect: () => observer.disconnect() };
}

async function openFixtureTabs() {
  const opened = [];
  for (const { url, title } of [...SEED_TABS, RELATED_TAB, ...UNRELATED_TABS]) {
    const tab = await BrowserTestUtils.openNewForegroundTab(gBrowser, url);
    opened.push({ tab, title });
  }

  // A tab whose title has not been applied yet would silently degrade every
  // measurement below into "embed the URL", so gate on it before measuring.
  for (const { tab, title } of opened) {
    await TestUtils.waitForCondition(
      () => tab.label === title,
      `Waiting for tab to be titled "${title}"`,
      100,
      // 30s, not the 5s default. Nothing here is measured -- this is setup
      // that runs before the first sample -- so a generous bound costs
      // nothing and keeps a loaded worker from turning six page loads into
      // an intermittent.
      300
    );
  }

  return opened.map(({ tab }) => tab);
}

/**
 * Runs one full pass of the user flow: create a group from the seed tabs, wait
 * for the generated label, ask for tab suggestions, then cancel out of the
 * panel so the next pass starts from the same state.
 */
async function runScenario({ editor, panel, seedTabs, spies }) {
  const nameField = panel.querySelector("#tab-group-name");
  const suggestionsContainer = panel.querySelector("#tab-group-suggestions");
  const suggestButton = panel.querySelector("#tab-group-suggestion-button");

  const labelCallIndex = spies.label.callCount;
  const suggestCallIndex = spies.suggest.callCount;

  const panelShown = BrowserTestUtils.waitForPopupEvent(panel, "shown");
  const labelStart = performance.now();
  const group = gBrowser.addTabGroup(seedTabs, {
    metricsContext: gBrowser.TabMetrics.userTriggeredContext(),
  });
  const labelObserved = observeGroupLabel(group);
  await panelShown;

  Assert.equal(
    spies.label.callCount,
    labelCallIndex + 1,
    "Opening the create panel asked the manager for a predicted label"
  );

  const manager = spies.label.thisValues[labelCallIndex];
  const predictedLabel = await spies.label.returnValues[labelCallIndex];

  // getPredictedLabelForGroup swallows inference failures and resolves to "",
  // which never reaches the name field. Checking the resolved value before
  // waiting on the UI keeps a broken engine from surfacing as an opaque
  // timeout, and keeps a fictional latency out of the results.
  Assert.ok(
    predictedLabel && predictedLabel.trim().length,
    `Topic model produced a label (got ${JSON.stringify(predictedLabel)})`
  );
  Assert.notEqual(
    manager.getLabelReason(),
    "ERROR",
    "Label generation did not take the error path"
  );

  await TestUtils.waitForCondition(
    () => labelObserved.when !== null,
    "Waiting for the generated label to reach the UI",
    100,
    // 30s, not the 5s default. This is a sync primitive, not a latency
    // assertion: first-use label generation is ~3.5s on a fast machine and the
    // span being measured is reported, so a slow label should show up as a
    // large labelLatency rather than as a test failure.
    300
  );
  const labelLatency = labelObserved.when - labelStart;
  Assert.equal(
    nameField.value,
    predictedLabel,
    "Generated label reached the name field"
  );
  const rows = observeSuggestionRows(suggestionsContainer);
  const suggestStart = performance.now();
  suggestButton.click();

  Assert.equal(
    spies.suggest.callCount,
    suggestCallIndex + 1,
    "Clicking suggest asked the manager for tab suggestions"
  );
  const suggestedTabs = await spies.suggest.returnValues[suggestCallIndex];

  // An empty result renders no rows at all, so bail out with the reason rather
  // than waiting for rows that are never coming.
  Assert.greater(
    suggestedTabs.length,
    0,
    "Suggestion flow returned at least one tab"
  );

  await TestUtils.waitForCondition(
    () => rows.times.some(r => r.count >= suggestedTabs.length),
    "Waiting for a suggestion row per suggested tab"
  );
  rows.disconnect();
  const suggestLatency =
    rows.times.find(r => r.count >= suggestedTabs.length).when - suggestStart;

  const suggestedTitles = suggestedTabs.map(t => t.label);
  Assert.deepEqual(
    suggestedTitles,
    [RELATED_TAB.title],
    "Only the ungrouped travel tab was suggested"
  );

  const panelHidden = BrowserTestUtils.waitForPopupEvent(panel, "hidden");
  editor.close(false);
  await panelHidden;

  Assert.ok(!group.isConnected, "Cancelling the panel removed the group");

  return { labelLatency, suggestLatency };
}

// Prefs live in perftest.toml, following D322000. `--ml-services` points the
// browser at production Remote Settings and model-hub.mozilla.org, so there is
// no perfSetup() and no mocked RS -- and therefore no MOZ_MODELS_HUB either.
add_task(async function test_smart_tab_grouping() {
  // Spies, not stubs: the production inference path runs untouched, and these
  // only expose what it returned and which manager instance ran it.
  const spies = {
    label: sinon.spy(
      SmartTabGroupingManager.prototype,
      "getPredictedLabelForGroup"
    ),
    suggest: sinon.spy(
      SmartTabGroupingManager.prototype,
      "smartTabGroupingForGroup"
    ),
  };

  const tabs = await openFixtureTabs();
  const seedTabs = tabs.slice(0, SEED_TABS.length);

  const editor = document.getElementById("tab-group-editor");
  const panel = editor.panel;

  try {
    await MLPerfTestUtils.runPerfScenario({
      metricPrefix: "STG-E2E",
      // No backend override: pinEngineBackends (D323349) injects
      // {expectValue: "best-onnx", replaceWith: <MOZ_ML_BACKENDS>} for every
      // declared engine, and assertPinnedBackend verifies each one resolved.
      // STG requests best-onnx, so the expectValue matches as-is.
      engines: [
        {
          featureId: "smart-tab-topic",
          metricName: "topic",
          // generateGroupLabels calls topicEngine.run() once
          // (SmartTabGrouping.sys.mjs:1595).
          expectedRuns: 1,
        },
        {
          featureId: "simple-text-embedder",
          metricName: "embedder",
          // Three runs per invocation, and only two of them are real work:
          //  1. initEmbeddingEngine()'s warm-up embedMany(["test"]), kicked
          //     off unawaited from tabgroup-menu.mjs on every panel open.
          //  2. the anchor+candidate title batch (SmartTabGrouping:896).
          //  3. the group label (:905), which fires because
          //     smartTabGroupingForGroup passes groupLabel: group?.label and
          //     the topic model has already set it.
          // embedMany batches per call, so it is three runs, not one per tab.
          expectedRuns: 3,
        },
      ],
      coldIterations: 5,
      warmIterations: 5,
      memoryIterations: 5,
      async scenario() {
        return runScenario({ editor, panel, seedTabs, spies });
      },
    });
  } finally {
    spies.label.restore();
    spies.suggest.restore();
    for (const tab of tabs) {
      BrowserTestUtils.removeTab(tab);
    }
  }
});
