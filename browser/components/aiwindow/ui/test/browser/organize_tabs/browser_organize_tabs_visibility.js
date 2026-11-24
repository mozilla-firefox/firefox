/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

async function assertButtonHiddenOnContent(win, reason) {
  // The Ask button and the group-tabs button toggle together, so waiting for
  // the Ask button to show means the immersive view has been left; only then is
  // the group-tabs button's visibility down to the feature gate.
  const askButton = win.document.getElementById("smartwindow-ask-button");
  await TestUtils.waitForCondition(
    () => BrowserTestUtils.isVisible(askButton),
    "Left the immersive view (the Ask button is shown)"
  );
  const button = win.document.getElementById("smartwindow-group-tabs-button");
  Assert.ok(
    !AutoTabGroupingSuggestions.isAvailable,
    `Feature is unavailable: ${reason}`
  );
  Assert.ok(
    BrowserTestUtils.isHidden(button),
    `Group tabs button is hidden: ${reason}`
  );
}

describe("Auto Tab Grouping toolbar button", () => {
  let win, mockEngineManager;

  beforeEach(() => {
    mockEngineManager = setupAutoTabGroupingTest();
  });

  afterEach(async () => {
    await cleanupAutoTabGroupingTest(win, mockEngineManager);
    win = null;
  });

  describe("visibility gating", () => {
    it("is not registered when the feature pref is off", async () => {
      await SpecialPowers.pushPrefEnv({
        set: [["browser.smartwindow.autoTabGrouping.enabled", false]],
      });

      win = await openAIWindow();
      Assert.equal(
        win.document.getElementById("smartwindow-group-tabs-button"),
        null,
        "Group tabs button is absent while the feature pref is off"
      );
      Assert.ok(
        !CustomizableUI.getUnusedWidgets(win.gNavToolbox.palette).some(
          widget => widget.id == "smartwindow-group-tabs-button"
        ),
        "Group tabs button is not in the customize palette either"
      );
    });

    it("follows the feature pref while a window is open", async () => {
      win = await openGroupingWindowWithTabs();
      const buttonId = "smartwindow-group-tabs-button";
      Assert.ok(
        win.document.getElementById(buttonId),
        "Group tabs button exists while the feature pref is on"
      );

      try {
        Services.prefs.setBoolPref(
          "browser.smartwindow.autoTabGrouping.enabled",
          false
        );
        Assert.equal(
          win.document.getElementById(buttonId),
          null,
          "Turning the feature pref off takes the button out of the toolbar"
        );
      } finally {
        Services.prefs.clearUserPref(
          "browser.smartwindow.autoTabGrouping.enabled"
        );
      }
      Assert.ok(
        win.document.getElementById(buttonId),
        "Turning the feature pref back on puts the button back"
      );
    });

    it("follows supportedLocales while a window is open", async () => {
      win = await openGroupingWindowWithTabs();
      const button = win.document.getElementById(
        "smartwindow-group-tabs-button"
      );
      Assert.ok(!button.hidden, "Group tabs button is shown in en-US");

      try {
        Services.prefs.setCharPref(
          "browser.smartwindow.autoTabGrouping.supportedLocales",
          "fr"
        );
        Assert.ok(
          button.hidden,
          "Dropping the app locale from the list hides the button"
        );
      } finally {
        Services.prefs.clearUserPref(
          "browser.smartwindow.autoTabGrouping.supportedLocales"
        );
      }
      Assert.ok(
        !button.hidden,
        "Adding the app locale back shows the button again"
      );
    });

    it("is hidden in a classic window", async () => {
      await SpecialPowers.pushPrefEnv({
        set: [["browser.smartwindow.autoTabGrouping.enabled", true]],
      });

      win = await BrowserTestUtils.openNewBrowserWindow();
      const button = win.document.getElementById(
        "smartwindow-group-tabs-button"
      );
      Assert.ok(button, "Group tabs button exists in the toolbar");
      Assert.ok(
        BrowserTestUtils.isHidden(button),
        "Group tabs button is hidden in a window that is not a Smart Window"
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
      it("stays hidden even with the feature pref on", async () => {
        await SpecialPowers.pushPrefEnv({
          set: [
            ["browser.smartwindow.autoTabGrouping.enabled", true],
            // Tests run in en-US, which this list leaves out.
            ["browser.smartwindow.autoTabGrouping.supportedLocales", "fr"],
          ],
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

  describe("default placement", () => {
    it("renders to the left of the window switcher", async () => {
      await SpecialPowers.pushPrefEnv({
        set: [["browser.smartwindow.autoTabGrouping.enabled", true]],
      });

      win = await openAIWindow();
      const { area } = CustomizableUI.getPlacementOfWidget("ai-window-toggle");
      const ids = CustomizableUI.getWidgetIdsInArea(area);
      Assert.equal(
        ids[ids.indexOf("ai-window-toggle") - 1],
        "smartwindow-group-tabs-button",
        "The button sits immediately left of the window switcher"
      );
    });
  });
});
