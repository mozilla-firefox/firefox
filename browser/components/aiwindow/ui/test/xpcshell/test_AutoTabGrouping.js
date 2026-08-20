/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const { AutoTabGroupingSuggestions } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/ui/modules/AutoTabGroupingSuggestions.sys.mjs"
);

function makeTab({
  url = "https://example.com/",
  pinned = false,
  closing = false,
  group = null,
  hidden = false,
  busy = false,
  label = "Example Page",
  iconUrl = null,
} = {}) {
  return {
    pinned,
    closing,
    group,
    hidden,
    label,
    hasAttribute(attr) {
      return attr === "busy" && busy;
    },
    linkedBrowser: url
      ? { currentURI: Services.io.newURI(url), mIconURL: iconUrl }
      : null,
  };
}

function makeWin(tabs) {
  return { gBrowser: { tabs } };
}

function makeCluster(size, cohesion = 0.5) {
  return {
    tabs: Array.from({ length: size }, (_, i) => ({ index: i })),
    cohesion,
  };
}

add_setup(function () {
  Services.prefs.setIntPref(
    "browser.smartwindow.autoTabGrouping.minTabsPerGroup",
    2
  );
  Services.prefs.setIntPref("browser.smartwindow.autoTabGrouping.maxGroups", 3);
  registerCleanupFunction(() => {
    Services.prefs.clearUserPref(
      "browser.smartwindow.autoTabGrouping.minTabsPerGroup"
    );
    Services.prefs.clearUserPref(
      "browser.smartwindow.autoTabGrouping.maxGroups"
    );
  });
});

add_task(function test_getCandidateTabs_keepsOnlyUngroupedWebTabs() {
  const httpsTab = makeTab({ url: "https://a.example/" });
  const httpTab = makeTab({ url: "http://b.example/" });
  const pinnedTab = makeTab({ pinned: true });
  const closingTab = makeTab({ closing: true });
  const groupedTab = makeTab({ group: { id: "g1" } });
  const hiddenTab = makeTab({ hidden: true });
  const aboutTab = makeTab({ url: "about:newtab" });
  const chromeTab = makeTab({ url: "chrome://browser/content/browser.xhtml" });
  const noBrowserTab = makeTab({ url: null });

  const win = makeWin([
    httpsTab,
    httpTab,
    pinnedTab,
    closingTab,
    groupedTab,
    hiddenTab,
    aboutTab,
    chromeTab,
    noBrowserTab,
  ]);

  const candidates = AutoTabGroupingSuggestions.getCandidateTabs(win);

  Assert.deepEqual(
    candidates,
    [httpsTab, httpTab],
    "Only ungrouped, non-pinned, visible http(s) tabs are candidates"
  );
});

add_task(function test_getCandidateTabs_excludesStillLoadingTabs() {
  const loaded = makeTab({ url: "https://a.example/" });
  const busyTab = makeTab({ url: "https://b.example/", busy: true });
  const untitledTab = makeTab({ url: "https://c.example/", label: "" });
  const win = makeWin([loaded, busyTab, untitledTab]);

  const candidates = AutoTabGroupingSuggestions.getCandidateTabs(win);

  Assert.deepEqual(
    candidates,
    [loaded],
    "Still-loading and untitled tabs are excluded so clustering sees resolved titles"
  );
});

add_task(function test_getCandidateTabs_excludesAlreadyGroupedTabs() {
  const ungrouped = makeTab({ url: "https://a.example/" });
  const grouped = makeTab({ url: "https://b.example/", group: { id: "g1" } });
  const win = makeWin([ungrouped, grouped]);

  const candidates = AutoTabGroupingSuggestions.getCandidateTabs(win);

  Assert.deepEqual(
    candidates,
    [ungrouped],
    "Tabs already in a group are never re-suggested"
  );
});

add_task(function test_selectClusters_filtersSortsAndCaps() {
  // Sizes 1 and 1 are below minTabsPerGroup (2) and dropped; the rest are
  // ordered largest-first and capped at maxGroups (3).
  const clusters = [
    makeCluster(1),
    makeCluster(5),
    makeCluster(3),
    makeCluster(2),
    makeCluster(4),
    makeCluster(1),
  ];

  const selected = AutoTabGroupingSuggestions.selectClusters(clusters);

  Assert.equal(selected.length, 3, "Capped at maxGroups");
  Assert.deepEqual(
    selected.map(c => c.tabs.length),
    [5, 4, 3],
    "Largest clusters first, small ones dropped, capped at maxGroups"
  );
});

