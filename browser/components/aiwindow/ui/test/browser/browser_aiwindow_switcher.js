/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { PromiseTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/PromiseTestUtils.sys.mjs"
);
// AI chat content loads Fluent strings asynchronously, which may not complete
// before the test finishes. This is expected and doesn't affect test behavior.
PromiseTestUtils.allowMatchingRejectionsGlobally(
  /Missing message.*smartwindow-messages-document-title/
);

/**
 * Switches the window mode between classic and smart by clicking an
 * option in the window switcher panel
 *
 * @param {Window} win - The window to switch
 * @param {boolean} aiWindow - true to switch to smart mode, false for classic
 */
async function switchWindowFromPanel(win, aiWindow) {
  const view = PanelMultiView.getViewNode(
    win.document,
    "ai-window-toggle-view"
  );
  const viewShown = BrowserTestUtils.waitForEvent(view, "ViewShown");
  win.document.getElementById("ai-window-toggle").click();
  await viewShown;

  // The view is shown in a panel CustomizableUI builds for the toolbar widget,
  // which it tears down once hidden
  const panel = view.closest("panel");

  view
    .querySelector(
      aiWindow ? "#ai-window-switch-ai" : "#ai-window-switch-classic"
    )
    .click();

  await TestUtils.waitForCondition(
    () => AIWindowTestUtils.isAIWindow(win) === aiWindow,
    `Window should be in ${aiWindow ? "Smart" : "Classic"} Window mode`
  );
  await TestUtils.waitForCondition(
    () => panel.state === "closed",
    "Panel should close after switching"
  );
}

