/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const { AutoTabGroupingSuggestions } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/ui/modules/AutoTabGroupingSuggestions.sys.mjs"
);

const { MockEngineManager } = ChromeUtils.importESModule(
  "resource://testing-common/AIWindowTestUtils.sys.mjs"
);

// Purpose the tab-group-naming feature resolves to (see FEATURE_PURPOSES in
// PromptLoader). MockEngineManager keys engines by purpose.
const NAMING_PURPOSE = "auto-tab-grouping";

function makeTab(label, url) {
  return {
    label,
    linkedBrowser: { currentURI: Services.io.newURI(url) },
  };
}

// A manager stub whose clustering step is fixed; the LLM naming call itself is
// NOT stubbed here, only its network boundary (via MockEngineManager), so
// _llmLabelForGroup runs for real: prompt load, title sanitization/cap,
// rendering, and response post-processing.
function installManager(cluster, onDeviceLabel = "On-Device Label") {
  const original = AutoTabGroupingSuggestions._manager;
  let onDeviceCalls = 0;
  AutoTabGroupingSuggestions._manager = {
    async generateClusters() {
      return { clusterRepresentations: [cluster] };
    },
    async getPredictedLabelForGroup() {
      onDeviceCalls++;
      return onDeviceLabel;
    },
  };
  AutoTabGroupingSuggestions._labelCache.clear();
  return {
    onDeviceCalls: () => onDeviceCalls,
    restore() {
      AutoTabGroupingSuggestions._manager = original;
      AutoTabGroupingSuggestions._labelCache.clear();
    },
  };
}

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.smartwindow.autoTabGrouping.minTabsPerGroup", 2],
      ["browser.smartwindow.autoTabGrouping.maxGroups", 3],
    ],
  });
});

add_task(async function test_buildProposals_usesLlmLabel() {
  const mockEngine = new MockEngineManager();
  const manager = installManager({
    tabs: [
      makeTab("Q3 Roadmap", "https://a.example/"),
      makeTab("Barcelona Trip", "https://b.example/"),
    ],
    cohesion: 0.9,
  });
  try {
    const proposalsPromise = AutoTabGroupingSuggestions.buildProposals([]);
    const { request, respond } = await mockEngine.captureRequest({
      purpose: NAMING_PURPOSE,
    });
    // The titles reached the model through the real code path, not a stub.
    Assert.ok(
      JSON.stringify(request).includes("Q3 Roadmap"),
      "The real tab titles are sent to the model"
    );
    respond("Work Docs");
    const proposals = await proposalsPromise;
    Assert.equal(
      proposals[0].label,
      "Work Docs",
      "The model's label is used and does not fall back to on-device"
    );
    Assert.equal(manager.onDeviceCalls(), 0, "The on-device model is not used");
  } finally {
    manager.restore();
    mockEngine.cleanupMocks();
  }
});

add_task(async function test_buildProposals_truncatesLongLlmLabel() {
  const mockEngine = new MockEngineManager();
  const manager = installManager({
    tabs: [
      makeTab("A", "https://a.example/"),
      makeTab("B", "https://b.example/"),
    ],
    cohesion: 0.9,
  });
  try {
    const proposalsPromise = AutoTabGroupingSuggestions.buildProposals([]);
    const { respond } = await mockEngine.captureRequest({
      purpose: NAMING_PURPOSE,
    });
    respond("Work Documents and Travel Planning Notes");
    const proposals = await proposalsPromise;
    const label = proposals[0].label;
    Assert.lessOrEqual(
      label.length,
      25,
      `An over-long label is truncated (got "${label}")`
    );
    Assert.ok(
      !/\s(and|or|&)$/i.test(label),
      "A dangling trailing connective left by truncation is removed"
    );
  } finally {
    manager.restore();
    mockEngine.cleanupMocks();
  }
});

add_task(async function test_buildProposals_dropsBlockedGroupWithoutFallback() {
  const mockEngine = new MockEngineManager();
  const manager = installManager({
    tabs: [
      makeTab("A", "https://a.example/"),
      makeTab("B", "https://b.example/"),
    ],
    cohesion: 0.9,
  });
  try {
    const proposalsPromise = AutoTabGroupingSuggestions.buildProposals([]);
    const { respond } = await mockEngine.captureRequest({
      purpose: NAMING_PURPOSE,
    });
    // An empty response from an available model is a deliberate decline (NSFW).
    respond("");
    const proposals = await proposalsPromise;
    Assert.equal(
      manager.onDeviceCalls(),
      0,
      "A blocked group does not fall back to the on-device model"
    );
    Assert.equal(proposals.length, 0, "The blocked group is dropped");
  } finally {
    manager.restore();
    mockEngine.cleanupMocks();
  }
});

add_task(async function test_buildProposals_fallsBackToOnDeviceLabel() {
  const mockEngine = new MockEngineManager();
  const manager = installManager({
    tabs: [
      makeTab("A", "https://a.example/"),
      makeTab("B", "https://b.example/"),
    ],
    cohesion: 0.9,
  });
  try {
    const proposalsPromise = AutoTabGroupingSuggestions.buildProposals([]);
    await mockEngine.captureRequest({ purpose: NAMING_PURPOSE });
    // Simulate a server / rate-limit error on the model call.
    mockEngine.rejectAllRequests();
    const proposals = await proposalsPromise;
    Assert.equal(
      proposals[0].label,
      "On-Device Label",
      "A failed model call falls back to the on-device label"
    );
  } finally {
    manager.restore();
    mockEngine.cleanupMocks();
  }
});