add_task(function test_selectClusters_dropsLowCohesion() {
  // A large but incohesive cluster (below the 0.15 threshold) is dropped even
  // though it clears the size minimum, so weakly-related tabs aren't grouped.
  const clusters = [makeCluster(5, 0.05), makeCluster(3, 0.4)];

  const selected = AutoTabGroupingSuggestions.selectClusters(clusters);

  Assert.deepEqual(
    selected.map(c => c.tabs.length),
    [3],
    "Clusters below the cohesion threshold are dropped regardless of size"
  );
});

add_task(function test_selectClusters_minCohesionPrefTunesThreshold() {
  // Raising the pref to 0.5 now drops a 0.4-cohesion cluster that the default
  // 0.15 threshold would have kept.
  Services.prefs.setCharPref(
    "browser.smartwindow.autoTabGrouping.minCohesion",
    "0.5"
  );
  try {
    const selected = AutoTabGroupingSuggestions.selectClusters([
      makeCluster(5, 0.4),
      makeCluster(3, 0.6),
    ]);
    Assert.deepEqual(
      selected.map(c => c.tabs.length),
      [3],
      "Only clusters at or above the pref-configured threshold are kept"
    );
  } finally {
    Services.prefs.clearUserPref(
      "browser.smartwindow.autoTabGrouping.minCohesion"
    );
  }
});

add_task(function test_selectClusters_handlesEmptyInput() {
  Assert.deepEqual(
    AutoTabGroupingSuggestions.selectClusters(undefined),
    [],
    "undefined clusters -> no proposals"
  );
  Assert.deepEqual(
    AutoTabGroupingSuggestions.selectClusters([]),
    [],
    "no clusters -> no proposals"
  );
  Assert.deepEqual(
    AutoTabGroupingSuggestions.selectClusters([makeCluster(1), { tabs: null }]),
    [],
    "clusters below the minimum (or missing tabs) -> no proposals"
  );
});

add_task(function test_manager_usesSmartWindowTopicModelSlot() {
  const originalManager = AutoTabGroupingSuggestions._manager;
  AutoTabGroupingSuggestions._manager = null;
  try {
    const { topicGeneration } = AutoTabGroupingSuggestions.manager.config;
    // The SW naming model lives in its own featureId/engine slot so it can be
    // updated independently of the shared Smart Tab Grouping model; the model
    // itself resolves from the smart-window-tab-topic Remote Settings record.
    Assert.equal(
      topicGeneration.featureId,
      "smart-window-tab-topic",
      "manager uses the Smart Window topic featureId, not the shared one"
    );
    Assert.equal(
      topicGeneration.engineId,
      "smart-window-tab-topic-engine",
      "manager uses the Smart Window topic engineId"
    );
  } finally {
    AutoTabGroupingSuggestions._manager = originalManager;
  }
});

add_task(async function test_buildProposals_dropsUntitledGroups() {
  const originalManager = AutoTabGroupingSuggestions._manager;
  const originalLlm = AutoTabGroupingSuggestions._llmLabelForGroup;
  // LLM unavailable -> exercise the on-device path.
  AutoTabGroupingSuggestions._llmLabelForGroup = async () => {
    throw new Error("force on-device");
  };
  AutoTabGroupingSuggestions._manager = {
    async generateClusters() {
      return {
        clusterRepresentations: [makeCluster(3, 0.9), makeCluster(2, 0.9)],
      };
    },
    async getPredictedLabelForGroup(tabs) {
      // The larger cluster comes back unlabeled (a Trust & Safety signal).
      return tabs.length === 3 ? "" : "Work";
    },
  };
  AutoTabGroupingSuggestions._labelCache.clear();
  try {
    const proposals = await AutoTabGroupingSuggestions.buildProposals([]);
    Assert.equal(proposals.length, 1, "The untitled group is dropped");
    Assert.equal(proposals[0].label, "Work", "The labeled group is kept");
  } finally {
    AutoTabGroupingSuggestions._manager = originalManager;
    AutoTabGroupingSuggestions._llmLabelForGroup = originalLlm;
    AutoTabGroupingSuggestions._labelCache.clear();
  }
});

