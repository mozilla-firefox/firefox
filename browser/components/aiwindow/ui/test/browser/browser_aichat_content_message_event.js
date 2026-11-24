/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { MockEngineManager } = ChromeUtils.importESModule(
  "resource://testing-common/AIWindowTestUtils.sys.mjs"
);

/**
 * Test that messageEvent handler is properly set up and receives events
 */
add_task(async function test_messageEvent_user() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.smartwindow.enabled", true]],
  });

  await BrowserTestUtils.withNewTab("about:aichatcontent", async browser => {
    await SpecialPowers.spawn(browser, [], async () => {
      await content.customElements.whenDefined("ai-chat-content");

      const chatContent = content.document.querySelector("ai-chat-content");
      Assert.ok(chatContent, "ai-chat-content element should exist");

      // Test that event listeners are properly set up
      let messageReceived = false;
      let receivedEventDetail = null;

      // Add our own listener to verify the event is dispatched
      chatContent.addEventListener("aiChatContentActor:message", event => {
        messageReceived = true;
        receivedEventDetail = event.detail;
      });

      const userEvent = new content.CustomEvent("aiChatContentActor:message", {
        detail: {
          role: "user",
          convId: "conv123",
          ordinal: 0,
          content: {
            body: "Hello, AI!",
          },
        },
        bubbles: true,
      });

      chatContent.dispatchEvent(userEvent);

      Assert.ok(messageReceived, "Message event should be received");
      Assert.equal(
        receivedEventDetail.role,
        "user",
        "Should receive correct role"
      );
      Assert.equal(
        receivedEventDetail.convId,
        "conv123",
        "Should receive correct convId"
      );
      Assert.equal(
        receivedEventDetail.content.body,
        "Hello, AI!",
        "Should receive correct message body"
      );
    });
  });

  await SpecialPowers.popPrefEnv();
});

/**
 * Test that messageEvent handles assistant messages correctly
 */
add_task(async function test_messageEvent_assistant() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.smartwindow.enabled", true]],
  });

  await BrowserTestUtils.withNewTab("about:aichatcontent", async browser => {
    await SpecialPowers.spawn(browser, [], async () => {
      await content.customElements.whenDefined("ai-chat-content");

      const chatContent = content.document.querySelector("ai-chat-content");
      Assert.ok(chatContent, "ai-chat-content element should exist");

      let messageReceived = false;
      let receivedEventDetail = null;

      chatContent.addEventListener("aiChatContentActor:message", event => {
        messageReceived = true;
        receivedEventDetail = event.detail;
      });

      const assistantEvent = new content.CustomEvent(
        "aiChatContentActor:message",
        {
          detail: {
            role: "assistant",
            convId: "conv123",
            ordinal: 1,
            id: "msg456",
            content: {
              body: "Hello! How can I help you?",
            },
            memoriesApplied: ["memory1", "memory2"],
            tokens: {
              search: ["token1", "token2"],
            },
          },
          bubbles: true,
        }
      );

      chatContent.dispatchEvent(assistantEvent);

      Assert.ok(messageReceived, "Assistant message event should be received");
      Assert.equal(
        receivedEventDetail.role,
        "assistant",
        "Should receive correct role"
      );
      Assert.equal(
        receivedEventDetail.convId,
        "conv123",
        "Should receive correct convId"
      );
      Assert.equal(
        receivedEventDetail.content.body,
        "Hello! How can I help you?",
        "Should receive correct message body"
      );
      Assert.equal(
        receivedEventDetail.id,
        "msg456",
        "Should receive correct message ID"
      );
    });
  });

  await SpecialPowers.popPrefEnv();
});

