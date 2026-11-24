/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { MockEngineManager } = ChromeUtils.importESModule(
  "resource://testing-common/AIWindowTestUtils.sys.mjs"
);

const LONG_MESSAGE = "Lorem ipsum dolor sit amet ".repeat(15);

function setupChatContent() {
  const restoreSignIn = skipSignIn();
  const mockEngineManager = new MockEngineManager();
  return { restoreSignIn, mockEngineManager };
}

async function getAichatBrowser(sidebarBrowser) {
  const aiWindow = await TestUtils.waitForCondition(
    () => sidebarBrowser.contentDocument?.querySelector("ai-window"),
    "Wait for ai-window element"
  );
  return TestUtils.waitForCondition(
    () => aiWindow.shadowRoot?.querySelector("#aichat-browser"),
    "Wait for aichat-browser"
  );
}

async function buildConversation(
  sidebarBrowser,
  aichatBrowser,
  mockEngineManager,
  prefix,
  turns
) {
  for (let i = 0; i < turns; i++) {
    await typeInSmartbar(sidebarBrowser, `${prefix} ${i}: ${LONG_MESSAGE}`);
    await submitSmartbar(sidebarBrowser);
    await mockEngineManager.respondTo({
      purpose: "chat",
      response: LONG_MESSAGE,
    });
    await SpecialPowers.spawn(aichatBrowser, [i + 1], async expected => {
      const chatContent = content.document.querySelector("ai-chat-content");
      await ContentTaskUtils.waitForMutationCondition(
        chatContent.shadowRoot,
        { childList: true, subtree: true },
        () => {
          const bubbles = chatContent.shadowRoot.querySelectorAll(
            ".chat-bubble-assistant"
          );
          if (bubbles.length < expected) {
            return false;
          }
          return !!bubbles[bubbles.length - 1].querySelector(
            "assistant-message-footer"
          );
        }
      );
      // Drain the rAF chain inside #scrollUserMessageIntoView so any
      // pending scrollIntoView for the last user message fires before the
      // test starts manipulating wrapper.scrollTop.
      await new Promise(r =>
        content.requestAnimationFrame(() => content.requestAnimationFrame(r))
      );
    });
  }
}

/**
 * Test that the scroll position is preserved when switching between tabs in the sidebar.
 * - When the user leaves a conversation scrolled up, returning to it restores the saved scrollTop.
 * - When the user leaves a conversation at the bottom, returning to it goes to the bottom (wasAtBottom override).
 */
add_task(async function test_scrolling_is_restored() {
  const { restoreSignIn, mockEngineManager } = setupChatContent();

  let win;
  try {
    let sidebarBrowser;
    ({ win, sidebarBrowser } = await openAIWindowWithSidebar());
    const aichatBrowser = await getAichatBrowser(sidebarBrowser);

    await buildConversation(
      sidebarBrowser,
      aichatBrowser,
      mockEngineManager,
      "Conv1",
      4
    );
    const conv1Id = await getConversationId(sidebarBrowser);

    // Scroll up so the saved position is not at the bottom. Disable smooth
    // scrolling so wrapper.scrollTop = X is applied synchronously — without
    // this the save would capture an intermediate scrollTop value.
    const targetScrollTop = 100;
    await SpecialPowers.spawn(
      aichatBrowser,
      [targetScrollTop],
      async target => {
        const chatContent = content.document.querySelector("ai-chat-content");
        const wrapper = chatContent.shadowRoot.querySelector(
          ".chat-content-wrapper"
        );
        wrapper.style.scrollBehavior = "auto";
        Assert.greater(
          wrapper.scrollHeight,
          wrapper.clientHeight + 200,
          "Conversation 1 should overflow with room to scroll up"
        );
        wrapper.scrollTop = target;
        Assert.equal(
          wrapper.scrollTop,
          target,
          "scrollTop should be applied immediately"
        );
      }
    );

    // Open a second tab and switch to it: the sidebar drops conv 1 and
    // starts a fresh conversation, which saves conv 1's scroll position.
    const tab1 = win.gBrowser.selectedTab;
    const tab2 = await BrowserTestUtils.openNewForegroundTab(
      win.gBrowser,
      "https://example.com/"
    );
    await TestUtils.waitForCondition(async () => {
      const id = await getConversationId(sidebarBrowser);
      return id !== conv1Id;
    }, "Sidebar should switch away from conv 1");

    // Build conversation 2 and ensure we end scrolled at the bottom.
    await buildConversation(
      sidebarBrowser,
      aichatBrowser,
      mockEngineManager,
      "Conv2",
      4
    );
    const conv2Id = await getConversationId(sidebarBrowser);
    Assert.notEqual(conv1Id, conv2Id, "Conversations should be distinct");

    await SpecialPowers.spawn(aichatBrowser, [], async () => {
      const chatContent = content.document.querySelector("ai-chat-content");
      const wrapper = chatContent.shadowRoot.querySelector(
        ".chat-content-wrapper"
      );
      wrapper.style.scrollBehavior = "auto";
      wrapper.scrollTop = wrapper.scrollHeight;
    });

    // Switch back to tab 1: scroll position should be restored.
    await BrowserTestUtils.switchTab(win.gBrowser, tab1);
    await TestUtils.waitForCondition(async () => {
      const id = await getConversationId(sidebarBrowser);
      return id === conv1Id;
    }, "Sidebar should restore conv 1");

    await SpecialPowers.spawn(
      aichatBrowser,
      [targetScrollTop],
      async expected => {
        const chatContent = content.document.querySelector("ai-chat-content");
        const wrapper = chatContent.shadowRoot.querySelector(
          ".chat-content-wrapper"
        );
        await ContentTaskUtils.waitForCondition(
          () => Math.abs(wrapper.scrollTop - expected) <= 1,
          `Conv 1 scrollTop should be restored to ${expected}`
        );
      }
    );

    // Switch back to tab 2: wasAtBottom should send us to the bottom of the actual content
    await BrowserTestUtils.switchTab(win.gBrowser, tab2);
    await TestUtils.waitForCondition(async () => {
      const id = await getConversationId(sidebarBrowser);
      return id === conv2Id;
    }, "Sidebar should restore conv 2");

    await SpecialPowers.spawn(aichatBrowser, [], async () => {
      const chatContent = content.document.querySelector("ai-chat-content");
      const wrapper = chatContent.shadowRoot.querySelector(
        ".chat-content-wrapper"
      );
      const innerWrapper = chatContent.shadowRoot.querySelector(
        ".chat-inner-wrapper"
      );
      await ContentTaskUtils.waitForCondition(() => {
        const lastChild = innerWrapper.lastElementChild;
        if (!lastChild) {
          return false;
        }
        const lastChildRect = lastChild.getBoundingClientRect();
        const wrapperRect = wrapper.getBoundingClientRect();

        return lastChildRect.bottom <= wrapperRect.bottom + 5;
      }, "Last actual content should align with viewport bottom in conv 2");
    });
  } finally {
    if (win) {
      await BrowserTestUtils.closeWindow(win);
    }
    await mockEngineManager.rejectAllRequests();
    mockEngineManager.cleanupMocks();
    restoreSignIn();
  }
});
