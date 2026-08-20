/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { ChatConversation } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/ui/modules/ChatConversation.sys.mjs"
);

const { ChatMessage } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/ui/modules/ChatMessage.sys.mjs"
);

const { ChatStore, MESSAGE_ROLE } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/ui/modules/ChatStore.sys.mjs"
);

const { sinon } = ChromeUtils.importESModule(
  "resource://testing-common/Sinon.sys.mjs"
);

const { TabStateFlusher } = ChromeUtils.importESModule(
  "moz-src:///browser/components/sessionstore/TabStateFlusher.sys.mjs"
);

const { MockEngineManager } = ChromeUtils.importESModule(
  "resource://testing-common/AIWindowTestUtils.sys.mjs"
);

function createMockConversation(id = "test-restore-id") {
  const conversation = new ChatConversation({
    id,
    title: "Restored Conversation",
    pageUrl: new URL("https://example.com/"),
  });

  conversation.messages = [
    new ChatMessage({
      ordinal: 0,
      role: MESSAGE_ROLE.USER,
      content: { text: "Hello" },
    }),
    new ChatMessage({
      ordinal: 1,
      role: MESSAGE_ROLE.ASSISTANT,
      content: { text: "Hi there" },
    }),
  ];

  return conversation;
}

describe("AI Window session restore", () => {
  let win, browser, sb, mockConversation, findStub;

  beforeEach(async () => {
    sb = sinon.createSandbox();
    findStub = sb.stub(ChatStore, "findConversationById");
    win = await openAIWindow();
    browser = win.gBrowser.selectedBrowser;
  });

  afterEach(async () => {
    await BrowserTestUtils.closeWindow(win);
    sb.restore();
    win = null;
    browser = null;
    sb = null;
    mockConversation = null;
    findStub = null;
  });

  describe("fullpage AI window", () => {
    beforeEach(() => {
      mockConversation = createMockConversation();
      findStub.resolves(mockConversation);
    });

    it("should restore conversation from window.history.state.conversationId", async () => {
      // Clear the attribute set during initial load so that #getPendingConversationId
      // falls through to history.state, which is what session restore actually provides.
      browser.removeAttribute("data-conversation-id");

      // Simulate what session restore does: restore a history entry with conversationId.
      await SpecialPowers.spawn(
        browser,
        [mockConversation.id],
        async convId => {
          content.history.replaceState({ conversationId: convId }, "");
        }
      );

      // Reload to trigger connectedCallback, which calls #loadPendingConversation and reads history.state.
      browser.reload();
      await BrowserTestUtils.browserLoaded(browser, false, AIWINDOW_URL);

      await TestUtils.waitForCondition(
        () => findStub.calledWith(mockConversation.id),
        "findConversationById should be called with the history.state conversation ID"
      );

      Assert.ok(
        findStub.calledWith(mockConversation.id),
        "Should restore conversation from window.history.state.conversationId"
      );

      const title = await SpecialPowers.spawn(
        browser,
        [],
        () => content.document.title
      );
      Assert.equal(
        title,
        mockConversation.title,
        "Document title should be set to the restored conversation title"
      );
    });

    it("should not clear an existing conversation when OpenConversation fires with no detail", async () => {
      const contentWin = browser.contentWindow;

      // Open a conversation with messages by dispatching OpenConversation with detail.
      contentWin.document.dispatchEvent(
        new contentWin.CustomEvent("OpenConversation", {
          detail: mockConversation,
        })
      );

      await TestUtils.waitForCondition(
        () =>
          contentWin.document.querySelector("ai-window")?.conversationId ===
          mockConversation.id,
        "conversation should be set after OpenConversation with detail"
      );

      // Dispatch OpenConversation with no detail — should not clear the conversation.
      contentWin.document.dispatchEvent(
        new contentWin.CustomEvent("OpenConversation", { detail: null })
      );

      const conversationId =
        contentWin.document.querySelector("ai-window")?.conversationId;
      Assert.equal(
        conversationId,
        mockConversation.id,
        "conversationId should not change after OpenConversation with no detail when messages exist"
      );
    });
  });

  describe("mode switching", () => {
    let sidebarBrowser;

    beforeEach(async () => {
      sb.stub(Chat, "fetchWithHistory").callsFake(async ({ conversation }) => {
        await ChatStore.updateConversation(conversation);
      });
      sb.stub(openAIEngine, "build").resolves({});

      // Replace the outer stub with a spy so findConversationById still calls
      // through to the real implementation, which lets us assert on the actual
      // restored conversation.
      findStub.restore();
      findStub = sb.spy(ChatStore, "findConversationById");

      BrowserTestUtils.startLoadingURIString(browser, "https://example.com/");
      await BrowserTestUtils.browserLoaded(browser);

      sidebarBrowser = await TestUtils.waitForCondition(
        () => win.document.getElementById("ai-window-browser"),
        "Sidebar browser should be in the DOM"
      );
      await TestUtils.waitForCondition(
        () =>
          sidebarBrowser.contentDocument?.querySelector("ai-window:defined"),
        "Sidebar ai-window should be loaded"
      );
    });

    afterEach(() => {
      sb.restore();
      sidebarBrowser = null;
    });

    it("should restore conversation when toggling from Smart to Classic and back to Smart", async () => {
      await typeInSmartbar(sidebarBrowser, "hello");
      await submitSmartbar(sidebarBrowser);

      // Wait for fetchWithHistory to be called and for the conversation to be
      // persisted before toggling, so findConversationById can find it.
      await TestUtils.waitForCondition(
        () => Chat.fetchWithHistory.called,
        "fetchWithHistory should be called after submit"
      );
      await Chat.fetchWithHistory.returnValues[0];

      AIWindow.toggleAIWindow(win, false);

      Assert.ok(
        !AIWindowUI.isSidebarOpen(win),
        "Sidebar should be closed after toggling to Classic"
      );

      findStub.resetHistory();

      AIWindow.toggleAIWindow(win, true);

      await TestUtils.waitForCondition(
        () => findStub.called,
        "findConversationById should be called to restore the conversation"
      );

      await TestUtils.waitForCondition(
        () => AIWindowUI.isSidebarOpen(win),
        "Sidebar should reopen with the restored conversation after toggling back to Smart"
      );

      const restoredConversation = await findStub.returnValues.at(-1);
      Assert.ok(
        restoredConversation?.messages?.some(m => m.content?.body === "hello"),
        "Restored conversation should contain the submitted 'hello' message"
      );
    });
  });

  describe("tab switching", () => {
    let newTab;

    beforeEach(async () => {
      mockConversation = createMockConversation("tab-a-conv");
      findStub.resolves(mockConversation);

      BrowserTestUtils.startLoadingURIString(browser, "https://example.com/");
      await BrowserTestUtils.browserLoaded(browser);
    });

    afterEach(async () => {
      if (newTab) {
        await BrowserTestUtils.removeTab(newTab);
        newTab = null;
      }
    });

    it("should open a fresh chat on a new tab and restore the conversation when switching back", async () => {
      const tabA = win.gBrowser.selectedTab;

      // Simulate a sidebar conversation on tab A.
      win.dispatchEvent(
        new win.CustomEvent("ai-window:opened-conversation", {
          detail: {
            mode: "sidebar",
            conversationId: mockConversation.id,
            conversation: mockConversation,
            tab: tabA,
          },
        })
      );

      // Open a new tab with no conversation.
      newTab = await BrowserTestUtils.openNewForegroundTab(
        win.gBrowser,
        "https://example.org/"
      );

      await TestUtils.waitForCondition(
        () => AIWindowUI.isSidebarOpen(win),
        "Sidebar should remain open when switching to new tab with no state"
      );

      findStub.resetHistory();

      // Simulate clicking the Ask button on the new tab — should open a fresh chat.
      // First close the sidebar if it's open, then open it to simulate user interaction
      if (AIWindowUI.isSidebarOpen(win)) {
        AIWindowUI.closeSidebar(win);
        await TestUtils.waitForCondition(
          () => !AIWindowUI.isSidebarOpen(win),
          "Sidebar should close first"
        );
      }
      AIWindowUI.openSidebar(win);

      await TestUtils.waitForCondition(
        () => AIWindowUI.isSidebarOpen(win),
        "Sidebar should open after Ask button click"
      );

      Assert.ok(
        !findStub.calledWith(mockConversation.id),
        "Should not load the previous tab's conversation in the new tab's sidebar"
      );

      // Switch back to tab A — its sidebar conversation should be restored.
      win.gBrowser.selectedTab = tabA;

      await TestUtils.waitForCondition(
        () => AIWindowUI.isSidebarOpen(win),
        "Sidebar should reopen when switching back to tab A"
      );

      Assert.ok(
        AIWindowUI.isSidebarOpen(win),
        "Sidebar should be open with tab A's conversation after switching back"
      );
    });
  });

  describe("reopen closed tab", () => {
    let convId, mockEngineManager, reopenedTab;

    beforeEach(async () => {
      findStub.restore();
      mockEngineManager = new MockEngineManager();
      await SpecialPowers.pushPrefEnv({
        set: [["browser.smartwindow.sidebar.openByDefault", true]],
      });

      await closeRecordedTab();

      Assert.equal(
        SessionStore.getClosedTabCountForWindow(win),
        1,
        "Closed tab should be recorded by SessionStore"
      );

      reopenedTab = SessionStore.undoCloseTab(win, 0, win);
    });

    afterEach(async () => {
      if (reopenedTab) {
        await BrowserTestUtils.removeTab(reopenedTab);
        reopenedTab = null;
      }
      mockEngineManager.cleanupMocks();
    });

    async function getSidebarBrowser() {
      const sidebarBrowser = await TestUtils.waitForCondition(
        () => win.document.getElementById("ai-window-browser"),
        "Sidebar browser should be in the DOM"
      );
      await TestUtils.waitForCondition(
        () =>
          sidebarBrowser.contentDocument?.querySelector("ai-window:defined"),
        "Sidebar ai-window should be loaded"
      );
      return sidebarBrowser;
    }

    // Drives a real conversation through the sidebar smartbar, then closes the
    // tab so SessionStore records a closed tab "Reopen Closed Tab" can restore.
    async function closeRecordedTab() {
      const tab = await BrowserTestUtils.openNewForegroundTab(
        win.gBrowser,
        "https://example.com/"
      );
      const sidebarBrowser = await getSidebarBrowser();

      await typeInSmartbar(sidebarBrowser, "hello");
      await submitSmartbar(sidebarBrowser);
      await mockEngineManager.respondTo({
        purpose: "chat",
        response: "Response from assistant",
      });

      convId = await getConversationId(sidebarBrowser);

      await TabStateFlusher.flush(tab.linkedBrowser);

      const sessionUpdate = BrowserTestUtils.waitForSessionStoreUpdate(tab);
      BrowserTestUtils.removeTab(tab);
      await sessionUpdate;
    }

    it("restores the tab's conversation after reopening a closed tab", async () => {
      // Wait for the restored tab to finish loading example.com. By then the
      // restore and read path has had its chance to reopen the conversation
      await BrowserTestUtils.browserLoaded(
        win.gBrowser.selectedBrowser,
        false,
        "https://example.com/"
      );

      const sidebarBrowser = await getSidebarBrowser();

      // the restored conversation should reload its messages into the sidebar
      const messages = await TestUtils.waitForCondition(async () => {
        const msgs = await getSidebarChatMessages(sidebarBrowser);
        return msgs.length === 2 ? msgs : null;
      }, "Sidebar should restore both messages");

      const reloadedConvId = await getConversationId(sidebarBrowser);
      Assert.equal(
        reloadedConvId,
        convId,
        "Reopened tab should restore its conversation id"
      );

      Assert.equal(messages[0].role, "user", "First message is the user turn");
      Assert.equal(messages[0].message, "hello", "user turn restored");
      Assert.equal(
        messages[1].role,
        "assistant",
        "Second message is the assistant turn"
      );
      Assert.equal(
        messages[1].message,
        "Response from assistant",
        "assistant turn restored"
      );
    });
  });
});