describe("When the window switcher button exists", () => {
  let win, restoreSignIn;

  beforeEach(async () => {
    await SpecialPowers.pushPrefEnv({
      set: [
        ["browser.smartwindow.enabled", true],
        ["sidebar.verticalTabs", false],
        ["browser.ai.control.smartWindow", "default"],
        ["browser.ai.control.default", "available"],
        ["ui.prefersReducedMotion", 1],
      ],
      clear: [["browser.startup.homepage"]],
    });
    restoreSignIn = skipSignIn();
  });

  afterEach(async () => {
    if (win) {
      await BrowserTestUtils.closeWindow(win);
    }
    restoreSignIn();
    await SpecialPowers.popPrefEnv();
    win = null;
  });

  describe("and is switching modes", () => {
    it("is visible only when Smart Window is enabled", async () => {
      Services.prefs.setBoolPref("browser.smartwindow.enabled", false);
      win = await BrowserTestUtils.openNewBrowserWindow();
      const button = win.document.getElementById("ai-window-toggle");

      Assert.ok(
        button?.hidden,
        "Window switcher button should be hidden when Smart Window is disabled"
      );

      Services.prefs.setBoolPref("browser.smartwindow.enabled", true);

      await TestUtils.waitForCondition(
        () => !win.document.getElementById("ai-window-toggle")?.hidden,
        "Window switcher button should be visible when Smart Window is enabled"
      );
    });

    it("switches a window from classic to smart mode", async () => {
      win = await BrowserTestUtils.openNewBrowserWindow();

      await switchWindowFromPanel(win, true);

      const button = win.document.getElementById("ai-window-toggle");

      Assert.ok(
        win
          .getComputedStyle(button)
          ["list-style-image"].includes("smart-window-simplified.svg"),
        "Button icon should change to smart icon when switching to smart mode"
      );
    });

    it("switches a window from smart to classic mode", async () => {
      win = await openAIWindow();

      await switchWindowFromPanel(win, false);

      const button = win.document.getElementById("ai-window-toggle");
      Assert.ok(
        win
          .getComputedStyle(button)
          ["list-style-image"].includes("about-logo.svg"),
        "Button icon should change to classic icon when switching to classic mode"
      );
    });

    it("switches to classic mode on account logout", async () => {
      win = await openAIWindow();

      AIWindow._onAccountLogout();

      Assert.ok(
        !AIWindow.isAIWindowActive(win),
        "Window should switch to classic mode after logout"
      );
    });

    it("replaces custom homepage tabs with Smart Window urls when switching to smart mode", async () => {
      Services.prefs.setStringPref(
        "browser.startup.homepage",
        "https://example.com|www.example.com|https://example.org"
      );
      win = await openAIWindow();

      // Toggle to classic first so we can load custom homepage URLs
      AIWindow.toggleAIWindow(win, false);

      const tab1 = await BrowserTestUtils.openNewForegroundTab(
        win.gBrowser,
        "https://example.com/"
      );
      const tab2 = await BrowserTestUtils.openNewForegroundTab(
        win.gBrowser,
        "https://example.org/"
      );

      // Toggle to AI Window - custom homepage tabs should be reconciled.
      // The schemeless "www.example.com" entry is gracefully skipped.
      AIWindow.toggleAIWindow(win, true);

      await TestUtils.waitForCondition(
        () => tab1.linkedBrowser.currentURI.spec === AIWINDOW_URL,
        "Tab with first custom homepage url should be reconciled to Smart Window url"
      );
      await TestUtils.waitForCondition(
        () => tab2.linkedBrowser.currentURI.spec === AIWINDOW_URL,
        "Tab with second custom homepage url should be reconciled to Smart Window url"
      );
    });

    it("hides the sidebar when switching to classic mode", async () => {
      win = await openAIWindow();

      await BrowserTestUtils.loadURIString({
        browser: win.gBrowser.selectedBrowser,
        uriString: "https://example.com/",
      });

      // Show sidebar elements directly to avoid triggering AI network request
      win.document.getElementById("ai-window-box").hidden = false;
      win.document.getElementById("ai-window-splitter").hidden = false;

      Assert.ok(
        AIWindowUI.isSidebarOpen(win),
        "Sidebar should be initially open in smart mode"
      );

      await switchWindowFromPanel(win, false);

      Assert.ok(
        !AIWindowUI.isSidebarOpen(win),
        "Sidebar should be closed after switching to classic mode"
      );
    });
  });

  describe("and changes its toolbar placement", () => {
    it("moves to nav-bar with vertical tabs and back to TabsToolbar without", async () => {
      win = await openAIWindow();
      const button = win.document.getElementById("ai-window-toggle");
      const tabsToolbar = win.document.getElementById("TabsToolbar");
      const navBar = win.document.getElementById("nav-bar");

      Assert.equal(
        button.closest("#TabsToolbar"),
        tabsToolbar,
        "Window switcher should initially be in TabsToolbar for horizontal tabs"
      );

      Services.prefs.setBoolPref("sidebar.verticalTabs", true);

      await BrowserTestUtils.waitForMutationCondition(
        navBar,
        { childList: true, subtree: true },
        () => button.closest("#nav-bar") === navBar
      );

      Assert.equal(
        button.closest("#nav-bar"),
        navBar,
        "Window switcher should be in nav-bar after enabling vertical tabs"
      );

      Services.prefs.setBoolPref("sidebar.verticalTabs", false);

      await BrowserTestUtils.waitForMutationCondition(
        tabsToolbar,
        { childList: true, subtree: true },
        () => button.closest("#TabsToolbar") === tabsToolbar
      );

      Assert.equal(
        button.closest("#TabsToolbar"),
        tabsToolbar,
        "Window switcher should be back in TabsToolbar after going back to horizontal tabs"
      );
    });

    it("moves the hamburger menu based on window mode and tab orientation", async () => {
      win = await openAIWindow();
      const hamburgerMenu = win.document.getElementById("PanelUI-button");
      const navBar = win.document.getElementById("nav-bar");
      const tabsWindowControls = win.document
        .getElementById("TabsToolbar")
        .querySelector(".titlebar-buttonbox-container");
      const navBarWindowControls = navBar.querySelector(
        ".titlebar-buttonbox-container"
      );

      // In classic mode with horizontal tabs, hamburger should stay in nav-bar
      AIWindow.toggleAIWindow(win, false);

      Assert.ok(
        hamburgerMenu.closest("#nav-bar"),
        "Hamburger menu should remain in nav-bar in classic mode with horizontal tabs"
      );

      // Switch to AI mode - hamburger should move beside the window controls
      AIWindow.toggleAIWindow(win, true);

      Assert.equal(
        tabsWindowControls.nextElementSibling,
        hamburgerMenu,
        "Hamburger menu should be positioned after the window controls in smart mode with horizontal tabs"
      );

      // Switch to vertical tabs - hamburger should sit beside the nav-bar window controls
      Services.prefs.setBoolPref("sidebar.verticalTabs", true);

      await BrowserTestUtils.waitForMutationCondition(
        navBar,
        { childList: true, subtree: true },
        () => navBarWindowControls.nextElementSibling === hamburgerMenu
      );

      Assert.equal(
        navBarWindowControls.nextElementSibling,
        hamburgerMenu,
        "Hamburger menu should be positioned after the window controls in smart mode with vertical tabs"
      );

      // Switch back to classic mode with vertical tabs - hamburger should stay beside the window controls
      AIWindow.toggleAIWindow(win, false);

      Assert.equal(
        navBarWindowControls.nextElementSibling,
        hamburgerMenu,
        "Hamburger menu should remain beside the window controls with vertical tabs even in classic mode"
      );

      // Switch back to horizontal tabs in classic mode - hamburger should return to nav-bar
      Services.prefs.setBoolPref("sidebar.verticalTabs", false);

      await BrowserTestUtils.waitForMutationCondition(
        navBar,
        { childList: true, subtree: true },
        () => hamburgerMenu.closest("#nav-bar")
      );

      Assert.ok(
        hamburgerMenu.closest("#nav-bar"),
        "Hamburger menu should return to nav-bar after switching back to horizontal tabs in classic mode"
      );
    });
  });

  describe("and is affected by AI controls", () => {
    it("is removed when Smart Window is blocked", async () => {
      win = await openAIWindow();
      const button = win.document.getElementById("ai-window-toggle");

      Assert.ok(
        button && !button.hidden,
        "Switcher button should exist and be visible when Smart Window is not blocked"
      );

      Services.prefs.setStringPref("browser.ai.control.smartWindow", "blocked");

      await TestUtils.waitForCondition(
        () => !win.document.getElementById("ai-window-toggle"),
        "Switcher button should not exist when Smart Window AI control is blocked"
      );
    });

    it("is removed when the global AI default is blocked", async () => {
      win = await openAIWindow();
      const button = win.document.getElementById("ai-window-toggle");

      Assert.ok(
        button && !button.hidden,
        "Switcher button should exist and be visible"
      );

      Services.prefs.setStringPref("browser.ai.control.default", "blocked");

      await TestUtils.waitForCondition(
        () => !win.document.getElementById("ai-window-toggle"),
        "Switcher button should not exist when global AI control is blocked"
      );
    });

    it("is restored after unblocking the global AI default", async () => {
      Services.prefs.setStringPref("browser.ai.control.default", "blocked");
      win = await BrowserTestUtils.openNewBrowserWindow();

      await TestUtils.waitForCondition(
        () => !win.document.getElementById("ai-window-toggle"),
        "Switcher button should not exist while global AI control is blocked"
      );

      Services.prefs.setStringPref("browser.ai.control.default", "available");

      await TestUtils.waitForCondition(
        () => win.document.getElementById("ai-window-toggle"),
        "Switcher button should be recreated after unblocking the global AI control"
      );
      Assert.ok(
        win.CustomizableUI.getPlacementOfWidget("ai-window-toggle"),
        "Switcher button should be placed back in a toolbar area after unblocking the global AI control"
      );
    });
  });
});
