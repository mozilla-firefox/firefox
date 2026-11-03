/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

/**
 * The AI Tab feature callouts: which anchor they use depending on whether the
 * sidebar is open, and how their primary button asks the sidebar chat for a
 * page.
 */

"use strict";

ChromeUtils.defineESModuleGetters(this, {
  FeatureCallout: "resource:///modules/asrouter/FeatureCallout.sys.mjs",
  PanelTestProvider: "resource:///modules/asrouter/PanelTestProvider.sys.mjs",
});

const { MockEngineManager } = ChromeUtils.importESModule(
  "resource://testing-common/AIWindowTestUtils.sys.mjs"
);

const { MESSAGE_ROLE } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/ui/modules/ChatEnums.sys.mjs"
);

const MESSAGE_ID = "SMARTWINDOW_AITAB_SPLITVIEW_TEST";
const CALLOUT_SELECTOR = `#feature-callout:not(.hidden) .${MESSAGE_ID}_SCREEN`;
const PROMPT =
  "Compare the pages in my split view side by side in a table and put it in a page";

async function getTestMessage() {
  const messages = await PanelTestProvider.getMessages();
  return messages.find(m => m.id === MESSAGE_ID);
}

async function showCallout(win) {
  const featureCallout = new FeatureCallout({
    win,
    location: "chrome",
    context: "chrome",
    browser: win.gBrowser.selectedBrowser,
    theme: { preset: "chrome" },
  });
  await featureCallout.showFeatureCallout(await getTestMessage());
  await TestUtils.waitForCondition(
    () => win.document.querySelector(CALLOUT_SELECTOR),
    "The AI Tab callout is showing"
  );
  return featureCallout;
}

async function endCallout(win, featureCallout) {
  featureCallout.endTour(true);
  await TestUtils.waitForCondition(
    () => !win.document.querySelector(CALLOUT_SELECTOR),
    "The AI Tab callout is gone"
  );
}

async function clickPrimaryAndGetUserMessage(win) {
  win.document.querySelector(`${CALLOUT_SELECTOR} .primary`).click();
  await TestUtils.waitForCondition(
    () => !win.document.querySelector(CALLOUT_SELECTOR),
    "The primary button dismisses the callout"
  );
  const sidebarBrowser = await waitForSidebarReady(win);
  let conversation;
  await TestUtils.waitForCondition(() => {
    conversation =
      sidebarBrowser.contentDocument.querySelector("ai-window")?.conversation;
    return conversation?.messages.some(m => m.role === MESSAGE_ROLE.USER);
  }, "The sidebar chat received the request");
  return {
    conversation,
    userMessage: conversation.messages.find(m => m.role === MESSAGE_ROLE.USER),
  };
}

describe("AI Tab feature callout", () => {
  let win;
  let restoreSignIn;
  let mockEngineManager;

  beforeEach(async () => {
    await SpecialPowers.pushPrefEnv({
      set: [
        ["browser.smartwindow.aitab.enabled", true],
        ["browser.smartwindow.sidebar.openByDefault", false],
        ["browser.tabs.splitView.enabled", true],
      ],
    });
    restoreSignIn = skipSignIn();
    mockEngineManager = new MockEngineManager();
    win = await openAIWindow();
    await promiseNavigateAndLoad(
      win.gBrowser.selectedBrowser,
      "https://example.com/"
    );
  });

  afterEach(async () => {
    await mockEngineManager.rejectAllRequests();
    mockEngineManager.cleanupMocks();
    restoreSignIn();
    await BrowserTestUtils.closeWindow(win);
    win = null;
    await SpecialPowers.popPrefEnv();
  });

  describe("anchor", () => {
    it("is the Ask button when the sidebar is closed", async () => {
      Assert.ok(!AIWindowUI.isSidebarOpen(win), "The sidebar is closed");

      const featureCallout = await showCallout(win);

      Assert.equal(
        featureCallout._getAnchor()?.element,
        win.document.getElementById("smartwindow-ask-button"),
        "The callout is anchored to the Ask button"
      );
      await endCallout(win, featureCallout);
    });

    it("is the sidebar Smartbar when the sidebar is open", async () => {
      const { sidebarBrowser } = await openAIWindowSidebar(
        win,
        "https://example.com/"
      );
      await waitForSidebarOpen(win);
      const smartbarWrapper = await TestUtils.waitForCondition(
        () =>
          sidebarBrowser.contentDocument
            .querySelector("ai-window")
            ?.shadowRoot?.querySelector("#smartbar-wrapper"),
        "The sidebar Smartbar is rendered"
      );

      const featureCallout = await showCallout(win);

      Assert.equal(
        featureCallout._getAnchor()?.element,
        smartbarWrapper,
        "The callout is anchored to the sidebar Smartbar"
      );
      await endCallout(win, featureCallout);
    });

    it("is the Ask button again once the sidebar is closed", async () => {
      await openAIWindowSidebar(win, "https://example.com/");
      await waitForSidebarOpen(win);
      AIWindowUI.toggleSidebar(win);
      await waitForSidebarClosed(win);

      const featureCallout = await showCallout(win);

      Assert.equal(
        featureCallout._getAnchor()?.element,
        win.document.getElementById("smartwindow-ask-button"),
        "The hidden sidebar Smartbar is skipped"
      );
      await endCallout(win, featureCallout);
    });
  });

  describe("primary button", () => {
    it("sends the request to the open sidebar chat", async () => {
      const { sidebarBrowser } = await openAIWindowSidebar(
        win,
        "https://example.com/"
      );
      await waitForSidebarOpen(win);
      const sidebarAIWindow = await TestUtils.waitForCondition(
        () => sidebarBrowser.contentDocument.querySelector("ai-window"),
        "The sidebar chat is loaded"
      );
      const conversationBefore = sidebarAIWindow.conversation;
      const tabCount = win.gBrowser.tabs.length;
      await showCallout(win);

      const { conversation, userMessage } =
        await clickPrimaryAndGetUserMessage(win);

      Assert.equal(
        conversation,
        conversationBefore,
        "The request goes to the sidebar's current conversation"
      );
      Assert.equal(
        userMessage.content.body,
        PROMPT,
        "The user turn is the message prompt alone"
      );
      Assert.equal(
        conversation.aitabSource,
        "fxms",
        "The request is attributed to the messaging system"
      );
      Assert.equal(win.gBrowser.tabs.length, tabCount, "No tab is opened");
      const { respond } = await mockEngineManager.captureRequest({
        purpose: "chat",
      });
      respond("Mock AI Tab reply.");
    });

    it("opens the sidebar and sends the request when the sidebar is closed", async () => {
      const tabCount = win.gBrowser.tabs.length;
      await showCallout(win);

      const { userMessage } = await clickPrimaryAndGetUserMessage(win);

      Assert.ok(AIWindowUI.isSidebarOpen(win), "The sidebar is opened");
      Assert.equal(
        userMessage.content.body,
        PROMPT,
        "The user turn is the message prompt alone"
      );
      Assert.equal(win.gBrowser.tabs.length, tabCount, "No tab is opened");
      const { respond } = await mockEngineManager.captureRequest({
        purpose: "chat",
      });
      respond("Mock AI Tab reply.");
    });
  });
});
