/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

describe("Auto Tab Grouping toolbar button", () => {
  let win, mockEngineManager;

  beforeEach(() => {
    mockEngineManager = setupAutoTabGroupingTest();
  });

  afterEach(async () => {
    await cleanupAutoTabGroupingTest(win, mockEngineManager);
    win = null;
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

  describe("clustering edge cases", () => {
    describe("when clustering times out", () => {
      let finishClustering;

      beforeEach(async () => {
        await SpecialPowers.pushPrefEnv({
          set: [
            ["browser.smartwindow.autoTabGrouping.enabled", true],
            ["browser.smartwindow.autoTabGrouping.timeoutMs", 50],
          ],
        });

        // Clustering that does not finish until the test says so, as the
        // models may not right after startup.
        const fake = fakeTwoGroupManager();
        AutoTabGroupingSuggestions._manager = {
          ...fake,
          generateClusters(tabList) {
            return new Promise(resolve => {
              finishClustering = () => resolve(fake.generateClusters(tabList));
            });
          },
        };
      });

      afterEach(() => {
        finishClustering?.();
        finishClustering = null;
      });

      it("stops waiting on slow clustering, then fills in its groups", async () => {
        win = await openAIWindow();
        await navigateToContent(win);
        await addWebTabs(win);

        AutoTabGrouping.toggleGroupTabsPanel(win);
        const panel = await TestUtils.waitForCondition(() =>
          win.document.getElementById("smartwindow-group-tabs-panel")
        );

        await TestUtils.waitForCondition(
          () =>
            panel
              .querySelector(".swgt-message")
              ?.getAttribute("data-l10n-id") === "smartwindow-group-tabs-empty",
          "Panel falls back to the empty state once the wait runs out"
        );
        Assert.ok(
          AutoTabGrouping._getState(win).computing,
          "The clustering run itself is still going"
        );
        Assert.ok(
          !Glean.smartWindow.autoTabGroupingCompleted.testGetValue(),
          "Nothing is recorded as completed while the run is still going"
        );
        await TestUtils.waitForCondition(
          () => Glean.smartWindow.autoTabGroupWindowDisplay.testGetValue(),
          "The panel records what it showed"
        );
        const displayed =
          Glean.smartWindow.autoTabGroupWindowDisplay.testGetValue();
        Assert.equal(
          displayed.length,
          1,
          "One 'window display' event recorded"
        );
        Assert.equal(
          displayed[0].extra.waited_out,
          "true",
          "Recorded that the panel stopped waiting on clustering"
        );
        Assert.equal(
          displayed[0].extra.suggested_groups,
          "0",
          "Nothing had been found by then"
        );

        finishClustering();
        await TestUtils.waitForCondition(
          () => panel.querySelectorAll(".swgt-suggestion").length === 2,
          "The groups fill the panel in once the slow run finishes"
        );
        Assert.ok(
          !panel.querySelector(".swgt-message"),
          "The empty state is gone"
        );
        const completed =
          Glean.smartWindow.autoTabGroupingCompleted.testGetValue();
        Assert.equal(completed?.length, 1, "The run completes once");
        Assert.equal(
          completed[0].extra.success,
          "true",
          "Taking longer than the panel waits is not a failure"
        );
        Assert.equal(completed[0].extra.groups, "2", "Both groups were found");
        Assert.equal(
          Glean.smartWindow.autoTabGroupSuggested.testGetValue()?.length,
          2,
          "Both groups are recorded as suggested"
        );
      });
    });

    it("records a request with no start when there are too few tabs", async () => {
      await SpecialPowers.pushPrefEnv({
        set: [["browser.smartwindow.autoTabGrouping.enabled", true]],
      });
      win = await openAIWindow();
      await navigateToContent(win);

      AutoTabGrouping.toggleGroupTabsPanel(win);
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
        "smartwindow-group-tabs-button"
      );
      await TestUtils.waitForCondition(() =>
        BrowserTestUtils.isVisible(button)
      );

      AutoTabGrouping.toggleGroupTabsPanel(win);
      let panel = await TestUtils.waitForCondition(() =>
        win.document.getElementById("smartwindow-group-tabs-panel")
      );
      await TestUtils.waitForCondition(
        () =>
          panel.querySelector(".swgt-loading")?.getAttribute("data-l10n-id") ===
          "smartwindow-group-tabs-loading",
        "Panel shows the loading state while clustering is pending"
      );

      AutoTabGrouping.toggleGroupTabsPanel(win);
      await TestUtils.waitForCondition(
        () => !win.document.getElementById("smartwindow-group-tabs-panel"),
        "Panel closed while clustering was in flight"
      );

      AutoTabGrouping.toggleGroupTabsPanel(win);
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
