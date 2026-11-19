/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/* import-globals-from ../head.js */
Services.scriptloader.loadSubScript(
  new URL("../head.js", getRootDirectory(gTestPath)).href,
  this
);

const { MockEngineManager } = ChromeUtils.importESModule(
  "resource://testing-common/AIWindowTestUtils.sys.mjs"
);
const { AITabStore } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/ui/modules/AITabStore.sys.mjs"
);
const { AITAB_PAGE_TYPES } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/ui/modules/AITabPageTypes.sys.mjs"
);
const { TabStateFlusher } = ChromeUtils.importESModule(
  "moz-src:///browser/components/sessionstore/TabStateFlusher.sys.mjs"
);

const PAGE_SLUG = "sidebar-test-page";
const PAGE_URL = `about:smartpage?page=${PAGE_SLUG}`;
const MESSAGE_SELECTOR = ".smartpage-chat-message";
const USER_PROMPT = "Add a heading";
const ASSISTANT_RESPONSE = "Added a heading.";

/**
 * Stores a page with a one-heading surface.
 *
 * @param {string} slug
 * @param {number} [pageType]
 */
function storePage(slug, pageType = AITAB_PAGE_TYPES.CHAT) {
  return AITabStore.create({
    convId: slug,
    slug,
    title: "Sidebar test",
    pageType,
    components: {
      surface: {
        components: [
          { id: "root", component: "Page", header: "hdr", children: [] },
          { id: "hdr", component: "Header", title: "Sidebar test" },
        ],
        dataModel: {},
      },
    },
  });
}

async function setupSidebarTest() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.smartwindow.aitab.enabled", true],
      ["browser.smartwindow.sidebar.openByDefault", true],
      ["browser.smartwindow.memories.generateFromConversation", false],
      ["browser.smartwindow.memories.generateFromHistory", false],
    ],
  });

  const context = {
    restoreSignIn: skipSignIn(),
    mockEngineManager: new MockEngineManager(),
    win: null,
    sidebarBrowser: null,
  };

  await storePage(PAGE_SLUG);

  context.win = await openAIWindow();
  await openEmptySmartPage(context);
  return context;
}

async function cleanupSidebarTest(context) {
  context.mockEngineManager.rejectAllRequests();

  try {
    if (context.win && !context.win.closed) {
      await BrowserTestUtils.closeWindow(context.win);
    }
  } finally {
    context.mockEngineManager.cleanupMocks();
    context.restoreSignIn();
    await AITabStore.deleteBySlug(PAGE_SLUG);
    await SpecialPowers.popPrefEnv();
  }
}

/**
 * Opens a Smart Page in a new tab with the sidebar showing an empty chat.
 *
 * @param {object} context
 * @param {string} [url]
 * @param {boolean} [expectMessage] - Whether the page invites changes, so the
 *   message is waited for.
 * @returns {Promise<MozTabbrowserTab>}
 */
async function openEmptySmartPage(
  context,
  url = PAGE_URL,
  expectMessage = true
) {
  const { win } = context;
  const tab = await BrowserTestUtils.openNewForegroundTab(win.gBrowser, url);
  if (!AIWindowUI.isSidebarOpen(win)) {
    await AIWindowUI.openSidebar(win);
  }
  context.sidebarBrowser = await waitForSidebarReady(win);
  await getAichatBrowser(context.sidebarBrowser);
  await waitForMessage(context, expectMessage);
  return tab;
}

async function getSidebarSnapshot(context) {
  return SpecialPowers.spawn(
    context.sidebarBrowser,
    [MESSAGE_SELECTOR],
    async selector => {
      const aiWindow = Cu.waiveXrays(
        content.document.querySelector("ai-window")
      );
      await aiWindow.updateComplete;

      const message = aiWindow.shadowRoot.querySelector(selector);
      return {
        messageShown: !!message,
        icon: message?.querySelector("img").getAttribute("src"),
        l10nId: message?.querySelector("p").getAttribute("data-l10n-id"),
        generating: aiWindow.isGenerating,
        startersShown: aiWindow.showStarters,
      };
    }
  );
}

async function waitForMessage(context, shown) {
  await TestUtils.waitForCondition(
    async () => (await getSidebarSnapshot(context)).messageShown === shown,
    `The Smartpage message should be ${shown ? "shown" : "hidden"}`
  );
}

async function submitPrompt(context) {
  await typeInSmartbar(context.sidebarBrowser, USER_PROMPT);
  await submitSmartbar(context.sidebarBrowser);
}