add_task(async function test_buildProposals_cachesLabelsBySourceTabs() {
  const originalManager = AutoTabGroupingSuggestions._manager;
  const originalLlm = AutoTabGroupingSuggestions._llmLabelForGroup;
  AutoTabGroupingSuggestions._llmLabelForGroup = async () => {
    throw new Error("force on-device");
  };
  const tabs = [
    makeTab({ url: "https://a.example/" }),
    makeTab({ url: "https://b.example/" }),
  ];
  let labelCalls = 0;
  AutoTabGroupingSuggestions._manager = {
    async generateClusters() {
      return { clusterRepresentations: [{ tabs, cohesion: 0.9 }] };
    },
    async getPredictedLabelForGroup() {
      labelCalls++;
      return "Work";
    },
  };
  AutoTabGroupingSuggestions._labelCache.clear();
  try {
    const first = await AutoTabGroupingSuggestions.buildProposals([]);
    const second = await AutoTabGroupingSuggestions.buildProposals([]);
    Assert.equal(
      labelCalls,
      1,
      "The label is generated once and reused for the same source tabs"
    );
    Assert.equal(first[0].label, "Work", "First run labels the group");
    Assert.equal(
      second[0].label,
      "Work",
      "Second run returns the cached label"
    );
  } finally {
    AutoTabGroupingSuggestions._manager = originalManager;
    AutoTabGroupingSuggestions._llmLabelForGroup = originalLlm;
    AutoTabGroupingSuggestions._labelCache.clear();
  }
});

add_task(function test_tabInfo_resolvesFaviconAndTitle() {
  const cached = AutoTabGroupingSuggestions._tabInfo(
    makeTab({ url: "https://example.com/kids-bikes", label: "Kids Bikes" })
  );
  Assert.equal(
    cached.iconUrl,
    "page-icon:https://example.com/kids-bikes",
    "A tab with no live icon falls back to the Places-cached icon"
  );
  Assert.equal(cached.title, "Kids Bikes", "The tab label is the title");

  Assert.equal(
    AutoTabGroupingSuggestions._tabInfo(
      makeTab({ iconUrl: "https://example.com/favicon.ico" })
    ).iconUrl,
    "page-icon:https://example.com/",
    "A web favicon is loaded through page-icon: rather than the network"
  );

  Assert.equal(
    AutoTabGroupingSuggestions._tabInfo(
      makeTab({ iconUrl: "data:image/png;base64,AAAA" })
    ).iconUrl,
    "data:image/png;base64,AAAA",
    "A non-web icon is used as-is"
  );

  Assert.equal(
    AutoTabGroupingSuggestions._tabInfo(makeTab({ url: null })).iconUrl,
    "chrome://global/skin/icons/defaultFavicon.svg",
    "A tab with no URI falls back to the default favicon"
  );

  Assert.equal(
    AutoTabGroupingSuggestions._tabInfo(
      makeTab({ url: "https://example.com/", label: "" })
    ).title,
    "example.com",
    "A tab with no label falls back to its site name"
  );
});

add_task(function test_hasEnoughMemory_respectsThreshold() {
  const minimumPref = "browser.ml.minimumPhysicalMemory";
  const checkPref = "browser.ml.checkForMemory";
  registerCleanupFunction(() => {
    Services.prefs.clearUserPref(minimumPref);
    Services.prefs.clearUserPref(checkPref);
  });

  Services.prefs.setIntPref(minimumPref, 1024);
  Assert.ok(
    !AutoTabGroupingSuggestions.hasEnoughMemory,
    "Machines below the threshold do not qualify"
  );
  Assert.ok(
    !AutoTabGroupingSuggestions.isAvailable,
    "Not enough memory hides the feature"
  );

  Services.prefs.setBoolPref(checkPref, false);
  Assert.ok(
    AutoTabGroupingSuggestions.hasEnoughMemory,
    "Turning off the platform memory check turns off this one too"
  );
});
