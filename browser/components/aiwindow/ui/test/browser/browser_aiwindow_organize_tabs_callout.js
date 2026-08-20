/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { SpecialMessageActions } = ChromeUtils.importESModule(
  "resource://messaging-system/lib/SpecialMessageActions.sys.mjs"
);
const { AutoTabGroupingSuggestions } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/ui/modules/AutoTabGroupingSuggestions.sys.mjs"
);

add_setup(async function setup() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.smartwindow.autoTabGrouping.enabled", true],
      ["browser.smartwindow.firstrun.hasCompleted", true],
      ["browser.ml.enable", true],
      ["browser.ml.modelHubRootUrl", "http://localhost:0/"],
      ["browser.tabs.groups.smart.enabled", true],
      ["browser.tabs.groups.smart.userEnabled", true],
    ],
  });

  const originalManager = AutoTabGroupingSuggestions._manager;
  AutoTabGroupingSuggestions._manager = {
    async generateClusters() {
      return { clusterRepresentations: [] };
    },
    async getPredictedLabelForGroup() {
      return "";
    },
  };
  // Force the on-device naming path so buildProposals never does real
  // FxA-token/network work if a group is ever labeled (matches the other ATG
  // tests).
  const originalLlmLabel = AutoTabGroupingSuggestions._llmLabelForGroup;
  AutoTabGroupingSuggestions._llmLabelForGroup = async () => {
    throw new Error("force on-device");
  };
  registerCleanupFunction(() => {
    AutoTabGroupingSuggestions._manager = originalManager;
    AutoTabGroupingSuggestions._llmLabelForGroup = originalLlmLabel;
  });
});

add_task(async function test_primary_action_opens_the_panel() {
  Services.fog.testResetFOG();
  const win = await openAIWindow();
  const url = "https://example.com/";
  const loaded = BrowserTestUtils.browserLoaded(
    win.gBrowser.selectedBrowser,
    false,
    url
  );
  BrowserTestUtils.startLoadingURIString(win.gBrowser.selectedBrowser, url);
  await loaded;

  await SpecialMessageActions.handleAction(
    {
      type: "OPEN_ORGANIZE_TABS_PANEL",
      data: { source: "callout_click" },
    },
    win.gBrowser.selectedBrowser
  );

  const panel = await TestUtils.waitForCondition(
    () => win.document.getElementById("smartwindow-group-tabs-panel"),
    "The callout's primary action opened the Organize Tabs panel"
  );
  await TestUtils.waitForCondition(
    () => panel.state === "open",
    "The panel finished opening"
  );

  const opened = Glean.smartWindow.autoTabGroupMenuOpened.testGetValue();
  Assert.equal(opened?.length, 1, "One 'menu opened' event recorded");
  Assert.equal(
    opened[0].extra.source,
    "callout_click",
    "The callout is credited with opening the panel"
  );

  await BrowserTestUtils.closeWindow(win);
});
