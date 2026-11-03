/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * End-to-end responsiveness of Smart Window with mocked MLPA.
 *
 * All flows include any on-device ML calls that could occur during a Smart Window
 * user interaction. To support this and avoid fetching models and Remote Settings
 * in every subtest, this reuses the taskcluster fetches from the toolkit/component/ml
 * perftest suite.
 *
 * The metric is an interval read from the "SmartWindow" profiler markers the
 * product emits, with the LLM calls stubbed to answer instantly. It tracks the
 * overhead introduced in the "hot" path of Smart Window:
 *
 *  ttft-overhead: from message submitted to first token, excluding MLPA calls
 *
 * It is measured in various scenarios, as some algorithms like the memory
 * indexing have execution times that depend on the user profile state. The scenarios
 * exercise various settings for:
 *
 *  Window state: cold | warm
 *     A cold window has no ML engines running and nothing cached (like the memory embeddings)
 *     A warm window has warm engines and some cache is possible
 *  Profile:
 *     visits: number of entries in the history
 *     memories: number of memories in store
 *
 * Every scenario runs one unrecorded iteration before the measured ones, so
 * the one-time costs of reaching it do not land in the sample.
 *
 * The profiler is started here unless the harness was asked for a profile, so
 * `--gecko-profile` still yields a usable one. See harnessWantsProfile.
 *
 * To run locally, serve the models from a local hub like the ML perftests do
 * (https://firefox-source-docs.mozilla.org/toolkit/components/ml/perf.html):
 *
 *   ./mach python toolkit/components/ml/tests/tools/create_local_hub.py \
 *     --fetches-dir ~/ml-fetches \
 *     --model xenova-all-minilm-l6-v2 --model mozilla-query-intent
 *   MOZ_ML_LOCAL_DIR=~/ml-fetches ./mach perftest \
 *     --hooks toolkit/components/ml/tests/tools/hooks_local_hub.py \
 *     --mochitest-extra-args=headless \
 *     browser/components/aiwindow/ui/test/browser_perf/browser_smartwindow_perf.js
 */

const perfMetadata = {
  owner: "GenAI Team",
  name: "browser_smartwindow_perf.js",
  description:
    "User-perceived responsiveness of Smart Window across window states and profile sizes",
  options: {
    default: {
      perfherder: true,
      perfherder_metrics: [
        { name: "ttft-overhead", unit: "ms", shouldAlert: true },
      ],
      verbose: true,
      manifest: "perftest.toml",
      manifest_flavor: "browser-chrome",
      try_platform: ["linux", "mac", "win"],
    },
  },
};

requestLongerTimeout(30);

const { EngineProcess } = ChromeUtils.importESModule(
  "chrome://global/content/ml/EngineProcess.sys.mjs"
);
const { MemoryStore } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/services/MemoryStore.sys.mjs"
);
const { ProfilerTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/ProfilerTestUtils.sys.mjs"
);
const { PlacesTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/PlacesTestUtils.sys.mjs"
);

// Lazy because mozperftest evaluates this file in node to read perfMetadata.
const lazy = {};
ChromeUtils.defineLazyGetter(
  lazy,
  "PlacesFrecencyRecalculator",
  () =>
    Cc["@mozilla.org/places/frecency-recalculator;1"].getService(Ci.nsIObserver)
      .wrappedJSObject
);

const ITERATIONS = 5;
const CHUNKS = ["The ", "quick ", "brown ", "fox."];
const PROMPT = "what should I look at next?";
const LONG_PROMPT = "Summarize the following passage. ".repeat(300);
const DEEP_CONVERSATION_TURNS = 5;
const QUERY_CONTEXT_TIMEOUT_MS = 30000;
const MESSAGE_COMPLETE_EVENT = "chat-conversation:message-complete";

const METRIC = "ttft-overhead";

const TTFT_MARKER = "Time to first token (TTFT)";
const TURNAROUND_MARKER = "Total turnaround time";
const SERVER_MARKER = "ServerE2E";