add_task(async function test_completed_assistant_message_announced() {
  const restoreSignIn = skipSignIn();
  const mockEngineManager = new MockEngineManager();

  let win;
  try {
    win = await openAIWindow();
    const browser = win.gBrowser.selectedBrowser;

    await typeInSmartbar(browser, "Hello");
    await waitForSmartbarAction(browser, "chat");
    await stubLoadURL(browser);
    await stubOpenSERP(browser);
    await submitSmartbar(browser);

    // The action can still resolve to "search" between the check above and
    // Enter, which submits a real SERP navigation instead of a chat message.
    Assert.ok(
      !(await getStubOpenSERPResult(browser)).called,
      "Submitting should ask the assistant, not run a search"
    );
    const aichatBrowser = await getAichatBrowser(browser);
    await SpecialPowers.spawn(aichatBrowser, [], async () => {
      const chatContent = content.document.querySelector("ai-chat-content");
      const announcer = await ContentTaskUtils.waitForCondition(
        () =>
          chatContent.shadowRoot?.querySelector(
            ".assistant-response-announcer"
          ),
        "assistant response announcer should exist"
      );
      Assert.equal(
        announcer.getAttribute("role"),
        "status",
        "announcer should expose status"
      );
      Assert.equal(
        announcer.getAttribute("aria-live"),
        "polite",
        "announcer should use polite live updates"
      );
      Assert.equal(
        announcer.getAttribute("aria-atomic"),
        "true",
        "announcer should announce its full contents"
      );

      // Record each announcement before the reply arrives: a live region is
      // read out when it changes, and a later update can clear it again.
      content.wrappedJSObject.announcements = Cu.cloneInto([], content);
      new content.MutationObserver(() =>
        content.wrappedJSObject.announcements.push(announcer.textContent.trim())
      ).observe(announcer, {
        childList: true,
        characterData: true,
        subtree: true,
      });
    });
    await mockEngineManager.respondTo({
      purpose: "chat",
      response: "**Final** answer\n\nSecond paragraph",
    });

    await SpecialPowers.spawn(aichatBrowser, [], async () => {
      await ContentTaskUtils.waitForCondition(
        () =>
          content.wrappedJSObject.announcements.includes(
            "Final answer Second paragraph"
          ),
        "completed assistant response should be announced"
      );
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

/**
 * Test that messageEvent handles loading messages correctly
 */
add_task(async function test_messageEvent_loading() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.smartwindow.enabled", true]],
  });

  await BrowserTestUtils.withNewTab("about:aichatcontent", async browser => {
    await SpecialPowers.spawn(browser, [], async () => {
      await content.customElements.whenDefined("ai-chat-content");

      const chatContent = content.document.querySelector("ai-chat-content");
      Assert.ok(chatContent, "ai-chat-content element should exist");

      let messageReceived = false;
      let receivedEventDetail = null;

      chatContent.addEventListener("aiChatContentActor:message", event => {
        messageReceived = true;
        receivedEventDetail = event.detail;
      });

      const loadingEvent = new content.CustomEvent(
        "aiChatContentActor:message",
        {
          detail: {
            role: "loading",
            isSearching: true,
            searchQuery: "test query",
          },
          bubbles: true,
        }
      );

      chatContent.dispatchEvent(loadingEvent);

      Assert.ok(messageReceived, "Loading message event should be received");
      Assert.equal(
        receivedEventDetail.role,
        "loading",
        "Should receive correct role"
      );
      Assert.equal(
        receivedEventDetail.isSearching,
        true,
        "Should receive correct isSearching flag"
      );
      Assert.equal(
        receivedEventDetail.searchQuery,
        "test query",
        "Should receive correct search query"
      );
    });
  });

  await SpecialPowers.popPrefEnv();
});

/**
 * Test that messageEvent handles clear-conversation messages correctly
 */
add_task(async function test_messageEvent_clear_conversation() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.smartwindow.enabled", true]],
  });

  await BrowserTestUtils.withNewTab("about:aichatcontent", async browser => {
    await SpecialPowers.spawn(browser, [], async () => {
      await content.customElements.whenDefined("ai-chat-content");

      const chatContent = content.document.querySelector("ai-chat-content");
      Assert.ok(chatContent, "ai-chat-content element should exist");

      let messageReceived = false;
      let receivedEventDetail = null;

      chatContent.addEventListener("aiChatContentActor:message", event => {
        messageReceived = true;
        receivedEventDetail = event.detail;
      });

      const clearEvent = new content.CustomEvent("aiChatContentActor:message", {
        detail: {
          role: "clear-conversation",
          convId: "conv456",
        },
        bubbles: true,
      });

      chatContent.dispatchEvent(clearEvent);

      Assert.ok(messageReceived, "Clear conversation event should be received");
      Assert.equal(
        receivedEventDetail.role,
        "clear-conversation",
        "Should receive correct role"
      );
      Assert.equal(
        receivedEventDetail.convId,
        "conv456",
        "Should receive correct convId"
      );
    });
  });

  await SpecialPowers.popPrefEnv();
});

/**
 * Test that switching conversations clears the loading state.
 */
