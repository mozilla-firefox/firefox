/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { AutoTabGrouping } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/ui/modules/AutoTabGrouping.sys.mjs"
);
const { AutoTabGroupingSuggestions } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/ui/modules/AutoTabGroupingSuggestions.sys.mjs"
);
const { SmartTabGroupingManager } = ChromeUtils.importESModule(
  "moz-src:///browser/components/tabbrowser/SmartTabGrouping.sys.mjs"
);
const { TabGroupTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/TabGroupTestUtils.sys.mjs"
);

// TODO: Break test into smaller pieces for ATG v1
requestLongerTimeout(2);

function fakeTwoGroupManager() {
  return {
    async generateClusters(tabList) {
      return {
        clusterRepresentations: [
          { tabs: tabList.slice(0, 2), cohesion: 0.9 },
          { tabs: tabList.slice(2, 4), cohesion: 0.9 },
        ],
      };
    },
    async getPredictedLabelForGroup() {
      return "Test Group";
    },
    async preloadAllModels() {},
  };
}

async function addWebTabs(win, paths = ["a", "b", "c", "d"]) {
  for (const path of paths) {
    const url = `https://example.com/${path}`;
    const tab = BrowserTestUtils.addTab(win.gBrowser, url, {
      skipAnimation: true,
    });
    await BrowserTestUtils.browserLoaded(tab.linkedBrowser, false, url);
  }
}

async function navigateToContent(win, url = "https://example.com/") {
  const browser = win.gBrowser.selectedTab.linkedBrowser;
  const loaded = BrowserTestUtils.browserLoaded(browser, false, url);
  BrowserTestUtils.startLoadingURIString(browser, url);
  await loaded;
}

// Enable the feature, open a Smart Window on a content tab, and add enough web
// tabs for clustering to produce suggestions.
async function openGroupingWindowWithTabs() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.smartwindow.autoTabGrouping.enabled", true]],
  });
  const win = await openAIWindow();
  await navigateToContent(win);
  await addWebTabs(win);
  return win;
}

async function openPanel(win) {
  AIWindowUI.toggleGroupTabsPanel(win);
  const panel = await TestUtils.waitForCondition(() =>
    win.document.getElementById("smartwindow-group-tabs-panel")
  );
  await TestUtils.waitForCondition(
    () => panel.state === "open",
    "Panel finished opening"
  );
  return panel;
}

// Open the panel and wait for the two clustered suggestion rows to render.
async function openPanelWithSuggestions(win) {
  const panel = await openPanel(win);
  await TestUtils.waitForCondition(
    () => panel.querySelectorAll(".swgt-suggestion").length === 2,
    "Two suggested group rows render once clustering finishes"
  );
  return panel;
}

async function closePanel(win) {
  AIWindowUI.toggleGroupTabsPanel(win);
  await TestUtils.waitForCondition(
    () => !win.document.getElementById("smartwindow-group-tabs-panel")
  );
}

async function assertButtonHiddenOnContent(win, reason) {
  // The Ask button and the group-tabs button toggle together, so waiting for
  // the Ask button to show means the immersive view has been left; only then is
  // the group-tabs button's visibility down to the feature gate.
  const askButton = win.document.getElementById("smartwindow-ask-button");
  await TestUtils.waitForCondition(
    () => BrowserTestUtils.isVisible(askButton),
    "Left the immersive view (the Ask button is shown)"
  );
  const button = win.document.getElementById(
    "smartwindow-group-tabs-button-inner"
  );
  Assert.ok(
    !AutoTabGroupingSuggestions.isAvailable,
    `Feature is unavailable: ${reason}`
  );
  Assert.ok(
    BrowserTestUtils.isHidden(button),
    `Group tabs button is hidden: ${reason}`
  );
}

add_setup(async function setup() {
  // Baseline allowed, opted-in config; each gating test flips one of these off.
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.smartwindow.endpoint", "http://localhost:0/v1"],
      ["browser.smartwindow.firstrun.hasCompleted", true],
      ["browser.ml.enable", true],
      ["browser.ml.modelHubRootUrl", "http://localhost:0/"],
      ["browser.tabs.groups.smart.enabled", true],
      ["browser.tabs.groups.smart.userEnabled", true],
      ["browser.tabs.groups.smart.optin", true],
    ],
  });

  const originalManager = AutoTabGroupingSuggestions._manager;
  const originalLlmLabel = AutoTabGroupingSuggestions._llmLabelForGroup;
  // The cloud LLM namer is unavailable in tests; force the on-device path so
  // buildProposals uses the stubbed manager's getPredictedLabelForGroup instead
  // of doing real FxA-token/network work per group.
  AutoTabGroupingSuggestions._llmLabelForGroup = async () => {
    throw new Error("force on-device");
  };
  registerCleanupFunction(() => {
    AutoTabGroupingSuggestions._manager = originalManager;
    AutoTabGroupingSuggestions._llmLabelForGroup = originalLlmLabel;
    AutoTabGroupingSuggestions._preloadPromise = null;
  });
});

