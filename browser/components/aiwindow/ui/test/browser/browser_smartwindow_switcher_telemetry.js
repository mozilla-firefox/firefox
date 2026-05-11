/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { SmartWindowTelemetry } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/ui/modules/SmartWindowTelemetry.sys.mjs"
);

describe("SmartWindowSwitcherTelemetry", () => {
  let sb;
  let win;

  beforeEach(async () => {
    sb = sinon.createSandbox();

    Services.fog.testResetFOG();
    sb.stub(AIWindowAccountAuth, "isSignedIn").resolves(true);
  });

  afterEach(async () => {
    if (win) {
      await BrowserTestUtils.closeWindow(win);
      win = null;
    }
    sb.restore();
  });

  describe("open_window event", () => {
    it("records open_window from trigger menu", async () => {
      win = await BrowserTestUtils.openNewBrowserWindow();
      AIWindow.toggleAIWindow(win, true, "menu");

      await TestUtils.waitForCondition(
        () => Glean.smartWindow.openWindow.testGetValue()?.length > 0,
        "Wait for open_window event"
      );

      const events = Glean.smartWindow.openWindow.testGetValue();
      Assert.equal(events?.length, 1, "One open_window event was recorded");
      Assert.equal(events[0].extra.trigger, "menu", "trigger is correct");
      Assert.equal(events[0].extra.fxa, "true", "fxa is correct");
      Assert.equal(
        events[0].extra.opened_tabs,
        String(win.gBrowser.tabs.length),
        "opened_tabs reflects tab count at open"
      );
    });

    it("records opened_tabs reflecting current tab count", async () => {
      win = await BrowserTestUtils.openNewBrowserWindow();
      BrowserTestUtils.addTab(win.gBrowser, "about:blank");
      BrowserTestUtils.addTab(win.gBrowser, "about:blank");

      AIWindow.toggleAIWindow(win, true, "menu");

      await TestUtils.waitForCondition(
        () => Glean.smartWindow.openWindow.testGetValue()?.length > 0,
        "Wait for open_window event"
      );

      const events = Glean.smartWindow.openWindow.testGetValue();
      Assert.equal(
        events[0].extra.opened_tabs,
        String(win.gBrowser.tabs.length),
        "opened_tabs reflects current tab count"
      );
    });

    it("records open_window from trigger new_window", async () => {
      win = await openAIWindow();

      const newWin = OpenBrowserWindow({ openerWindow: win });
      try {
        await TestUtils.topicObserved(
          "browser-delayed-startup-finished",
          subject => subject == newWin
        );

        await TestUtils.waitForCondition(
          () =>
            Glean.smartWindow.openWindow
              .testGetValue()
              ?.find(e => e.extra.trigger === "new_window"),
          "Wait for new_window open_window event"
        );

        const newWindowEvent = Glean.smartWindow.openWindow
          .testGetValue()
          .find(e => e.extra.trigger === "new_window");
        Assert.ok(
          newWindowEvent,
          "open_window event with new_window trigger was recorded"
        );
      } finally {
        await BrowserTestUtils.closeWindow(newWin);
      }
    });

    it("records open_window from trigger undo_close", async () => {
      win = await openAIWindow();

      await BrowserTestUtils.closeWindow(win);
      win = null; // prevent afterEach from double-closing

      await TestUtils.waitForCondition(
        () => SessionStore.getClosedWindowCount() > 0,
        "Wait for closed window to be recorded"
      );

      Services.fog.testResetFOG();

      let restoredWin;
      try {
        restoredWin = SessionStore.undoCloseWindow(0);
        await BrowserTestUtils.waitForEvent(restoredWin, "load");

        await TestUtils.waitForCondition(
          () => Glean.smartWindow.openWindow.testGetValue()?.length > 0,
          "Wait for open_window event from undo close window"
        );

        const events = Glean.smartWindow.openWindow.testGetValue();
        Assert.equal(events?.length, 1, "One open_window event was recorded");
        Assert.equal(
          events[0].extra.trigger,
          "undo_close",
          "trigger is undo_close"
        );
        Assert.equal(events[0].extra.fxa, "true", "fxa is correct");
      } finally {
        await BrowserTestUtils.closeWindow(restoredWin);
      }
    });

    it("records onboarding=true when firstrun has not been completed", async () => {
      try {
        await SpecialPowers.pushPrefEnv({
          set: [["browser.smartwindow.firstrun.hasCompleted", false]],
        });

        win = await BrowserTestUtils.openNewBrowserWindow();
        AIWindow.toggleAIWindow(win, true, "menu");

        await TestUtils.waitForCondition(
          () => Glean.smartWindow.openWindow.testGetValue()?.length > 0,
          "Wait for open_window event"
        );

        const events = Glean.smartWindow.openWindow.testGetValue();
        Assert.equal(
          events[0].extra.onboarding,
          "true",
          "onboarding is true when firstrun has not been completed"
        );
      } finally {
        await SpecialPowers.popPrefEnv();
      }
    });

    it("records onboarding=false when firstrun has been completed", async () => {
      try {
        await SpecialPowers.pushPrefEnv({
          set: [["browser.smartwindow.firstrun.hasCompleted", true]],
        });

        win = await BrowserTestUtils.openNewBrowserWindow();
        AIWindow.toggleAIWindow(win, true, "menu");

        await TestUtils.waitForCondition(
          () => Glean.smartWindow.openWindow.testGetValue()?.length > 0,
          "Wait for open_window event"
        );

        const events = Glean.smartWindow.openWindow.testGetValue();
        Assert.equal(
          events[0].extra.onboarding,
          "false",
          "onboarding is false when firstrun has been completed"
        );
      } finally {
        await SpecialPowers.popPrefEnv();
      }
    });
  });

  describe("classic_switch event", () => {
    it("records classic_switch when switching back to classic window", async () => {
      win = await openAIWindow();

      AIWindow.toggleAIWindow(win, false);

      await TestUtils.waitForCondition(
        () => Glean.smartWindow.classicSwitch.testGetValue()?.length > 0,
        "Wait for classic_switch event"
      );

      const events = Glean.smartWindow.classicSwitch.testGetValue();
      Assert.equal(events?.length, 1, "One classic_switch event was recorded");
      Assert.equal(
        events[0].extra.opened_tabs,
        String(win.gBrowser.tabs.length),
        "opened_tabs is recorded on classic_switch"
      );
      Assert.greaterOrEqual(
        Number(events[0].extra.duration_ms),
        0,
        "duration_ms is recorded on classic_switch"
      );
    });

    it("records non-zero duration_ms after the window is active", async () => {
      win = await openAIWindow();

      // Yield to let some time pass since the window became active.
      await new Promise(resolve => win.setTimeout(resolve, 5));

      AIWindow.toggleAIWindow(win, false);

      await TestUtils.waitForCondition(
        () => Glean.smartWindow.classicSwitch.testGetValue()?.length > 0,
        "Wait for classic_switch event"
      );

      const events = Glean.smartWindow.classicSwitch.testGetValue();
      Assert.greater(
        Number(events[0].extra.duration_ms),
        0,
        "duration_ms is greater than 0"
      );
    });
  });

  describe("close_window event", () => {
    it("records close_window when an active Smart Window is closed", async () => {
      win = await openAIWindow();
      const tabsAtClose = win.gBrowser.tabs.length;

      await BrowserTestUtils.closeWindow(win);
      win = null; // prevent afterEach from double-closing

      await TestUtils.waitForCondition(
        () => Glean.smartWindow.closeWindow.testGetValue()?.length > 0,
        "Wait for close_window event"
      );

      const events = Glean.smartWindow.closeWindow.testGetValue();
      Assert.equal(events?.length, 1, "One close_window event was recorded");
      Assert.equal(
        events[0].extra.opened_tabs,
        String(tabsAtClose),
        "opened_tabs reflects tab count at close"
      );
      Assert.greaterOrEqual(
        Number(events[0].extra.duration_ms),
        0,
        "duration_ms is recorded on close_window"
      );
    });

    it("does not record close_window when a classic window is closed", async () => {
      win = await BrowserTestUtils.openNewBrowserWindow();

      await BrowserTestUtils.closeWindow(win);
      win = null;

      // Yield to give any async telemetry recording a chance to occur.
      await new Promise(resolve => Services.tm.dispatchToMainThread(resolve));

      Assert.equal(
        Glean.smartWindow.closeWindow.testGetValue() ?? null,
        null,
        "No close_window event was recorded for a classic window"
      );
    });

    it("does not record close_window after switching back to classic", async () => {
      win = await openAIWindow();

      AIWindow.toggleAIWindow(win, false);

      await TestUtils.waitForCondition(
        () => Glean.smartWindow.classicSwitch.testGetValue()?.length > 0,
        "Wait for classic_switch event"
      );

      await BrowserTestUtils.closeWindow(win);
      win = null;

      Assert.equal(
        Glean.smartWindow.closeWindow.testGetValue() ?? null,
        null,
        "No close_window event was recorded after switching to classic"
      );
    });
  });

  describe("uri_load event", () => {
    beforeEach(() => {
      SmartWindowTelemetry.lastUriLoadTimestamp = 0;
    });

    afterEach(async () => {
      await SpecialPowers.popPrefEnv();
    });

    it("records uri_load from location change", async () => {
      await SpecialPowers.pushPrefEnv({
        set: [["browser.smartwindow.firstrun.modelChoice", "1"]],
      });

      win = await openAIWindow();
      const browser = win.gBrowser.selectedBrowser;

      win.dispatchEvent(
        new win.CustomEvent("ai-window:opened-conversation", {
          detail: {
            mode: "fullpage",
            conversationId: "test-conv-id",
            tab: win.gBrowser.selectedTab,
          },
        })
      );

      const loadPromise = BrowserTestUtils.browserLoaded(
        browser,
        false,
        "https://example.com/"
      );
      BrowserTestUtils.startLoadingURIString(browser, "https://example.com/");
      await loadPromise;

      await TestUtils.waitForCondition(
        () => Glean.smartWindow.uriLoad.testGetValue()?.length > 0,
        "Wait for uri_load event from location change"
      );

      const events = Glean.smartWindow.uriLoad.testGetValue();
      Assert.equal(events?.length, 1, "One uri_load event was recorded");
      Assert.equal(
        events[0].extra.model,
        "gemini-2.5-flash-lite",
        "model is correct"
      );
    });

    it("records uri_load from open link", async () => {
      await SpecialPowers.pushPrefEnv({
        set: [["browser.smartwindow.firstrun.modelChoice", "1"]],
      });

      win = await openAIWindow();
      const browser = win.gBrowser.selectedBrowser;

      sb.stub(win, "switchToTabHavingURI").returns(true);

      const aichatBrowser = await TestUtils.waitForCondition(
        () =>
          browser.contentDocument
            ?.querySelector("ai-window")
            ?.shadowRoot?.querySelector("#aichat-browser"),
        "Wait for aichat-browser to be created"
      );

      if (aichatBrowser.currentURI?.spec !== "about:aichatcontent") {
        await BrowserTestUtils.browserLoaded(aichatBrowser);
      }

      await SpecialPowers.spawn(aichatBrowser, [], async () => {
        content.windowGlobalChild
          .getActor("AIChatContent")
          .sendAsyncMessage("AIChatContent:OpenLink", {
            url: "https://example.com/",
          });
      });

      await TestUtils.waitForCondition(
        () => Glean.smartWindow.uriLoad.testGetValue()?.length > 0,
        "Wait for uri_load event from open link"
      );

      const events = Glean.smartWindow.uriLoad.testGetValue();
      Assert.equal(events?.length, 1, "One uri_load event was recorded");
      Assert.equal(
        events[0].extra.model,
        "gemini-2.5-flash-lite",
        "model is correct"
      );
    });
  });

  describe("sidebar_open/sidebar_close event", () => {
    it("records sidebar_open and sidebar_close with chat_id and message_seq", async () => {
      win = await openAIWindow();

      const mockConversation = {
        id: "test-conv-id",
        messages: [
          { role: 0 }, // USER
          { role: 1 }, // ASSISTANT
          { role: 2 }, // SYSTEM - should not be counted
        ],
        messageCount: 2,
      };

      AIWindowUI.openSidebar(win, mockConversation);

      const openEvents = Glean.smartWindow.sidebarOpen.testGetValue();
      Assert.equal(
        openEvents?.length,
        1,
        "One sidebar_open event was recorded"
      );
      Assert.equal(
        openEvents[0].extra.chat_id,
        "test-conv-id",
        "sidebar_open chat_id is correct"
      );
      Assert.equal(
        openEvents[0].extra.message_seq,
        "2",
        "sidebar_open message_seq counts only user and assistant messages"
      );

      AIWindowUI.closeSidebar(win);

      const closeEvents = Glean.smartWindow.sidebarClose.testGetValue();
      Assert.equal(
        closeEvents?.length,
        1,
        "One sidebar_close event was recorded"
      );
    });
  });

  describe("tabs_opened event", () => {
    it("increments tabs_opened counter when a tab is opened", async () => {
      win = await openAIWindow();

      BrowserTestUtils.addTab(win.gBrowser, "https://example.com/");

      Assert.equal(
        Glean.smartWindow.tabsOpened.testGetValue(),
        1,
        "tabs_opened counter was incremented"
      );
    });
  });
});