async function finishResponse(context) {
  await context.mockEngineManager.respondTo({
    purpose: "chat",
    response: ASSISTANT_RESPONSE,
  });
  await context.mockEngineManager.respondTo({
    purpose: "title-generation",
    response: "Updated Smart Page",
  });
  await TestUtils.waitForCondition(
    async () => !(await getSidebarSnapshot(context)).generating,
    "The response finishes"
  );
}

async function waitForChatMessages(context) {
  return TestUtils.waitForCondition(async () => {
    const messages = await getSidebarChatMessages(context.sidebarBrowser);
    return messages.length === 2 ? messages : null;
  }, "The sidebar displays both chat messages");
}

async function reopenChatTab(context) {
  const { win } = context;
  const tab = win.gBrowser.selectedTab;
  const conversationId = await getConversationId(context.sidebarBrowser);

  await TestUtils.waitForCondition(async () => {
    const conversation =
      await AIWindow.chatStore.findConversationById(conversationId);
    return conversation?.messageCount === 2;
  }, "The submitted conversation is persisted");

  await openEmptySmartPage(context);
  await TabStateFlusher.flush(tab.linkedBrowser);

  const sessionUpdate = BrowserTestUtils.waitForSessionStoreUpdate(tab);
  BrowserTestUtils.removeTab(tab);
  await sessionUpdate;

  const restoredTab = SessionStore.undoCloseTab(win, 0, win);
  await BrowserTestUtils.browserLoaded(
    restoredTab.linkedBrowser,
    false,
    PAGE_URL
  );
  context.sidebarBrowser = await waitForSidebarReady(win);

  await waitForChatMessages(context);
}

async function startNewChat(context) {
  await SpecialPowers.spawn(context.sidebarBrowser, [], () => {
    content.document
      .querySelector("ai-window")
      .shadowRoot.querySelector(".new-chat-icon-button")
      .click();
  });
}

describe("AI Tab sidebar message", () => {
  let context;

  beforeEach(async () => {
    context = await setupSidebarTest();
  });

  afterEach(async () => {
    await cleanupSidebarTest(context);
    context = null;
  });

  it("shows the Smart Window icon and localized message in an empty chat", async () => {
    const snapshot = await getSidebarSnapshot(context);

    Assert.equal(
      snapshot.icon,
      "chrome://browser/content/aiwindow/assets/smart-window.svg",
      "The Smart Window icon is displayed"
    );
    Assert.equal(
      snapshot.l10nId,
      "smartwindow-aitab-empty-chat-message",
      "The localized message is displayed"
    );
    Assert.ok(!snapshot.startersShown, "Conversation starters are hidden");
  });

  it("hides the message before the submitted prompt receives a response", async () => {
    await submitPrompt(context);

    Assert.ok(
      !(await getSidebarSnapshot(context)).messageShown,
      "Submitting removes the message"
    );

    await finishResponse(context);

    Assert.ok(
      !(await getSidebarSnapshot(context)).messageShown,
      "The message stays hidden after the response"
    );
  });

  it("hides the message when a closed chat tab is restored", async () => {
    await submitPrompt(context);
    await finishResponse(context);

    await reopenChatTab(context);

    Assert.ok(
      !(await getSidebarSnapshot(context)).messageShown,
      "The empty-state message does not overlap restored messages"
    );
  });

  it("shows the message again after starting a new chat", async () => {
    await submitPrompt(context);
    await finishResponse(context);

    await startNewChat(context);

    Assert.ok(
      (await getSidebarSnapshot(context)).messageShown,
      "The new empty chat displays the message"
    );
  });
});

describe("AI Tab sidebar message for pages that cannot be modified", () => {
  let context;

  beforeEach(async () => {
    context = await setupSidebarTest();
  });

  afterEach(async () => {
    await AITabStore.deleteBySlug("sidebar-test-research");
    await AITabStore.deleteBySlug("sidebar-test-digest");
    await cleanupSidebarTest(context);
    context = null;
  });

  for (const [name, pageType] of [
    ["research", AITAB_PAGE_TYPES.RESEARCH],
    ["digest", AITAB_PAGE_TYPES.DIGEST],
  ]) {
    it(`does not invite changes to a ${name} page`, async () => {
      const slug = `sidebar-test-${name}`;
      await storePage(slug, pageType);

      const lockedTab = await openEmptySmartPage(
        context,
        `about:smartpage?page=${slug}`,
        false
      );
      Assert.ok(
        !(await getSidebarSnapshot(context)).messageShown,
        `The message is hidden on the ${name} page`
      );

      const chatTab = await openEmptySmartPage(context);
      Assert.ok(
        (await getSidebarSnapshot(context)).messageShown,
        "It is shown again on a page that can be modified"
      );

      BrowserTestUtils.removeTab(chatTab);
      BrowserTestUtils.removeTab(lockedTab);
    });
  }
});
