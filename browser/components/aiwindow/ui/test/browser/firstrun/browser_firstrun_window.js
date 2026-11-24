/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

ChromeUtils.defineLazyGetter(this, "SidebarTestUtils", () => {
  const { SidebarTestUtils: utils } = ChromeUtils.importESModule(
    "resource://testing-common/SidebarTestUtils.sys.mjs"
  );
  utils.init(this);
  return utils;
});

describe("Before first run completes", () => {
  beforeEach(async () => {
    await SpecialPowers.pushPrefEnv({
      set: [
        ["browser.smartwindow.enabled", true],
        ["browser.smartwindow.firstrun.hasCompleted", false],
      ],
    });
  });

  afterEach(async () => {
    await SpecialPowers.flushPrefEnv();
  });

  describe("launching from a Classic Window", () => {
    let restoreSignIn;
    let tab;

    beforeEach(async () => {
      restoreSignIn = skipSignIn();
      // Ensure we start in classic mode
      document.documentElement.removeAttribute("ai-window");
      tab = await BrowserTestUtils.openNewForegroundTab(
        gBrowser,
        "about:blank"
      );
    });

    afterEach(() => {
      AIWindow.toggleAIWindow(window, false);
      restoreSignIn();
      BrowserTestUtils.removeTab(tab);
    });

    it("shows first run in the same window", async () => {
      await AIWindow.launchWindow(gBrowser.selectedBrowser);

      await TestUtils.waitForCondition(
        () => gBrowser.selectedBrowser.currentURI.spec === FIRSTRUN_URL,
        "Should navigate to firstrun.html"
      );
      Assert.equal(
        gBrowser.selectedBrowser.currentURI.spec,
        FIRSTRUN_URL,
        "launchWindow should load firstrun.html when firstrun not completed"
      );
    });

    it("shows first run from the switcher", async () => {
      const button = document.getElementById("ai-window-toggle");
      const view = PanelMultiView.getViewNode(
        document,
        "ai-window-toggle-view"
      );
      const viewShownPromise = BrowserTestUtils.waitForEvent(view, "ViewShown");
      button.click();
      await viewShownPromise;

      view.querySelector("#ai-window-switch-ai").click();

      await TestUtils.waitForCondition(
        () => document.documentElement.hasAttribute("ai-window"),
        "Window should have ai-window attribute after switching"
      );
      await TestUtils.waitForCondition(
        () => gBrowser.selectedBrowser.currentURI.spec === FIRSTRUN_URL,
        "Should navigate to firstrun.html"
      );
      Assert.equal(
        gBrowser.selectedBrowser.currentURI.spec,
        FIRSTRUN_URL,
        "Switcher should load firstrun.html when firstrun not completed"
      );
      await TestUtils.waitForCondition(
        () => PanelUI.panel.state === "closed",
        "Panel should close after switching"
      );
    });

    it("shows first run in a new Smart Window", async () => {
      const newWindowPromise = BrowserTestUtils.waitForNewWindow({
        url: FIRSTRUN_URL,
      });

      await AIWindow.launchWindow(gBrowser.selectedBrowser, true);
      const newWindow = await newWindowPromise;

      Assert.equal(
        newWindow.gBrowser.selectedBrowser.currentURI.spec,
        FIRSTRUN_URL,
        "launchWindow with openNewWindow=true should load firstrun.html when firstrun not completed"
      );
      await TestUtils.waitForCondition(
        () => newWindow.document.documentElement.hasAttribute("ai-window"),
        "New window should have ai-window attribute after authorization"
      );
      Assert.ok(
        newWindow.document.documentElement.hasAttribute("ai-window"),
        "New window should be in AI Window mode"
      );

      await BrowserTestUtils.closeWindow(newWindow);
    });
  });

  describe("in a Smart Window showing first run", () => {
    let win;

    beforeEach(async () => {
      await SpecialPowers.pushPrefEnv({
        set: [["sidebar.verticalTabs", false]],
      });
      win = await openAIWindow({ waitForTabURL: FIRSTRUN_URL });
      await BrowserTestUtils.waitForMutationCondition(
        win.document.documentElement,
        { attributes: true },
        () => win.document.documentElement.hasAttribute("aiwindow-first-run")
      );
    });

    afterEach(async () => {
      if (win) {
        await BrowserTestUtils.closeWindow(win);
        win = null;
      }
    });

    it("keeps the toggle interactive", async () => {
      const toggle = win.document.getElementById("ai-window-toggle");
      Assert.ok(
        toggle.closest("#TabsToolbar-customization-target"),
        "Toggle should live in the tabstrip in horizontal tabs mode"
      );

      const toggleStyle = win.getComputedStyle(toggle);
      Assert.notEqual(
        toggleStyle.pointerEvents,
        "none",
        "Toggle must stay clickable during first run (regression guard)"
      );
      Assert.equal(
        toggleStyle.opacity,
        "1",
        "Toggle must not be dimmed during first run"
      );

      const tabsStyle = win.getComputedStyle(
        win.document.getElementById("tabbrowser-tabs")
      );
      Assert.equal(
        tabsStyle.pointerEvents,
        "none",
        "Other tabstrip items remain dimmed during first run"
      );
    });

    describe("with a core sidebar panel open", () => {
      let root;
      let els;

      beforeEach(async () => {
        root = win.document.documentElement;
        els = getCoreSidebarEls(win);
        const browser = win.gBrowser.selectedBrowser;

        // A sidebar panel may already be open when the user is in the smart
        // window. Open one outside of first run so it loads normally, then
        // re-enter first run.
        await promiseNavigateAndLoad(browser, "https://example.com/");
        await BrowserTestUtils.waitForMutationCondition(
          root,
          { attributes: true },
          () => !root.hasAttribute("aiwindow-first-run")
        );
        await SidebarTestUtils.showPanel(win, "viewBookmarksSidebar");
        Assert.ok(
          BrowserTestUtils.isVisible(els.box),
          "Sidebar panel is visible before first run"
        );

        await promiseNavigateAndLoad(browser, FIRSTRUN_URL);
        await BrowserTestUtils.waitForMutationCondition(
          root,
          { attributes: true },
          () => root.hasAttribute("aiwindow-first-run")
        );
      });

      afterEach(() => {
        SidebarTestUtils.closePanel(win);
      });

      // In horizontal tabs mode the whole core sidebar - launcher, an open
      // panel, and both splitters - is hidden during smart window first run,
      // and comes back once first run finishes.
      it("hides it until first run finishes", async () => {
        assertCoreSidebarHidden(els, "during first run");

        // Completing first run navigates to the AI Window new tab, which keeps
        // the window in immersive view but clears the first-run state. The
        // core sidebar is gated on first run specifically, so the open panel
        // comes back even though the window is still immersive.
        await promiseNavigateAndLoad(
          win.gBrowser.selectedBrowser,
          AIWINDOW_URL
        );
        await BrowserTestUtils.waitForMutationCondition(
          root,
          { attributes: true },
          () => !root.hasAttribute("aiwindow-first-run")
        );

        Assert.ok(
          root.hasAttribute("aiwindow-immersive-view"),
          "Window is still in immersive view after first run completes"
        );
        Assert.ok(
          BrowserTestUtils.isVisible(els.box),
          "Sidebar panel is visible again after first run completes"
        );
      });

      // Aborting first run by switching back to a classic window also restores
      // the core sidebar.
      it("shows it again when switching to a Classic Window", async () => {
        Assert.ok(
          BrowserTestUtils.isHidden(els.box),
          "Sidebar panel is hidden during first run"
        );

        AIWindow.toggleAIWindow(win, false);

        Assert.ok(
          !root.hasAttribute("ai-window"),
          "Window is back in classic mode after aborting first run"
        );
        Assert.ok(
          BrowserTestUtils.isVisible(els.box),
          "Sidebar panel is visible again after aborting first run"
        );
      });
    });
  });
});

/**
 * The core (non-AI) sidebar's launcher, panel, and their splitters.
 *
 * @param {Window} win
 * @returns {Record<string, Element>}
 */
function getCoreSidebarEls(win) {
  return {
    container: win.document.getElementById("sidebar-container"),
    launcherSplitter: win.document.getElementById("sidebar-launcher-splitter"),
    box: win.document.getElementById("sidebar-box"),
    splitter: win.document.getElementById("sidebar-splitter"),
  };
}

function assertCoreSidebarHidden(els, context) {
  for (const [name, el] of Object.entries(els)) {
    Assert.ok(
      BrowserTestUtils.isHidden(el),
      `#${el.id} (${name}) is hidden ${context}`
    );
  }
}