const PROFILES = {
  "fresh-profile": { visits: 0, memories: 0 },
  "medium-profile": { visits: 2000, memories: 20 },
  "large-profile": { visits: 20000, memories: 200 },
};

// Grouped by profile, which is seeded once per group.
const SCENARIOS = [
  { profile: "fresh-profile", windowState: "cold" },
  { profile: "fresh-profile", windowState: "warm" },
  { profile: "medium-profile", windowState: "cold" },
  { profile: "medium-profile", windowState: "warm" },
  { profile: "medium-profile", windowState: "warm", prompt: "long-prompt" },
  { profile: "medium-profile", windowState: "warm", prompt: "deep-chat" },
  { profile: "large-profile", windowState: "cold" },
  { profile: "large-profile", windowState: "warm" },
];

/**
 * @param {object} scenario - Row from SCENARIOS.
 * @returns {string} The subtest name, e.g. "ttft-overhead-cold-large-profile".
 */
function metricName(scenario) {
  const parts = [METRIC, scenario.windowState, scenario.profile];
  if (scenario.prompt) {
    parts.push(scenario.prompt);
  }
  return parts.join("-");
}

/** @type {Map<string, number[]>} Subtest name to its per-iteration values. */
const journal = new Map();

/**
 * Fills history and the memory store to the size the profile describes.
 *
 * @param {string} name - Key into PROFILES.
 */
async function seedProfile(name) {
  const { visits, memories } = PROFILES[name];

  await PlacesUtils.history.clear();
  for (let start = 0; start < visits; start += 1000) {
    const pages = [];
    for (let i = start; i < Math.min(start + 1000, visits); i++) {
      pages.push({
        url: `https://example.com/${name}/page-${i}`,
        title: `${name} history entry ${i}`,
        visits: [{ transition: PlacesUtils.history.TRANSITIONS.LINK }],
      });
    }
    await PlacesUtils.history.insertMany(pages);
  }

  // Make sure Places is ready to be used after the insertion.
  await lazy.PlacesFrecencyRecalculator.recalculateAnyOutdatedFrecencies();
  await PlacesTestUtils.promiseAsyncUpdates();

  for (const memory of await MemoryStore.getMemories({
    includeSoftDeleted: true,
  })) {
    await MemoryStore.hardDeleteMemory(memory.id);
  }
  for (let i = 0; i < memories; i++) {
    await MemoryStore.addMemory({
      id: `perf-mem-${i}`,
      memory_summary: `Reads about ${name} topic number ${i} regularly`,
    });
  }
}

/**
 * @param {string} windowState - "cold" or "warm".
 */
async function applyWindowState(windowState) {
  if (windowState !== "cold") {
    return;
  }
  await EngineProcess.destroyMLEngine();
  await TestUtils.waitForCondition(
    () => EngineProcess.areAllEnginesTerminated(),
    "Wait for the inference engines to terminate"
  );
  MemoryStore._clearEmbeddingsCache();
}

/**
 * @param {MozBrowser} browser - The browser hosting ai-window.
 * @returns {Element} The ai-window element, which runs in the parent process.
 */
function aiWindowElement(browser) {
  return browser.contentDocument.querySelector("ai-window");
}

/**
 * @param {ChatConversation} conversation - The conversation being answered.
 * @returns {Promise<void>} Resolves when the next assistant message completes.
 */
function promiseMessageComplete(conversation) {
  return new Promise(resolve => {
    const onComplete = () => {
      conversation.off(MESSAGE_COMPLETE_EVENT, onComplete);
      resolve();
    };
    conversation.on(MESSAGE_COMPLETE_EVENT, onComplete);
  });
}

/**
 * Types a prompt, selects the chat action, submits it, and waits for the turn
 * to fully process. The profiler is not read here, so serialization stays out
 * of the measured path.
 *
 * @param {MozBrowser} browser - The browser hosting ai-window.
 * @param {string} text - The prompt to submit.
 */
