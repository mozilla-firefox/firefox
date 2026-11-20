/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const SUPPORTED_REGIONS_PREF = "browser.smartwindow.agent.supportedRegions";
const UNSUPPORTED_MARKETS = JSON.stringify([["DE", ["de"]]]);

const { MonitorPanel } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/ui/modules/MonitorPanel.sys.mjs"
);

let win;

add_setup(async function () {
  await setupMonitorButton();
  win = await openSharedAIWindow();
});

describe("Monitor button", () => {
  describe("in a Smart Window", () => {
    let sandbox;
    let toggleMonitorPanel;

    beforeEach(() => {
      sandbox = sinon.createSandbox();
      toggleMonitorPanel = sandbox.stub(MonitorPanel, "toggleMonitorPanel");
    });

    afterEach(() => {
      sandbox.restore();
      getMonitorButton(win)?.removeAttribute("tabindex");
    });

    it("toggles the monitor panel of its window when clicked", async () => {
      await promiseNavigateAndLoad(
        win.gBrowser.selectedBrowser,
        "https://example.com/"
      );
      const monitorButton = getMonitorButton(win);
      Assert.ok(
        BrowserTestUtils.isVisible(monitorButton),
        "Monitor button is visible for AI Window"
      );

      EventUtils.synthesizeMouseAtCenter(monitorButton, {}, win);

      Assert.ok(
        toggleMonitorPanel.calledOnceWith(win),
        "Clicking the button toggles the panel for the window it belongs to"
      );
    });

    it("toggles the monitor panel when Space is pressed on it", async () => {
      await promiseNavigateAndLoad(
        win.gBrowser.selectedBrowser,
        "https://example.com/"
      );
      // Toolbar buttons are only focusable while ToolbarKeyboardNavigator has
      // given them a tabindex, so take the same step it does before typing.
      const monitorButton = getMonitorButton(win);
      monitorButton.setAttribute("tabindex", "-1");
      monitorButton.focus();
      Assert.equal(
        win.document.activeElement,
        monitorButton,
        "Monitor button can take keyboard focus"
      );

      // Space rather than Enter: on macOS, Return activates the default button
      // instead of the focused one (see XULButtonElement::AfterHandleEvent).
      EventUtils.synthesizeKey(" ", {}, win);

      Assert.ok(
        toggleMonitorPanel.calledOnceWith(win),
        "Pressing Space on the button toggles the panel"
      );
    });

    // Unlike the ask button, the monitor button stays available on the Smart
    // Window full page.
    it("stays visible in immersive view", async () => {
      await promiseNavigateAndLoad(win.gBrowser.selectedBrowser, AIWINDOW_URL);

      Assert.ok(
        win.document.documentElement.hasAttribute("aiwindow-immersive-view"),
        "Chrome window has the aiwindow-immersive-view attribute"
      );
      Assert.ok(
        !getMonitorButton(win).hidden,
        "Monitor button is not hidden in immersive view"
      );
    });

    // Every other path that sets `hidden` runs from AIWindow.init, so a node
    // built after the window is initialised depends on onCreated resolving the
    // window it was built for.
    it("stays visible when it is rebuilt in an open window", async () => {
      await SpecialPowers.pushPrefEnv({
        set: [["browser.smartwindow.agent.toolbar.enabled", false]],
      });
      Assert.ok(
        !getMonitorButton(win),
        "Turning the gate off removes the button"
      );

      await SpecialPowers.popPrefEnv();

      Assert.ok(
        !getMonitorButton(win).hidden,
        "A node built after the window was initialised is still visible"
      );
    });
  });

  describe("in a Classic Window", () => {
    let classicWin;

    afterEach(async () => {
      if (classicWin) {
        await BrowserTestUtils.closeWindow(classicWin);
        classicWin = null;
      }
    });

    it("is hidden", async () => {
      classicWin = await BrowserTestUtils.openNewBrowserWindow({
        openerWindow: null,
      });

      Assert.ok(
        BrowserTestUtils.isHidden(getMonitorButton(classicWin)),
        "Monitor button is not visible in the toolbar for classic window"
      );
    });
  });

  describe("when the user removes it from the toolbar", () => {
    let newWin;

    afterEach(async () => {
      if (newWin) {
        await BrowserTestUtils.closeWindow(newWin);
        newWin = null;
      }
      // Not CustomizableUI.reset(): it rebuilds every area from the areas'
      // defaultPlacements, which never contain widgets whose defaultArea is a
      // builtin toolbar (see CustomizableUIInternal.createWidget). That would
      // unplace ai-window-toggle for the rest of the browser session.
      if (!CustomizableUI.getPlacementOfWidget(WIDGET_ID)) {
        CustomizableUI.addWidgetToArea(WIDGET_ID, CustomizableUI.AREA_NAVBAR);
      }
    });

    it("starts in the navbar and can be removed", () => {
      Assert.equal(
        CustomizableUI.getPlacementOfWidget(WIDGET_ID)?.area,
        CustomizableUI.AREA_NAVBAR,
        "Monitor button is placed in the navbar by default"
      );
      Assert.ok(
        CustomizableUI.isWidgetRemovable(WIDGET_ID),
        "Monitor button is removable"
      );
    });

    it("stays in the customize palette of new windows", async () => {
      CustomizableUI.removeWidgetFromArea(WIDGET_ID);

      newWin = await openAIWindow();

      Assert.ok(
        !getMonitorButton(newWin)?.closest("toolbar"),
        "A new window does not put the removed button back on a toolbar"
      );
      Assert.ok(
        isInCustomizePalette(newWin),
        "Removed monitor button is available in the customize palette"
      );
    });
  });

  // The widget is not registered at all, and so stays out of the customize
  // palette, unless both the agent pref and its own toolbar pref are on.
  describe("while browser.smartwindow.agent.enabled is off", () => {
    beforeEach(async () => {
      await SpecialPowers.pushPrefEnv({
        set: [["browser.smartwindow.agent.enabled", false]],
      });
    });

    afterEach(async () => {
      await SpecialPowers.popPrefEnv();
    });

    it("is not registered", () => {
      assertNotRegistered("agent.enabled is off");
    });
  });

  describe("while browser.smartwindow.agent.toolbar.enabled is off", () => {
    beforeEach(async () => {
      await SpecialPowers.pushPrefEnv({
        set: [["browser.smartwindow.agent.toolbar.enabled", false]],
      });
    });

    afterEach(async () => {
      await SpecialPowers.popPrefEnv();
    });

    it("is not registered", () => {
      assertNotRegistered("agent.toolbar.enabled is off");
    });
  });

  describe("while both agent prefs are off", () => {
    beforeEach(async () => {
      await SpecialPowers.pushPrefEnv({
        set: [
          ["browser.smartwindow.agent.enabled", false],
          ["browser.smartwindow.agent.toolbar.enabled", false],
        ],
      });
    });

    afterEach(async () => {
      await SpecialPowers.popPrefEnv();
    });

    it("is not registered", () => {
      assertNotRegistered("both agent prefs are off");
    });
  });

  // The button is the toolbar surface of a feature that cannot create a
  // monitor outside its supported regions, so it follows the same region gate
  // as the /watch command and the tasks page rather than offering a dead
  // button.
  describe("when no market covers the home region", () => {
    beforeEach(async () => {
      await SpecialPowers.pushPrefEnv({
        set: [[SUPPORTED_REGIONS_PREF, UNSUPPORTED_MARKETS]],
      });
    });

    afterEach(async () => {
      await SpecialPowers.popPrefEnv();
    });

    it("is not registered", () => {
      assertNotRegistered("no market covers the home region");
    });
  });

  // The gate pairs the home region with the app locale, so switching Firefox
  // to a language the market does not cover has to take the button away again
  // rather than leaving a button that cannot create a monitor.
  describe("when the app locale changes", () => {
    let originalAvailable;

    // Setting requestedLocales writes these prefs, so the pref env is what
    // puts them back, user value or not.
    beforeEach(async () => {
      await SpecialPowers.pushPrefEnv({
        clear: [
          ["intl.locale.requested"],
          ["extensions.getAddons.cache.lastLocale"],
        ],
      });
      originalAvailable = Services.locale.availableLocales;
      Services.locale.availableLocales = ["en-US", "fr"];
    });

    afterEach(async () => {
      await SpecialPowers.popPrefEnv();
      Services.locale.availableLocales = originalAvailable;
      await TestUtils.waitForCondition(
        () => getMonitorButton(win),
        "Waiting for the monitor button to be restored"
      );
    });

    it("goes away once the locale leaves the market", async () => {
      Services.locale.requestedLocales = ["fr"];

      await TestUtils.waitForCondition(
        () => !getMonitorButton(win),
        "Monitor button goes away once the app locale leaves the market"
      );
    });

    it("comes back when the locale returns to the market", async () => {
      Services.locale.requestedLocales = ["fr"];
      await TestUtils.waitForCondition(
        () => !getMonitorButton(win),
        "Waiting for the monitor button to go away"
      );

      Services.locale.requestedLocales = ["en-US"];

      await TestUtils.waitForCondition(
        () => getMonitorButton(win),
        "Monitor button comes back when the app locale returns to the market"
      );
    });
  });

  // A fresh profile has no home region until detection finishes, so the
  // button has to appear when that lands rather than waiting for a restart.
  describe("while the home region is unknown", () => {
    beforeEach(() => {
      Region._setHomeRegion("", false);
      AIWindowToolbarButtons.updateMonitorRegistration();
    });

    afterEach(() => {
      Region._setHomeRegion(TEST_REGION, false);
      AIWindowToolbarButtons.updateMonitorRegistration();
    });

    it("appears once a supported region is detected", () => {
      Assert.equal(
        getMonitorButton(win),
        null,
        "No button while the home region is still unknown"
      );

      // _setHomeRegion notifies browser-region-updated, which
      // AIWindowToolbarButtons observes.
      Region._setHomeRegion(TEST_REGION);

      Assert.ok(
        getMonitorButton(win),
        "Detecting a supported region registers the button without a restart"
      );
    });
  });
});

/**
 * @param {ChromeWindow} window
 * @returns {boolean} Whether the monitor button is offered in the window's
 *   customize palette.
 */
function isInCustomizePalette(window) {
  return CustomizableUI.getUnusedWidgets(window.gNavToolbox.palette).some(
    widget => widget.id == WIDGET_ID
  );
}

/**
 * Asserts the monitor button is neither in the shared window nor offered in
 * its customize palette.
 *
 * @param {string} scenario - Why the button should be unregistered.
 */
function assertNotRegistered(scenario) {
  Assert.equal(
    getMonitorButton(win),
    null,
    `Monitor button is absent when ${scenario}`
  );
  Assert.ok(
    !isInCustomizePalette(win),
    `Monitor button is not in the customize palette when ${scenario}`
  );
}
