/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  ASRouter: "resource:///modules/asrouter/ASRouter.sys.mjs",
});

const { MockEngineManager } = ChromeUtils.importESModule(
  "resource://testing-common/AIWindowTestUtils.sys.mjs"
);

const promptShowingSelector =
  "#feature-callout:not(.hidden) .SMARTWINDOW_SIDEBAR_AUTO_OPEN_PROMPT_CALLOUT";

const acceptedResultSelector =
  "#feature-callout:not(.hidden) .SMARTWINDOW_SIDEBAR_AUTO_OPEN_ACCEPTED_CALLOUT";

const rejectedResultSelector =
  "#feature-callout:not(.hidden) .SMARTWINDOW_SIDEBAR_AUTO_OPEN_REJECTED_CALLOUT";

describe("sidebar auto-open pref prompt", () => {
  let win, askButton, mockEngineManager;

  beforeEach(async () => {
    Services.prefs.clearUserPref("browser.smartwindow.sidebar.emptyCloseCount");
    await lazy.ASRouter.resetMessageState();

    mockEngineManager = new MockEngineManager();
    win = await openAIWindow();
    askButton = win.document.getElementById("smartwindow-ask-button-inner");

    await promiseNavigateAndLoad(
      win.gBrowser.selectedBrowser,
      "https://example.com/"
    );
  });

  afterEach(async () => {
    Services.prefs.clearUserPref("browser.smartwindow.sidebar.emptyCloseCount");

    await lazy.ASRouter.resetMessageState();
    await BrowserTestUtils.closeWindow(win);
    await mockEngineManager.rejectAllRequests();
    mockEngineManager.cleanupMocks();

    win = null;
  });

  describe("when a user closes the sidebar enough times to trigger prompt", () => {
    it("triggers opening the auto-open pref prompt", async () => {
      await triggerAutoOpenPrompt(win, askButton);

      const callout = await waitForCallout(win, promptShowingSelector);

      Assert.ok(callout, "The auto open prompt should be showing");
    });

    describe("when the browser.smartwindow.sidebar.openByDefault pref is already disabled", () => {
      beforeEach(async () => {
        await SpecialPowers.pushPrefEnv({
          set: [["browser.smartwindow.sidebar.openByDefault", false]],
        });
      });

      afterEach(async () => {
        await SpecialPowers.popPrefEnv();
      });

      it("does not open the auto-open pref prompt", async () => {
        await triggerAutoOpenPrompt(win, askButton);

        Assert.ok(
          !win.document.querySelector(promptShowingSelector),
          "Prompt should not open if browser.smartwindow.sidebar.openByDefault is set to false"
        );
      });
    });
  });

  describe("when the closed sidebar has a started conversation", () => {
    async function openSidebarAndGetAIWindow() {
      if (!AIWindowUI.isSidebarOpen(win)) {
        EventUtils.synthesizeMouseAtCenter(askButton, {}, win);
      }
      await BrowserTestUtils.waitForMutationCondition(
        win.document.getElementById(AIWindowUI.BOX_ID),
        { attributes: true, attributeFilter: ["collapsed"] },
        () => AIWindowUI.isSidebarOpen(win)
      );

      const sidebarBrowser = win.document.getElementById("ai-window-browser");
      return TestUtils.waitForCondition(() => {
        const el =
          sidebarBrowser.contentDocument?.querySelector("ai-window:defined");
        return el?.conversation ? el : null;
      }, "Sidebar ai-window should be loaded with a conversation");
    }

    it("resets the empty-close count when closing below the trigger", async () => {
      // One prior empty close, a single close away from the trigger count.
      Services.prefs.setIntPref(
        "browser.smartwindow.sidebar.emptyCloseCount",
        1
      );

      const aiWindow = await openSidebarAndGetAIWindow();

      // Simulate a started conversation in this tab's sidebar.
      aiWindow.conversation.addUserMessage("Hello");
      Assert.greater(
        aiWindow.conversation.messageCount,
        0,
        "The sidebar conversation should have a chat message"
      );

      // Close the non-empty sidebar.
      EventUtils.synthesizeMouseAtCenter(askButton, {}, win);
      await BrowserTestUtils.waitForMutationCondition(
        win.document.getElementById(AIWindowUI.BOX_ID),
        { attributes: true, attributeFilter: ["collapsed"] },
        () => !AIWindowUI.isSidebarOpen(win)
      );

      Assert.equal(
        Services.prefs.getIntPref(
          "browser.smartwindow.sidebar.emptyCloseCount"
        ),
        0,
        "Closing a non-empty sidebar should reset the empty-close count"
      );
      Assert.ok(
        !win.document.querySelector(promptShowingSelector),
        "The keep-closed callout should not appear for a non-empty sidebar"
      );
    });

    it("resets the empty-close count even once the trigger count is reached", async () => {
      // Already at the trigger count from prior empty closes. The original bug:
      // engaging with a conversation could no longer pull the count back down,
      // so the prompt kept targeting an active user.
      Services.prefs.setIntPref(
        "browser.smartwindow.sidebar.emptyCloseCount",
        2
      );

      const aiWindow = await openSidebarAndGetAIWindow();

      aiWindow.conversation.addUserMessage("Hello");
      Assert.greater(
        aiWindow.conversation.messageCount,
        0,
        "The sidebar conversation should have a chat message"
      );

      EventUtils.synthesizeMouseAtCenter(askButton, {}, win);
      await BrowserTestUtils.waitForMutationCondition(
        win.document.getElementById(AIWindowUI.BOX_ID),
        { attributes: true, attributeFilter: ["collapsed"] },
        () => !AIWindowUI.isSidebarOpen(win)
      );

      Assert.equal(
        Services.prefs.getIntPref(
          "browser.smartwindow.sidebar.emptyCloseCount"
        ),
        0,
        "A non-empty close at the trigger count should still reset to 0"
      );
    });
  });

  describe("when the user accepts the prompt", () => {
    let callout, newTab;

    beforeEach(async () => {
      await triggerAutoOpenPrompt(win, askButton);

      callout = await waitForCallout(win, promptShowingSelector);
    });

    afterEach(() => {
      Services.prefs.setBoolPref(
        "browser.smartwindow.sidebar.openByDefault",
        true
      );

      if (newTab) {
        BrowserTestUtils.removeTab(newTab);
      }
    });

    it("toggles the auto open pref", async () => {
      let currentAutoOpen = Services.prefs.getBoolPref(
        "browser.smartwindow.sidebar.openByDefault",
        true
      );
      Assert.ok(currentAutoOpen, "openByDefault should start as true");

      Services.fog.testResetFOG();
      const primaryButton = callout.querySelector("button.primary");
      primaryButton.click();

      currentAutoOpen = Services.prefs.getBoolPref(
        "browser.smartwindow.sidebar.openByDefault",
        true
      );
      Assert.ok(
        !currentAutoOpen,
        "openByDefault should switch to false if user accepts prompt"
      );

      const changeEvents =
        Glean.smartWindow.sidebarOpenByDefaultChange.testGetValue();
      Assert.equal(
        changeEvents.length,
        1,
        "One sidebarOpenByDefaultChange event was recorded"
      );
      Assert.equal(
        changeEvents[0].extra.enabled,
        "false",
        "The event records the sidebar no longer opening by default"
      );
      Assert.equal(
        Glean.smartWindow.sidebarOpenByDefault.testGetValue(),
        false,
        "sidebarOpenByDefault metric is false after accepting the prompt"
      );
    });

    describe("the accepted result prompt", () => {
      let resultCallout;

      beforeEach(async () => {
        const primaryButton = callout.querySelector("button.primary");
        primaryButton.click();
        resultCallout = await waitForCallout(win, acceptedResultSelector);
      });

      afterEach(async () => {
        if (newTab) {
          BrowserTestUtils.removeTab(newTab);
        }
      });

      it("has a link to the Settings page", async () => {
        const anchorTag = resultCallout.querySelector(
          "a.text-link[data-l10n-name='settings']"
        );

        Assert.ok(
          anchorTag,
          "Settings link should be present in the result callback"
        );

        const newTabPromise = BrowserTestUtils.waitForNewTab(
          win.gBrowser,
          url => url.startsWith("about:preferences"),
          true
        );

        anchorTag.click();

        newTab = await newTabPromise;

        Assert.equal(
          newTab.linkedBrowser.currentURI.spec,
          "about:preferences#personalizeSmartWindow",
          "One of the tabs should be the smart window prefs"
        );
      });
    });
  });

  describe("when a user dismisses the prompt", () => {
    let callout, newTab;

    beforeEach(async () => {
      await triggerAutoOpenPrompt(win, askButton);

      callout = await waitForCallout(win, promptShowingSelector);
    });

    it("doesn't reopen again", async () => {
      // click no thanks
      const secondaryButton = callout.querySelector("button.secondary");
      secondaryButton.click();

      await dismissResultCallout(win);

      await TestUtils.waitForCondition(
        () =>
          lazy.ASRouter.state.messageImpressions
            .SMARTWINDOW_SIDEBAR_AUTO_OPEN_PROMPT.length,
        "First impression should be recorded"
      );

      EventUtils.synthesizeMouseAtCenter(askButton, {}, win); // open
      await TestUtils.waitForCondition(() => AIWindowUI.isSidebarOpen(win));
      EventUtils.synthesizeMouseAtCenter(askButton, {}, win); // close
      await TestUtils.waitForCondition(() => !AIWindowUI.isSidebarOpen(win));

      Assert.equal(
        lazy.ASRouter.state.messageImpressions
          .SMARTWINDOW_SIDEBAR_AUTO_OPEN_PROMPT.length,
        1,
        "Impression count should not increase after dismissal"
      );
      Assert.ok(
        !win.document.querySelector(promptShowingSelector),
        "Prompt should not reopen after being dismissed"
      );
    });

    describe("the accepted result prompt", () => {
      let resultCallout;

      beforeEach(async () => {
        const primaryButton = callout.querySelector("button.primary");
        primaryButton.click();
        resultCallout = await waitForCallout(win, acceptedResultSelector);
      });

      afterEach(async () => {
        if (newTab) {
          BrowserTestUtils.removeTab(newTab);
        }
      });

      it("has a link to the Settings page", async () => {
        const anchorTag = resultCallout.querySelector(
          "a.text-link[data-l10n-name='settings']"
        );

        Assert.ok(
          anchorTag,
          "Settings link should be present in the result callback"
        );

        const newTabPromise = BrowserTestUtils.waitForNewTab(
          win.gBrowser,
          url => url.startsWith("about:preferences"),
          true
        );

        anchorTag.click();

        newTab = await newTabPromise;

        Assert.equal(
          newTab.linkedBrowser.currentURI.spec,
          "about:preferences#personalizeSmartWindow",
          "One of the tabs should be the smart window prefs"
        );
      });
    });
  });
});

async function dismissResultCallout(win) {
  const resultCallout = await waitForCallout(win, rejectedResultSelector);
  const dismissButton = resultCallout.querySelector(".dismiss-button");
  EventUtils.synthesizeMouseAtCenter(dismissButton, {}, win);
}

async function waitForCallout(win, selector) {
  let callout = null;
  await BrowserTestUtils.waitForMutationCondition(
    win.document,
    { childList: true, subtree: true, attributeFilter: ["class"] },
    () => (callout = win.document.querySelector(selector))
  );

  return callout;
}

async function triggerAutoOpenPrompt(win, askButton) {
  const clicks = [
    { label: "close 1", expectedOpen: false },
    { label: "open", expectedOpen: true },
    { label: "close 2", expectedOpen: false },
  ];

  for (const { expectedOpen } of clicks) {
    EventUtils.synthesizeMouseAtCenter(askButton, {}, win);
    await TestUtils.waitForCondition(
      () => AIWindowUI.isSidebarOpen(win) === expectedOpen
    );
  }
}