async function runTurn(browser, text) {
  await SpecialPowers.spawn(
    browser,
    [text, QUERY_CONTEXT_TIMEOUT_MS],
    async (prompt, timeoutMs) => {
      const aiWindow = content.document.querySelector("ai-window");
      const bar = await ContentTaskUtils.waitForCondition(() => {
        const el = aiWindow.shadowRoot.querySelector("#ai-window-smartbar");
        return el && el.inputField && el;
      }, "Wait for the smartbar");
      bar.value = prompt.slice(0, -1);
      bar.inputField.focus();
      EventUtils.sendString(prompt.slice(-1), content);
      // This promise never rejects on its own, so it needs a timeout.
      let timer;
      try {
        await Promise.race([
          bar.lastQueryContextPromise,
          new Promise((resolve, reject) => {
            timer = content.setTimeout(
              () =>
                reject(new Error("Timed out waiting for the query context")),
              timeoutMs
            );
          }),
        ]);
      } finally {
        content.clearTimeout(timer);
      }
    }
  );

  await AIWindowTestUtils.selectExplicitSmartbarAction(browser, "chat");
  await AIWindowTestUtils.waitForSmartbarAction(browser, "chat");

  // The action dropdown keeps focus, so the input is refocused before Enter.
  await SpecialPowers.spawn(browser, [], async () => {
    const aiWindow = content.document.querySelector("ai-window");
    const bar = aiWindow.shadowRoot.querySelector("#ai-window-smartbar");
    bar.inputField.focus();
    await ContentTaskUtils.waitForCondition(
      () => bar.matches(":focus-within"),
      "Wait for the smartbar to accept focus"
    );
  });

  const aiWindow = aiWindowElement(browser);
  const messageComplete = promiseMessageComplete(aiWindow.conversation);
  await SpecialPowers.spawn(browser, [], () => {
    EventUtils.synthesizeKey("KEY_Enter", {}, content);
  });
  await messageComplete;
  // The turnaround marker is recorded after the message completes and before
  // the window stops generating.
  await TestUtils.waitForCondition(
    () => !aiWindow.isGenerating,
    "Wait for the turn to finish"
  );
}

/**
 * Collects the "SmartWindow" markers of the parent process.
 *
 * @returns {Array<{label: string, duration: number}>}
 */
function smartWindowMarkers() {
  const into = [];
  for (const thread of Services.profiler.getProfileData().threads) {
    const { schema, data } = thread.markers;
    for (const row of data) {
      if (thread.stringTable[row[schema.name]] !== "SmartWindow") {
        continue;
      }
      const payload = row[schema.data];
      let label = payload && payload.name;
      if (typeof label === "number") {
        label = thread.stringTable[label];
      }
      into.push({
        label: label || "",
        duration: row[schema.endTime] - row[schema.startTime],
      });
    }
  }
  return into;
}

function markerDurations(markers, label) {
  return markers
    .filter(marker => marker.label === label)
    .map(marker => marker.duration);
}

/**
 * @returns {boolean} True when a profile was requested, so the harness owns
 *   the profiler session. IsActive() is always true under mochitest.
 */
function harnessWantsProfile() {
  return Services.env.exists("MOZ_PROFILER_SHUTDOWN");
}

/**
 * @returns {object} Label to the number of markers already in the buffer.
 */
function markerBaseline() {
  const markers = smartWindowMarkers();
  const baseline = {};
  for (const label of [TTFT_MARKER, TURNAROUND_MARKER, SERVER_MARKER]) {
    baseline[label] = markerDurations(markers, label).length;
  }
  return baseline;
}

/**
 * @param {Array<{label: string, duration: number}>} markers - Markers to read.
 * @param {string} label - Marker label.
 * @param {object} baseline - From markerBaseline.
 * @returns {number[]} The durations, oldest first.
 */
function newMarkerDurations(markers, label, baseline) {
  return markerDurations(markers, label).slice(baseline[label]);
}

/**
 * Runs one iteration of a scenario in its own window.
 *
 * @param {object} scenario - Row from SCENARIOS.
 * @returns {Promise<number>} The measured TTFT overhead, in ms.
 */