describe("Auto Tab Grouping toolbar button", () => {
  let win;

  beforeEach(() => {
    AutoTabGroupingSuggestions._manager = fakeTwoGroupManager();
    AutoTabGroupingSuggestions._preloadPromise = null;
    Services.fog.testResetFOG();
  });

  afterEach(async () => {
    if (win) {
      await BrowserTestUtils.closeWindow(win);
      win = null;
    }
    TabGroupTestUtils.forgetSavedTabGroups();
    await SpecialPowers.popPrefEnv();
  });

  describe("visibility gating", () => {
    it("is hidden when the feature pref is off", async () => {
      await SpecialPowers.pushPrefEnv({
        set: [["browser.smartwindow.autoTabGrouping.enabled", false]],
      });

      win = await openAIWindow();
      const button = win.document.getElementById(
        "smartwindow-group-tabs-button-inner"
      );
      Assert.ok(button, "Group tabs button exists in the toolbar");
      Assert.ok(
        BrowserTestUtils.isHidden(button),
        "Group tabs button is hidden while the feature pref is off"
      );
    });

    it("is not hidden in the immersive new-tab view", async () => {
      await SpecialPowers.pushPrefEnv({
        set: [["browser.smartwindow.autoTabGrouping.enabled", true]],
      });

      win = await openAIWindow();
      const askButton = win.document.getElementById("smartwindow-ask-button");
      const groupTabsButton = win.document.getElementById(
        "smartwindow-group-tabs-button"
      );
      await TestUtils.waitForCondition(
        () => BrowserTestUtils.isHidden(askButton),
        "In the immersive new-tab view (the Ask button is hidden)"
      );
      Assert.ok(
        !groupTabsButton.hidden,
        "Group tabs button is not hidden in the immersive view"
      );
    });

    describe("when the app locale is unsupported", () => {
      let isAllowedDescriptor;

      beforeEach(() => {
        isAllowedDescriptor = Object.getOwnPropertyDescriptor(
          SmartTabGroupingManager,
          "isAllowed"
        );
        Object.defineProperty(SmartTabGroupingManager, "isAllowed", {
          configurable: true,
          get: () => false,
        });
      });

      afterEach(() => {
        Object.defineProperty(
          SmartTabGroupingManager,
          "isAllowed",
          isAllowedDescriptor
        );
      });

      it("stays hidden even with the feature pref on", async () => {
        await SpecialPowers.pushPrefEnv({
          set: [["browser.smartwindow.autoTabGrouping.enabled", true]],
        });

        win = await openAIWindow();
        await navigateToContent(win);
        await assertButtonHiddenOnContent(
          win,
          "the app locale is not supported"
        );
      });
    });

    it("stays hidden when on-device ML is disabled", async () => {
      await SpecialPowers.pushPrefEnv({
        set: [
          ["browser.smartwindow.autoTabGrouping.enabled", true],
          ["browser.ml.enable", false],
        ],
      });

      win = await openAIWindow();
      await navigateToContent(win);
      await assertButtonHiddenOnContent(win, "on-device ML is disabled");
    });
  });

  describe("model preloading", () => {
    let preloadStub;

    afterEach(() => {
      preloadStub?.restore();
      preloadStub = null;
    });

    it("preloads when a Smart Window shows the button", async () => {
      await SpecialPowers.pushPrefEnv({
        set: [["browser.smartwindow.autoTabGrouping.enabled", true]],
      });
      preloadStub = sinon.stub(AutoTabGroupingSuggestions, "preloadModels");

      win = await openAIWindow();

      Assert.ok(
        preloadStub.calledOnce,
        "Opening a Smart Window preloads the models once"
      );
    });

    it("preloads when a window switches to Smart Window", async () => {
      await SpecialPowers.pushPrefEnv({
        set: [["browser.smartwindow.autoTabGrouping.enabled", true]],
      });
      preloadStub = sinon.stub(AutoTabGroupingSuggestions, "preloadModels");

      win = await BrowserTestUtils.openNewBrowserWindow();
      Assert.ok(
        preloadStub.notCalled,
        "A classic window does not preload the models"
      );

      AIWindow.toggleAIWindow(win, true);
      Assert.ok(
        preloadStub.calledOnce,
        "Switching to Smart Window preloads the models once"
      );
    });

    it("does not preload while the button stays hidden", async () => {
      await SpecialPowers.pushPrefEnv({
        set: [["browser.smartwindow.autoTabGrouping.enabled", false]],
      });
      preloadStub = sinon.stub(AutoTabGroupingSuggestions, "preloadModels");

      win = await openAIWindow();

      Assert.ok(
        preloadStub.notCalled,
        "The models are not preloaded while the feature pref is off"
      );
    });

    it("does not download while the preload pref is off", async () => {
      await SpecialPowers.pushPrefEnv({
        set: [
          ["browser.smartwindow.autoTabGrouping.enabled", true],
          ["browser.smartwindow.autoTabGrouping.preloadModels", false],
        ],
      });

      let preloads = 0;
      AutoTabGroupingSuggestions._manager = {
        ...fakeTwoGroupManager(),
        async preloadAllModels() {
          preloads++;
        },
      };

      await AutoTabGroupingSuggestions.preloadModels();

      Assert.equal(
        preloads,
        0,
        "The models are not fetched while the preload pref is off"
      );
    });

    it("shares one download across repeated preloads", async () => {
      await SpecialPowers.pushPrefEnv({
        set: [
          ["browser.smartwindow.autoTabGrouping.enabled", true],
          ["browser.smartwindow.autoTabGrouping.preloadModels", true],
        ],
      });

      let preloads = 0;
      AutoTabGroupingSuggestions._manager = {
        ...fakeTwoGroupManager(),
        async preloadAllModels() {
          preloads++;
        },
      };

      await AutoTabGroupingSuggestions.preloadModels();
      await AutoTabGroupingSuggestions.preloadModels();

      Assert.equal(preloads, 1, "The second preload reuses the first download");
    });

    it("does not preload again after a failed download", async () => {
      await SpecialPowers.pushPrefEnv({
        set: [
          ["browser.smartwindow.autoTabGrouping.enabled", true],
          ["browser.smartwindow.autoTabGrouping.preloadModels", true],
        ],
      });

      let preloads = 0;
      AutoTabGroupingSuggestions._manager = {
        ...fakeTwoGroupManager(),
        async preloadAllModels() {
          preloads++;
          throw new Error("Preload failed");
        },
      };

      await AutoTabGroupingSuggestions.preloadModels();
      await AutoTabGroupingSuggestions.preloadModels();

      Assert.equal(
        preloads,
        1,
        "A failed download is not retried on the next preload"
      );
    });
  });

  describe("creating groups", () => {
    it("shows the button, lists suggestions, and creates all groups", async () => {
      win = await openGroupingWindowWithTabs();

      const button = win.document.getElementById(
        "smartwindow-group-tabs-button-inner"
      );
      await TestUtils.waitForCondition(
        () => BrowserTestUtils.isVisible(button),
        "Group tabs button is visible on a content tab with the pref on"
      );

      const panel = await openPanelWithSuggestions(win);

      await TestUtils.waitForCondition(
        () => button.getAttribute("aria-expanded") === "true",
        "Button is marked expanded while the panel is open"
      );

      const createAll = panel.querySelector(".swgt-create-all");
      Assert.ok(createAll, "'Create all suggested groups' row exists");

      const groupsBefore = win.gBrowser.tabGroups.length;
      createAll.click();
      await TestUtils.waitForCondition(
        () => win.gBrowser.tabGroups.length === groupsBefore + 2,
        "Clicking 'Create all suggested groups' created both groups"
      );

      await TestUtils.waitForCondition(
        () => !panel.querySelectorAll(".swgt-suggestion").length,
        "The created suggestions leave the list"
      );
      Assert.ok(
        win.document.getElementById("smartwindow-group-tabs-panel"),
        "Panel stays open after creating groups"
      );
      Assert.equal(
        button.getAttribute("aria-expanded"),
        "true",
        "Button is still marked expanded"
      );
    });

    it("records the clustering pipeline, and asks again when reopened", async () => {
      win = await openGroupingWindowWithTabs();
      const tabs = String(win.gBrowser.tabs.length);

      await openPanelWithSuggestions(win);

      const opened = Glean.smartWindow.autoTabGroupMenuOpened.testGetValue();
      Assert.equal(opened?.length, 1, "One 'menu opened' event recorded");
      Assert.equal(opened[0].extra.source, "button", "Opened by the button");
      Assert.equal(opened[0].extra.tabs, tabs, "Recorded the open tab count");

      const requested =
        Glean.smartWindow.autoTabGroupingRequested.testGetValue();
      Assert.equal(requested?.length, 1, "One 'requested' event recorded");
      Assert.equal(
        requested[0].extra.tabs,
        tabs,
        "Recorded the open tab count"
      );

      const started = Glean.smartWindow.autoTabGroupingStarted.testGetValue();
      Assert.equal(started?.length, 1, "One 'started' event recorded");
      Assert.greater(
        Number(started[0].extra.total_length),
        0,
        "Reported how much tab-title text the model was given"
      );

      const completed =
        Glean.smartWindow.autoTabGroupingCompleted.testGetValue();
      Assert.equal(completed?.length, 1, "One 'completed' event recorded");
      Assert.equal(completed[0].extra.success, "true", "Clustering succeeded");
      Assert.equal(completed[0].extra.groups, "2", "Suggested two groups");
      Assert.equal(
        completed[0].extra.grouped_tabs,
        "4",
        "Placed four tabs into those groups"
      );
      Assert.greater(
        Number(completed[0].extra.total_length),
        0,
        "Reported how much tab-title text ended up in the suggestions"
      );

      const suggested = Glean.smartWindow.autoTabGroupSuggested.testGetValue();
      Assert.equal(suggested?.length, 2, "One event per suggested group");
      Assert.ok(
        suggested.every(e => e.extra.grouped_tabs === "2"),
        "Each suggested group holds two tabs"
      );
      Assert.equal(
        new Set(suggested.map(e => e.extra.grouped_id)).size,
        2,
        "The two groups are told apart by grouped_id"
      );

      const displayed =
        Glean.smartWindow.autoTabGroupWindowDisplay.testGetValue();
      Assert.equal(displayed?.length, 1, "One 'window display' event recorded");
      Assert.equal(
        displayed[0].extra.suggested_groups,
        "2",
        "Showed both suggestions"
      );
      Assert.equal(displayed[0].extra.groups, "0", "Nothing created yet");

      await closePanel(win);
      await openPanelWithSuggestions(win);
      Assert.equal(
        Glean.smartWindow.autoTabGroupingRequested.testGetValue().length,
        2,
        "Reopening the panel asks for suggestions again"
      );
    });

    it("records accepting every suggestion at once", async () => {
      win = await openGroupingWindowWithTabs();
      const panel = await openPanelWithSuggestions(win);
      const suggestedIds = Glean.smartWindow.autoTabGroupSuggested
        .testGetValue()
        .map(e => e.extra.grouped_id);

      const groupsBefore = win.gBrowser.tabGroups.length;
      panel.querySelector(".swgt-create-all").click();
      await TestUtils.waitForCondition(
        () => win.gBrowser.tabGroups.length === groupsBefore + 2,
        "Both groups created"
      );

      const accepted = Glean.smartWindow.autoTabGroupAccepted.testGetValue();
      Assert.equal(accepted?.length, 2, "One 'accepted' event per group");
      Assert.ok(
        accepted.every(e => e.extra.source === "collective_accept"),
        "Both are recorded as a collective accept"
      );
      Assert.deepEqual(
        accepted.map(e => e.extra.grouped_id),
        suggestedIds,
        "Accepting reports the same grouped_id the suggestion did"
      );

      const completed = Glean.smartWindow.autoTabGroupCompleted.testGetValue();
      Assert.equal(completed?.length, 2, "One 'completed' event per group");
      Assert.ok(
        completed.every(e => e.extra.success === "true"),
        "Both groups were created successfully"
      );
      Assert.ok(
        completed.every(e => e.extra.grouped_tabs === "2"),
        "Each created group holds two tabs"
      );
    });

    it("creates one group at a time and keeps the panel open", async () => {
      win = await openGroupingWindowWithTabs();
      const panel = await openPanelWithSuggestions(win);

      const groupsBefore = win.gBrowser.tabGroups.length;
      panel.querySelector(".swgt-suggestion").click();
      await TestUtils.waitForCondition(
        () => win.gBrowser.tabGroups.length === groupsBefore + 1,
        "Clicking one suggestion created one group"
      );

      const remaining = await TestUtils.waitForCondition(() => {
        const rows = panel.querySelectorAll(".swgt-suggestion");
        return rows.length === 1 ? rows[0] : null;
      }, "The other suggestion is still offered");
      Assert.ok(
        win.document.getElementById("smartwindow-group-tabs-panel"),
        "Panel stays open so the next group can be created"
      );
      Assert.equal(
        panel.querySelectorAll(".swgt-recent-row").length,
        1,
        "The created group is listed under 'Just created'"
      );
      await TestUtils.waitForCondition(
        () => win.document.activeElement === remaining,
        "Focus moves to the suggestion that took its place"
      );

      remaining.click();
      await TestUtils.waitForCondition(
        () => win.gBrowser.tabGroups.length === groupsBefore + 2,
        "The second suggestion created the second group without reopening"
      );
      const accepted = Glean.smartWindow.autoTabGroupAccepted.testGetValue();
      Assert.equal(accepted?.length, 2, "Two 'accepted' events recorded");
      Assert.ok(
        accepted.every(e => e.extra.source === "individual_accept"),
        "Both are recorded as individual accepts"
      );
    });

    it("lists created groups under 'Just created' and ungroups them all", async () => {
      win = await openGroupingWindowWithTabs();
      const panel = await openPanelWithSuggestions(win);

      const groupsBefore = win.gBrowser.tabGroups.length;
      const tabsBefore = win.gBrowser.tabs.length;
      panel.querySelector(".swgt-create-all").click();
      await TestUtils.waitForCondition(
        () => win.gBrowser.tabGroups.length === groupsBefore + 2,
        "Both groups created"
      );
      await TestUtils.waitForCondition(
        () => panel.querySelectorAll(".swgt-recent-row").length === 2,
        "Both created groups are listed under 'Just created'"
      );
      Assert.ok(
        panel.querySelector(".swgt-ungroup"),
        "'Ungroup' footer is shown"
      );
      Assert.equal(
        panel
          .querySelector(".swgt-note .swgt-message")
          .getAttribute("data-l10n-id"),
        "smartwindow-group-tabs-all-sorted",
        "Having created groups, the panel credits the work even with tabs left ungrouped"
      );

      const ungroup = panel.querySelector(".swgt-ungroup");
      ungroup.focus();
      Assert.equal(
        win.document.activeElement,
        ungroup,
        "The 'Ungroup' row can take focus"
      );
      ungroup.click();
      await TestUtils.waitForCondition(
        () => win.gBrowser.tabGroups.length === groupsBefore,
        "Ungroup reverses every created group"
      );
      Assert.equal(
        win.gBrowser.tabs.length,
        tabsBefore,
        "Ungroup keeps the tabs open"
      );
      const requested =
        Glean.smartWindow.autoTabUngroupRequested.testGetValue();
      Assert.equal(requested?.length, 2, "One 'ungroup requested' per group");
      Assert.ok(
        requested.every(e => e.extra.source === "collective_ungroup"),
        "Both are recorded as a collective ungroup"
      );
      const ungrouped =
        Glean.smartWindow.autoTabUngroupCompleted.testGetValue();
      Assert.equal(ungrouped?.length, 2, "One 'ungroup completed' per group");
      Assert.ok(
        ungrouped.every(e => e.extra.success === "true"),
        "Both groups were ungrouped successfully"
      );
      Assert.deepEqual(
        ungrouped.map(e => e.extra.grouped_id),
        requested.map(e => e.extra.grouped_id),
        "Ungrouping reports the grouped_id the group was suggested under"
      );
      await TestUtils.waitForCondition(
        () => !panel.querySelectorAll(".swgt-recent-row").length,
        "The 'Just created' list is cleared"
      );
      await TestUtils.waitForCondition(
        () => win.document.activeElement === panel.querySelector(".swgt-card"),
        "Focus lands on the card once the 'Ungroup' row it was on is gone"
      );
    });

    it("only lists groups created while the panel was open", async () => {
      win = await openGroupingWindowWithTabs();
      let panel = await openPanelWithSuggestions(win);

      panel.querySelector(".swgt-create-all").click();
      await TestUtils.waitForCondition(
        () => panel.querySelectorAll(".swgt-recent-row").length === 2,
        "Both created groups are listed under 'Just created'"
      );

      await closePanel(win);
      panel = await openPanel(win);
      const card = panel.querySelector(".swgt-card");
      await card.updateComplete;

      Assert.ok(
        card.querySelector(".swgt-header"),
        "The reopened card rendered"
      );
      Assert.deepEqual(card.recent, [], "The 'Just created' list starts empty");
      Assert.ok(
        !card.querySelector(
          '[data-l10n-id="smartwindow-group-tabs-just-created-heading"]'
        ),
        "The 'Just created' heading is gone"
      );
      Assert.ok(
        !card.querySelector(".swgt-recent-row"),
        "None of the groups made last time are listed"
      );
      Assert.ok(
        !card.querySelector(".swgt-ungroup"),
        "The 'Ungroup' row is gone with them"
      );
      Assert.equal(
        win.gBrowser.tabGroups.length,
        2,
        "The groups themselves are left alone"
      );
    });
  });

  describe("when there is nothing left to suggest", () => {
    it("credits work done this time and forgets it the next", async () => {
      win = await openGroupingWindowWithTabs();
      let panel = await openPanelWithSuggestions(win);
      const noteId = () =>
        panel
          .querySelector(".swgt-note .swgt-message")
          ?.getAttribute("data-l10n-id");

      panel.querySelector(".swgt-create-all").click();
      await TestUtils.waitForCondition(
        () => panel.querySelectorAll(".swgt-recent-row").length === 2,
        "Both created groups are listed under 'Just created'"
      );
      Assert.equal(
        noteId(),
        "smartwindow-group-tabs-all-sorted",
        "Groups created this time earn the 'nice work' note"
      );

      await closePanel(win);
      panel = await openPanel(win);
      await TestUtils.waitForCondition(
        () => noteId() === "smartwindow-group-tabs-empty",
        "Reopened with nothing created, the panel just says it has no suggestions"
      );
    });
  });

  describe("closing duplicate tabs", () => {
    it("hides the row when there is nothing to close", async () => {
      win = await openGroupingWindowWithTabs();
      const panel = await openPanelWithSuggestions(win);

      Assert.ok(
        !panel.querySelector(".swgt-close-duplicates"),
        "No 'Close Duplicate Tabs' row while every tab is unique"
      );
    });

    it("closes the duplicates and leaves one of each tab", async () => {
      await SpecialPowers.pushPrefEnv({
        set: [
          ["browser.smartwindow.autoTabGrouping.enabled", true],
          // Skip the one-time "we'll close duplicate tabs" confirmation.
          ["browser.tabs.haveShownCloseAllDuplicateTabsWarning", true],
        ],
      });
      win = await openAIWindow();
      await navigateToContent(win);
      await addWebTabs(win);
      await addWebTabs(win, ["a", "b"]);
      const tabsBefore = win.gBrowser.tabs.length;

      const panel = await openPanelWithSuggestions(win);
      const row = await TestUtils.waitForCondition(
        () => panel.querySelector(".swgt-close-duplicates"),
        "The 'Close Duplicate Tabs' row appears once duplicates exist"
      );

      const duplicates = win.gBrowser.getAllDuplicateTabsToClose().length;
      Assert.equal(duplicates, 2, "The two added tabs are the duplicates");
      Assert.equal(
        JSON.parse(row.getAttribute("data-l10n-args")).tabCount,
        duplicates,
        "The row is told how many tabs activating it closes"
      );
      const accService = Cc["@mozilla.org/accessibilityService;1"].getService(
        Ci.nsIAccessibilityService
      );
      await TestUtils.waitForCondition(
        () => accService.getAccessibleFor(row)?.name?.includes(`${duplicates}`),
        "The row's label reports that count"
      );

      const hintShown = BrowserTestUtils.waitForEvent(
        win.document,
        "popupshown",
        true,
        event => event.target.id === "confirmation-hint"
      );
      row.click();
      const { target: hint } = await hintShown;
      Assert.equal(
        hint.anchorNode?.id,
        "smartwindow-group-tabs-button-inner",
        "The hint points at our button, not the All Tabs button which may be absent"
      );
      await TestUtils.waitForCondition(
        () => win.gBrowser.tabs.length === tabsBefore - 2,
        "Both duplicate tabs are closed"
      );
      const urls = win.gBrowser.tabs.map(t => t.linkedBrowser.currentURI.spec);
      Assert.equal(
        new Set(urls).size,
        urls.length,
        "Every remaining tab is at a distinct URL"
      );

      const requested =
        Glean.smartWindow.closeDuplicateTabsRequested.testGetValue();
      Assert.equal(requested?.length, 1, "One 'requested' event recorded");
      Assert.equal(
        requested[0].extra.tabs,
        String(tabsBefore),
        "Recorded the tab count from before the duplicates were closed"
      );

      const started =
        Glean.smartWindow.closeDuplicateTabsStarted.testGetValue();
      Assert.equal(started?.length, 1, "One 'started' event recorded");
      Assert.greater(
        Number(started[0].extra.total_length),
        0,
        "Reported how much tab-title text was searched"
      );

      const completed =
        Glean.smartWindow.closeDuplicateTabsCompleted.testGetValue();
      Assert.equal(completed?.length, 1, "One 'completed' event recorded");
      Assert.equal(completed[0].extra.success, "true", "The search succeeded");
      Assert.equal(
        completed[0].extra.duplicate_tabs,
        "2",
        "Found both duplicates"
      );

      const closed = Glean.smartWindow.duplicateTabsClosed.testGetValue();
      Assert.equal(closed?.length, 1, "One 'closed' event recorded");
      Assert.equal(closed[0].extra.duplicate_tabs, "2", "Closed both");
      Assert.equal(closed[0].extra.success, "true", "Closing succeeded");
    });
  });

  describe("viewing existing tab groups", () => {
    it("offers the row once groups exist and switches to the one clicked", async () => {
      win = await openGroupingWindowWithTabs();
      const panel = await openPanelWithSuggestions(win);

      Assert.equal(
        win.gBrowser.getAllTabGroups().length,
        0,
        "No tab group exists yet"
      );
      Assert.ok(
        !panel.querySelector(".swgt-view-tab-groups"),
        "No 'View Tab Groups' row while the user has no groups"
      );

      panel.querySelector(".swgt-create-all").click();
      const row = await TestUtils.waitForCondition(
        () => panel.querySelector(".swgt-view-tab-groups"),
        "The row appears once the created groups exist"
      );
      Assert.equal(
        row.querySelector(".swgt-row-label").getAttribute("data-l10n-id"),
        "smartwindow-group-tabs-view-tab-groups",
        "The row's label is localized via Fluent"
      );

      row.click();
      const list = await TestUtils.waitForCondition(
        () => panel._flyoutPanel?.querySelector("tab-groups-list"),
        "Activating the row opens the tab groups list"
      );
      Assert.equal(
        panel._flyoutPanel
          .querySelector(".swgt-flyout-list")
          .getAttribute("data-l10n-id"),
        "smartwindow-group-tabs-groups-list",
        "The list's accessible name is localized via Fluent"
      );
      Assert.equal(
        row.getAttribute("aria-expanded"),
        "true",
        "The row reports its list as expanded"
      );
      await TestUtils.waitForCondition(
        () => panel._flyoutPanel.contains(win.document.activeElement),
        "Activating the row moves focus into the list"
      );

      // Reopening must rebuild the list, so groups created in between show up.
      AutoTabGrouping._hideFlyout(panel);
      row.click();
      const groupRows = await TestUtils.waitForCondition(() => {
        const rebuilt = panel._flyoutPanel.querySelector("tab-groups-list");
        const rows = rebuilt?.querySelectorAll(".tab-group-row");
        return rebuilt !== list && rows?.length === 2 ? [...rows] : null;
      }, "Reopening builds a fresh list of both groups");

      const group = win.gBrowser.getTabGroupById(
        groupRows[1].dataset.tabGroupId
      );
      Assert.ok(group, "Each row names a tab group");
      groupRows[1].click();
      await TestUtils.waitForCondition(
        () => !win.document.getElementById("smartwindow-group-tabs-panel"),
        "Selecting a group closes the panel"
      );
      Assert.equal(
        win.gBrowser.selectedTab.group,
        group,
        "The clicked group's tab is selected"
      );
    });

    it("takes focus when the list replaces an open suggestion flyout", async () => {
      win = await openGroupingWindowWithTabs();
      const panel = await openPanelWithSuggestions(win);

      panel.querySelector(".swgt-suggestion").click();
      const row = await TestUtils.waitForCondition(
        () => panel.querySelector(".swgt-view-tab-groups"),
        "The row appears once the created group exists"
      );
      const suggestion = panel.querySelector(".swgt-suggestion");
      suggestion.dispatchEvent(new win.MouseEvent("mouseenter"));
      await TestUtils.waitForCondition(
        () =>
          panel._flyoutPanel?.state === "open" &&
          panel._flyoutPanel.querySelector(".swgt-flyout-tab"),
        "The remaining suggestion's flyout is open"
      );

      row.click();
      await TestUtils.waitForCondition(
        () => win.document.activeElement?.classList.contains("tab-group-row"),
        "Focus lands in the tab groups list, not on the render it replaced"
      );
    });
  });

  describe("previewing a suggested group", () => {
    // Open the flyout for the first suggestion and return its tab rows. The
    // opening popup takes focus off the row, so put it back once it settles.
    async function openFlyout(panel) {
      const row = panel.querySelector(".swgt-suggestion");
      row.focus();
      await TestUtils.waitForCondition(
        () =>
          panel._flyoutPanel?.state === "open" &&
          panel._flyoutPanel.querySelectorAll(".swgt-flyout-tab").length,
        "The flyout is open and lists the group's tabs"
      );
      row.focus();
      return {
        row,
        tabRows: [...panel._flyoutPanel.querySelectorAll(".swgt-flyout-tab")],
      };
    }

    it("hides the flyout once the pointer leaves it", async () => {
      win = await openGroupingWindowWithTabs();
      const panel = await openPanelWithSuggestions(win);
      const { row } = await openFlyout(panel);

      row.blur();
      panel._flyoutPanel.dispatchEvent(new win.MouseEvent("mouseleave"));
      Assert.notEqual(
        panel._hideTimer,
        0,
        "Leaving the flyout schedules a hide"
      );
      await TestUtils.waitForCondition(
        () => panel._flyoutPanel.state === "closed",
        "The flyout hides itself once the pointer has left"
      );

      panel.querySelector(".swgt-create-all").focus();
      row.focus();
      await TestUtils.waitForCondition(
        () => panel._activeRow === row && panel._flyoutPanel.state !== "closed",
        "Focusing the row again reopens the flyout the pointer dismissed"
      );
    });

    it("hides the flyout when another panel row is hovered", async () => {
      win = await openGroupingWindowWithTabs();
      const panel = await openPanelWithSuggestions(win);
      const { row } = await openFlyout(panel);

      row.blur();
      row.dispatchEvent(new win.MouseEvent("mouseleave"));
      panel
        .querySelector(".swgt-create-all")
        .dispatchEvent(new win.MouseEvent("mouseover", { bubbles: true }));

      Assert.ok(
        !row.classList.contains("is-active"),
        "The suggestion drops its highlight the moment the pointer moves on"
      );
      Assert.equal(
        panel._activeRow,
        null,
        "The suggestion row is no longer marked active"
      );
      Assert.equal(panel._hideTimer, 0, "No delayed hide is left pending");
      await TestUtils.waitForCondition(
        () => panel._flyoutPanel.state === "closed",
        "Moving onto 'Create Groups' closes the suggestion's flyout"
      );

      panel.querySelector(".swgt-create-all").focus();
      row.focus();
      await TestUtils.waitForCondition(
        () => panel._activeRow === row && panel._flyoutPanel.state !== "closed",
        "Keyboard focus on that suggestion still opens its flyout afterwards"
      );
    });

    it("keeps the flyout the keyboard opened when the pointer drifts away", async () => {
      win = await openGroupingWindowWithTabs();
      const panel = await openPanelWithSuggestions(win);
      const { tabRows } = await openFlyout(panel);
      const rows = [...panel.querySelectorAll(".swgt-suggestion")];
      const suggestion = AutoTabGrouping._getState(win).suggestions[0];

      EventUtils.synthesizeKey("KEY_ArrowRight", {}, win);
      await TestUtils.waitForCondition(
        () => win.document.activeElement === tabRows[0],
        "Focus moved into the flyout"
      );

      rows[1].dispatchEvent(new win.MouseEvent("mouseenter"));
      rows[1].dispatchEvent(new win.MouseEvent("mouseleave"));

      Assert.equal(
        panel._flyoutPanel._flyoutEl.suggestion,
        suggestion,
        "The flyout still lists the group the keyboard drilled into"
      );
      Assert.equal(
        panel._hideTimer,
        0,
        "Pointing away does not schedule a hide while the flyout holds focus"
      );
    });

    it("lists the group's tabs and switches to the one clicked", async () => {
      win = await openGroupingWindowWithTabs();
      const panel = await openPanelWithSuggestions(win);
      const { tabRows } = await openFlyout(panel);

      Assert.equal(
        panel._flyoutPanel.getAttribute("flip"),
        "slide",
        "The flyout slides rather than flipping when it meets a screen edge"
      );

      const suggestion = AutoTabGrouping._getState(win).suggestions[0];
      Assert.equal(
        tabRows.length,
        suggestion.tabs.length,
        "One row per tab in the suggested group"
      );

      tabRows[1].click();
      await TestUtils.waitForCondition(
        () => !win.document.getElementById("smartwindow-group-tabs-panel"),
        "Selecting a tab closes the panel"
      );
      Assert.equal(
        win.gBrowser.selectedTab,
        suggestion.tabs[1],
        "The clicked tab is selected"
      );
      Assert.equal(
        win.gBrowser.tabGroups.length,
        0,
        "Previewing a group's tabs does not create the group"
      );
    });

    it("moves focus into the flyout and back out again", async () => {
      win = await openGroupingWindowWithTabs();
      const panel = await openPanelWithSuggestions(win);
      const { row, tabRows } = await openFlyout(panel);

      EventUtils.synthesizeKey("KEY_ArrowRight", {}, win);
      await TestUtils.waitForCondition(
        () => win.document.activeElement === tabRows[0],
        "The forward arrow moves focus to the first tab"
      );

      EventUtils.synthesizeKey("KEY_ArrowDown", {}, win);
      Assert.equal(
        win.document.activeElement,
        tabRows[1],
        "ArrowDown moves to the next tab"
      );

      let escapeEvent;
      win.addEventListener(
        "keydown",
        e => {
          escapeEvent = e;
        },
        { once: true }
      );
      EventUtils.synthesizeKey("KEY_Escape", {}, win);
      Assert.equal(
        win.document.activeElement,
        row,
        "Escape steps back out to the suggestion row"
      );
      Assert.ok(
        escapeEvent.defaultPrevented,
        "Escape is consumed instead of reaching the window's own Escape key"
      );
      Assert.ok(
        win.document.getElementById("smartwindow-group-tabs-panel"),
        "Escape inside the flyout leaves the panel open"
      );
      await TestUtils.waitForCondition(
        () => panel._flyoutPanel.state === "closed",
        "Escape closes the flyout it backed out of"
      );

      EventUtils.synthesizeKey("KEY_ArrowLeft", {}, win);
      await TestUtils.waitForCondition(
        () => win.document.activeElement?.matches(".swgt-flyout-tab"),
        "Either arrow re-enters the flyout, whichever side it opens on"
      );

      EventUtils.synthesizeKey("KEY_Escape", {}, win);
      await TestUtils.waitForCondition(
        () => panel._flyoutPanel.state === "closed",
        "The flyout is closed again"
      );
      EventUtils.synthesizeKey("KEY_Escape", {}, win);
      await TestUtils.waitForCondition(
        () => !win.document.getElementById("smartwindow-group-tabs-panel"),
        "A second Escape, with no flyout left to close, closes the panel"
      );
    });

    it("closes only the flyout when Escape is pressed while pointing at it", async () => {
      win = await openGroupingWindowWithTabs();
      const panel = await openPanelWithSuggestions(win);
      await openFlyout(panel);

      // Focus stays on the card: hovering a row opens the flyout without
      // moving focus, which is the case that used to close both panels.
      Assert.ok(
        !panel._flyoutPanel.contains(win.document.activeElement),
        "Focus is outside the flyout"
      );

      EventUtils.synthesizeKey("KEY_Escape", {}, win);
      await TestUtils.waitForCondition(
        () => panel._flyoutPanel.state === "closed",
        "Escape closes the pointer-opened flyout"
      );
      Assert.ok(
        win.document.getElementById("smartwindow-group-tabs-panel"),
        "The panel itself stays open"
      );
    });

    it("drops a focus request when the flyout hides before it opens", async () => {
      win = await openGroupingWindowWithTabs();
      const panel = await openPanelWithSuggestions(win);
      const rows = [...panel.querySelectorAll(".swgt-suggestion")];
      const suggestions = AutoTabGrouping._getState(win).suggestions;

      AutoTabGrouping._showFlyoutById(win, panel, suggestions[0].id, rows[0]);
      AutoTabGrouping._focusFlyout(panel);
      AutoTabGrouping._hideFlyout(panel);

      rows[1].focus();
      await BrowserTestUtils.waitForEvent(panel._flyoutPanel, "popupshown");
      Assert.equal(
        win.document.activeElement,
        rows[1],
        "Reopening the flyout on hover does not steal focus into it"
      );
    });
  });

  describe("moving through the panel's rows with the keyboard", () => {
    let flyoutStub;

    beforeEach(() => {
      // Focusing a row opens its flyout, and the opening popup takes focus off
      // the row; the flyout's own navigation is covered above.
      flyoutStub = sinon.stub(AutoTabGrouping, "_showFlyoutById");
    });

    afterEach(() => {
      flyoutStub.restore();
    });

    it("wraps around with the arrow keys and jumps with Home and End", async () => {
      win = await openGroupingWindowWithTabs();
      const panel = await openPanelWithSuggestions(win);
      const card = panel.querySelector(".swgt-card");
      const rows = [...panel.querySelectorAll(".swgt-row")];
      Assert.equal(rows.length, 3, "'Create all' plus the two suggestions");
      if (win.document.activeElement !== card) {
        await BrowserTestUtils.waitForEvent(card, "focus");
      }

      EventUtils.synthesizeKey("KEY_ArrowDown", {}, win);
      Assert.equal(
        win.document.activeElement,
        rows[0],
        "ArrowDown enters the list at the first row"
      );

      EventUtils.synthesizeKey("KEY_ArrowDown", {}, win);
      Assert.equal(
        win.document.activeElement,
        rows[1],
        "ArrowDown moves to the next row"
      );

      EventUtils.synthesizeKey("KEY_ArrowUp", {}, win);
      Assert.equal(
        win.document.activeElement,
        rows[0],
        "ArrowUp moves back to the previous row"
      );

      EventUtils.synthesizeKey("KEY_ArrowUp", {}, win);
      Assert.equal(
        win.document.activeElement,
        rows.at(-1),
        "ArrowUp from the first row wraps to the last"
      );

      EventUtils.synthesizeKey("KEY_ArrowDown", {}, win);
      Assert.equal(
        win.document.activeElement,
        rows[0],
        "ArrowDown from the last row wraps to the first"
      );

      EventUtils.synthesizeKey("KEY_End", {}, win);
      Assert.equal(
        win.document.activeElement,
        rows.at(-1),
        "End jumps to the last row"
      );

      EventUtils.synthesizeKey("KEY_Home", {}, win);
      Assert.equal(
        win.document.activeElement,
        rows[0],
        "Home jumps back to the first row"
      );

      EventUtils.synthesizeKey("KEY_ArrowDown", { accelKey: true }, win);
      Assert.equal(
        win.document.activeElement,
        rows[0],
        "A modified arrow is left alone"
      );
    });
  });

  describe("panel localization and keyboard access", () => {
    it("localizes strings and supports keyboard operation", async () => {
      await SpecialPowers.pushPrefEnv({
        set: [["browser.smartwindow.autoTabGrouping.enabled", true]],
      });

      win = await openAIWindow();
      await navigateToContent(win);
      await addWebTabs(win);

      const button = win.document.getElementById(
        "smartwindow-group-tabs-button-inner"
      );
      const popupSet = win.document.getElementById("mainPopupSet");

      AIWindowUI.toggleGroupTabsPanel(win);
      const panel = await BrowserTestUtils.waitForMutationCondition(
        popupSet,
        { childList: true },
        () => win.document.getElementById("smartwindow-group-tabs-panel")
      );
      const card = panel.querySelector(".swgt-card");
      await BrowserTestUtils.waitForMutationCondition(
        card,
        { childList: true, subtree: true },
        () => card.querySelectorAll(".swgt-suggestion").length === 2
      );

      Assert.equal(card.getAttribute("role"), "dialog", "Card is a dialog");
      if (win.document.activeElement !== card) {
        await BrowserTestUtils.waitForEvent(card, "focus");
      }
      Assert.equal(
        win.document.activeElement,
        card,
        "Focus moves to the dialog card when the panel opens"
      );

      const heading = panel.querySelector(".swgt-header");
      Assert.equal(
        heading.getAttribute("data-l10n-id"),
        "smartwindow-group-tabs-panel-heading",
        "Heading is localized via Fluent"
      );
      const createAll = panel.querySelector(".swgt-create-all");
      Assert.equal(
        createAll.getAttribute("data-l10n-id"),
        "smartwindow-group-tabs-create-all",
        "'Create all' label is localized via Fluent"
      );
      const suggestionRow = panel.querySelector(".swgt-suggestion");
      Assert.equal(
        suggestionRow.getAttribute("data-l10n-id"),
        "smartwindow-group-tabs-suggestion",
        "Suggestion row's accessible name is localized via Fluent"
      );
      const accService = Cc["@mozilla.org/accessibilityService;1"].getService(
        Ci.nsIAccessibilityService
      );
      await TestUtils.waitForCondition(() => {
        const acc = accService.getAccessibleFor(suggestionRow);
        return acc && acc.name && acc.name.trim();
      }, "Suggestion row exposes a non-empty accessible name");

      const favicons = suggestionRow.querySelector(".swgt-favicons");
      Assert.equal(
        favicons.getAttribute("aria-hidden"),
        "true",
        "Tab favicons are hidden from assistive technology"
      );
      const images = [...favicons.querySelectorAll("img.swgt-favicon")];
      Assert.greater(images.length, 0, "Suggestion row shows tab favicons");
      Assert.ok(
        images.every(img => img.src.startsWith("page-icon:")),
        "Each favicon loads through page-icon:"
      );

      suggestionRow.focus();
      const flyoutList = await BrowserTestUtils.waitForMutationCondition(
        popupSet,
        { childList: true, subtree: true },
        () => panel._flyoutPanel?.querySelector(".swgt-flyout-list")
      );
      Assert.equal(
        flyoutList.getAttribute("data-l10n-id"),
        "smartwindow-group-tabs-flyout-list",
        "Flyout's accessible name is localized via Fluent"
      );
      Assert.equal(
        suggestionRow.getAttribute("aria-expanded"),
        "true",
        "Focused suggestion row reports its flyout as expanded"
      );

      EventUtils.synthesizeKey("KEY_Escape", {}, win);
      await TestUtils.waitForCondition(
        () => panel._flyoutPanel.state === "closed",
        "The first Escape closes the open flyout"
      );

      const focusReturned = BrowserTestUtils.waitForEvent(button, "focus");
      EventUtils.synthesizeKey("KEY_Escape", {}, win);
      await focusReturned;
      Assert.ok(
        !win.document.getElementById("smartwindow-group-tabs-panel"),
        "A second Escape closes the panel"
      );
      Assert.equal(
        win.document.activeElement,
        button,
        "Focus returns to the toolbar button after Escape"
      );
    });
  });

  describe("clustering edge cases", () => {
    it("falls back to the empty state when clustering times out", async () => {
      await SpecialPowers.pushPrefEnv({
        set: [
          ["browser.smartwindow.autoTabGrouping.enabled", true],
          ["browser.smartwindow.autoTabGrouping.timeoutMs", 50],
        ],
      });

      AutoTabGroupingSuggestions._manager = {
        ...fakeTwoGroupManager(),
        generateClusters() {
          return new Promise(() => {});
        },
      };

      win = await openAIWindow();
      await navigateToContent(win);
      await addWebTabs(win);

      AIWindowUI.toggleGroupTabsPanel(win);
      const panel = await TestUtils.waitForCondition(() =>
        win.document.getElementById("smartwindow-group-tabs-panel")
      );

      await TestUtils.waitForCondition(
        () =>
          panel.querySelector(".swgt-message")?.getAttribute("data-l10n-id") ===
          "smartwindow-group-tabs-empty",
        "Panel falls back to the empty state after the clustering timeout"
      );

      const completed =
        Glean.smartWindow.autoTabGroupingCompleted.testGetValue();
      Assert.equal(completed?.length, 1, "The timed-out run still completes");
      Assert.equal(
        completed[0].extra.success,
        "false",
        "Recorded as a failure"
      );
      Assert.equal(
        completed[0].extra.error_type,
        "TimeoutError",
        "Recorded the error's name, not its message"
      );
      Assert.equal(completed[0].extra.groups, "0", "Nothing was suggested");
      Assert.ok(
        !Glean.smartWindow.autoTabGroupSuggested.testGetValue(),
        "No group was suggested"
      );
    });

    it("records a request with no start when there are too few tabs", async () => {
      await SpecialPowers.pushPrefEnv({
        set: [["browser.smartwindow.autoTabGrouping.enabled", true]],
      });
      win = await openAIWindow();
      await navigateToContent(win);

      AIWindowUI.toggleGroupTabsPanel(win);
      const panel = await TestUtils.waitForCondition(() =>
        win.document.getElementById("smartwindow-group-tabs-panel")
      );
      await TestUtils.waitForCondition(
        () =>
          panel.querySelector(".swgt-message")?.getAttribute("data-l10n-id") ===
          "smartwindow-group-tabs-empty",
        "Panel says it has nothing to suggest"
      );

      Assert.equal(
        Glean.smartWindow.autoTabGroupingRequested.testGetValue()?.length,
        1,
        "Opening the panel still asks for suggestions"
      );
      Assert.ok(
        !Glean.smartWindow.autoTabGroupingStarted.testGetValue(),
        "The model never ran, so nothing started"
      );
      Assert.ok(
        !Glean.smartWindow.autoTabGroupingCompleted.testGetValue(),
        "And nothing completed"
      );
    });

    it("renders suggestions when reopened while clustering is still running", async () => {
      await SpecialPowers.pushPrefEnv({
        set: [["browser.smartwindow.autoTabGrouping.enabled", true]],
      });

      let releaseClusters;
      const clustersReady = new Promise(resolve => {
        releaseClusters = resolve;
      });
      const fakeManager = fakeTwoGroupManager();
      AutoTabGroupingSuggestions._manager = {
        ...fakeManager,
        async generateClusters(tabList) {
          await clustersReady;
          return fakeManager.generateClusters(tabList);
        },
      };

      win = await openAIWindow();
      await navigateToContent(win);
      await addWebTabs(win);

      const button = win.document.getElementById(
        "smartwindow-group-tabs-button-inner"
      );
      await TestUtils.waitForCondition(() =>
        BrowserTestUtils.isVisible(button)
      );

      AIWindowUI.toggleGroupTabsPanel(win);
      let panel = await TestUtils.waitForCondition(() =>
        win.document.getElementById("smartwindow-group-tabs-panel")
      );
      await TestUtils.waitForCondition(
        () =>
          panel.querySelector(".swgt-loading")?.getAttribute("data-l10n-id") ===
          "smartwindow-group-tabs-loading",
        "Panel shows the loading state while clustering is pending"
      );

      AIWindowUI.toggleGroupTabsPanel(win);
      await TestUtils.waitForCondition(
        () => !win.document.getElementById("smartwindow-group-tabs-panel"),
        "Panel closed while clustering was in flight"
      );

      AIWindowUI.toggleGroupTabsPanel(win);
      panel = await TestUtils.waitForCondition(() =>
        win.document.getElementById("smartwindow-group-tabs-panel")
      );

      releaseClusters();

      await TestUtils.waitForCondition(
        () => panel.querySelectorAll(".swgt-suggestion").length === 2,
        "Reopened panel renders suggestions once clustering settles"
      );
    });
  });
});