add_task(async function test_conversation_change_clears_loading_state() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.smartwindow.enabled", true]],
  });

  await BrowserTestUtils.withNewTab("about:aichatcontent", async browser => {
    const actor =
      browser.browsingContext.currentWindowGlobal.getActor("AIChatContent");

    await actor.dispatchMessageToChatContent({
      role: "user",
      convId: "conv-a",
      ordinal: 0,
      content: { body: "Hello" },
    });
    actor.setGeneratingOnChatContent(true);

    await SpecialPowers.spawn(browser, [], async () => {
      const chatContent = content.document.querySelector("ai-chat-content");
      await ContentTaskUtils.waitForMutationCondition(
        chatContent.shadowRoot,
        { childList: true, subtree: true },
        () => chatContent.shadowRoot?.querySelector("chat-assistant-loader")
      );
      Assert.ok(
        chatContent.shadowRoot?.querySelector("chat-assistant-loader"),
        "loading indicator should be visible while waiting for a response"
      );
    });

    await actor.dispatchMessageToChatContent({
      role: "clear-conversation",
      content: { body: "" },
    });
    actor.setGeneratingOnChatContent(false);

    await SpecialPowers.spawn(browser, [], async () => {
      const chatContent = content.document.querySelector("ai-chat-content");
      await ContentTaskUtils.waitForCondition(
        () => !chatContent.shadowRoot?.querySelector("chat-assistant-loader"),
        "loading indicator should be cleared when conversation changes"
      );
    });
  });

  await SpecialPowers.popPrefEnv();
});

/**
 * Test that a user message with contextMentions renders website chips.
 */
add_task(async function test_messageEvent_user_context_mentions() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.smartwindow.enabled", true]],
  });

  await BrowserTestUtils.withNewTab("about:aichatcontent", async browser => {
    const actor =
      browser.browsingContext.currentWindowGlobal.getActor("AIChatContent");

    await actor.dispatchMessageToChatContent({
      role: "user",
      convId: "conv1",
      ordinal: 0,
      content: {
        body: "Summarize this page",
        contextMentions: [
          {
            url: "https://example.com",
            label: "Example",
            iconSrc: "chrome://global/skin/icons/defaultFavicon.svg",
          },
        ],
      },
    });

    await SpecialPowers.spawn(browser, [], async () => {
      const chatContent = content.document.querySelector("ai-chat-content");

      let chipContainer;
      await ContentTaskUtils.waitForMutationCondition(
        chatContent.shadowRoot,
        { childList: true, subtree: true },
        () => {
          chipContainer = chatContent.shadowRoot.querySelector(
            "website-chip-container"
          );
          return chipContainer;
        }
      );
      Assert.ok(chipContainer, "website-chip-container should be rendered");

      let chip;
      await ContentTaskUtils.waitForMutationCondition(
        chipContainer.shadowRoot,
        { childList: true, subtree: true },
        () => {
          chip = chipContainer.shadowRoot?.querySelector("smartwindow-chip");
          return chip;
        }
      );
      Assert.ok(chip, "smartwindow-chip should be rendered");

      const chipJS = chip.wrappedJSObject || chip;
      Assert.equal(chipJS.label, "Example", "Chip should have correct label");
      Assert.equal(
        chipJS.href,
        "https://example.com",
        "Chip should have correct href"
      );
    });
  });

  await SpecialPowers.popPrefEnv();
});

/**
 * Test that a user message without contextMentions renders no website chips.
 */
add_task(async function test_messageEvent_user_no_context_mentions() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.smartwindow.enabled", true]],
  });

  await BrowserTestUtils.withNewTab("about:aichatcontent", async browser => {
    const actor =
      browser.browsingContext.currentWindowGlobal.getActor("AIChatContent");

    await actor.dispatchMessageToChatContent({
      role: "user",
      convId: "conv1",
      ordinal: 0,
      content: { body: "Hello" },
    });

    await SpecialPowers.spawn(browser, [], async () => {
      const chatContent = content.document.querySelector("ai-chat-content");

      await ContentTaskUtils.waitForMutationCondition(
        chatContent.shadowRoot,
        { childList: true, subtree: true },
        () => chatContent.shadowRoot.querySelector("ai-chat-message")
      );

      Assert.ok(
        !chatContent.shadowRoot.querySelector("website-chip-container"),
        "website-chip-container should not be rendered without contextMentions"
      );
    });
  });

  await SpecialPowers.popPrefEnv();
});