async function runScenario(scenario) {
  await applyWindowState(scenario.windowState);

  const ownsProfiler = !harnessWantsProfile();
  const win = await AIWindowTestUtils.openReadyAIWindow();
  try {
    const browser = win.gBrowser.selectedBrowser;
    await AIWindowTestUtils.getAichatBrowser(browser);
    const prompt = scenario.prompt === "long-prompt" ? LONG_PROMPT : PROMPT;
    let markers;
    let turns = 0;

    if (ownsProfiler) {
      await ProfilerTestUtils.startProfiler({
        features: ["nostacksampling"],
        threads: ["GeckoMain"],
      });
    }
    const baseline = markerBaseline();

    await AIWindowTestUtils.withServer({ streamChunks: CHUNKS }, async () => {
      // A warm window has already answered something.
      if (scenario.windowState === "warm") {
        await runTurn(browser, "hello there, what is this?");
        turns++;
      }
      if (scenario.prompt === "deep-chat") {
        for (let i = 0; i < DEEP_CONVERSATION_TURNS; i++) {
          await runTurn(browser, `tell me more about topic ${i}`);
          turns++;
        }
      }

      if (scenario.windowState === "cold") {
        Assert.ok(
          EngineProcess.areAllEnginesTerminated(),
          "The engines are still cold when the measured turn starts"
        );
      }

      await runTurn(browser, prompt);
      turns++;
      markers = smartWindowMarkers();
    });

    Assert.ok(
      !EngineProcess.areAllEnginesTerminated(),
      "The on-device ML engines ran during the turn"
    );
    if (PROFILES[scenario.profile].memories > 0) {
      Assert.ok(
        !!MemoryStore.memoryEmbeddingsCache,
        "The memories embedder produced embeddings"
      );
    }
    Assert.ok(
      newMarkerDurations(markers, SERVER_MARKER, baseline).length,
      "The turns went through the mocked MLPA server"
    );

    const ttfts = newMarkerDurations(markers, TTFT_MARKER, baseline);
    Assert.equal(ttfts.length, turns, "Every turn emitted a TTFT marker");
    Assert.equal(
      newMarkerDurations(markers, TURNAROUND_MARKER, baseline).length,
      turns,
      "Every turn emitted a turnaround marker"
    );

    // The measured turn is the last one.
    return Math.round(ttfts[turns - 1]);
  } finally {
    if (ownsProfiler && Services.profiler.IsActive()) {
      await Services.profiler.StopProfiler();
    }
    await BrowserTestUtils.closeWindow(win);
  }
}

add_setup(async function () {
  const prefs = [
    ["browser.smartwindow.firstrun.modelChoice", "0"],
    // The memories flows are only reachable with ToU consent.
    ["browser.smartwindow.tos.consentTime", 123],
  ];
  const modelHubRootUrl = Services.env.get("MOZ_MODELS_HUB");
  if (modelHubRootUrl) {
    prefs.push(["browser.ml.modelHubRootUrl", modelHubRootUrl]);
  }
  await SpecialPowers.pushPrefEnv({ set: prefs });
  registerCleanupFunction(async () => {
    await PlacesUtils.history.clear();
    await SpecialPowers.popPrefEnv();
  });
});

for (const profile of Object.keys(PROFILES)) {
  const rows = SCENARIOS.filter(scenario => scenario.profile === profile);

  add_task({ name: `smartwindow_${profile}` }, async function () {
    await seedProfile(profile);

    for (const scenario of rows) {
      // Unrecorded warmup iteration.
      await runScenario(scenario);

      const values = [];
      for (let i = 0; i < ITERATIONS; i++) {
        values.push(await runScenario(scenario));
      }
      journal.set(metricName(scenario), values);
    }
  });
}

add_task(function report() {
  const metrics = [...journal].map(([name, values]) => ({
    name,
    values,
    unit: "ms",
    shouldAlert: true,
  }));

  Assert.equal(
    metrics.length,
    SCENARIOS.length,
    "Every scenario reported its metric"
  );
  for (const { name, values } of metrics) {
    Assert.equal(
      values.length,
      ITERATIONS,
      `${name} was measured on every iteration`
    );
  }

  info(`perfMetrics | ${JSON.stringify(metrics)}`);
});
