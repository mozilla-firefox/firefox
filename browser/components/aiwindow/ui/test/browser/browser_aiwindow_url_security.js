/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Browser tests for AI Window URL security validation.
 *
 * Tests the seen-URL-based link unfurling flow where:
 * 1. seenUrls is pushed to ai-chat-message via the seenUrls property
 * 2. ai-chat-message renders markdown and unfurls unseen links
 * 3. Links present in seenUrls render normally; others are disclosed
 *
 * These tests verify the security invariants at the component boundary:
 * - Fail-closed: unseen links are unfurled by default
 * - Seen links render normally when URL is in seenUrls
 * - Unseen links remain disclosed
 *
 * Note: Uses wrappedJSObject to bypass Xray wrappers that can prevent
 * Lit property setters from firing in mochitest environment.
 */

const { MockEngineManager } = ChromeUtils.importESModule(
  "resource://testing-common/AIWindowTestUtils.sys.mjs"
);

/**
 * Test 1: Verify fail-closed default behavior.
 *
 * When seenUrls is empty (default), all http/https links should
 * be unfurled with a disclosure span.
 */
add_task(async function test_fail_closed_default() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.smartwindow.enabled", true]],
  });

  const tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "about:aichatcontent"
  );
  const browser = tab.linkedBrowser;

  try {
    await SpecialPowers.spawn(browser, [], async () => {
      if (content.document.readyState !== "complete") {
        await ContentTaskUtils.waitForEvent(content, "load");
      }

      const doc = content.document;

      function getRoot(el) {
        return el.shadowRoot ?? el;
      }

      await content.customElements.whenDefined("ai-chat-message");

      const el = doc.createElement("ai-chat-message");
      doc.body.appendChild(el);

      const elJS = el.wrappedJSObject || el;

      // `role` reflects to the `data-message-role` attribute: the bare `role`
      // attribute is the native ARIA role and collides with the reactive
      // property, so it's set via the data-* attribute instead.
      // TODO: rename the reactive property off `role` so callers can set it
      // directly (`elJS.role = "assistant"`).
      el.setAttribute("data-message-role", "assistant");
      elJS.messageId = "test-fail-closed";
      el.setAttribute("data-message-id", "test-fail-closed");
      elJS.message =
        "Check out [Example](https://example.com) and [Test](https://test.com).";
      el.setAttribute(
        "message",
        "Check out [Example](https://example.com) and [Test](https://test.com)."
      );

      await ContentTaskUtils.waitForCondition(() => {
        const div = getRoot(el).querySelector(".message-assistant");
        return (
          div && div.querySelectorAll(".untrusted-link-label").length === 2
        );
      }, "Both untrusted link labels should be rendered");

      const assistantDiv = getRoot(el).querySelector(".message-assistant");

      const anchors = [...assistantDiv.querySelectorAll("a[href]")];
      Assert.equal(anchors.length, 2, "Should have 2 disclosure anchors");

      for (const anchor of anchors) {
        Assert.equal(
          anchor.textContent,
          anchor.getAttribute("href"),
          "Disclosure anchor text should match its href"
        );
      }

      el.remove();
    });
  } finally {
    await BrowserTestUtils.removeTab(tab);
    await SpecialPowers.popPrefEnv();
  }
});

/**
 * Test 2: Verify seen and unseen links are handled correctly.
 *
 * When seenUrls contains some URLs:
 * - Matching URLs should render normally (clickable, original text)
 * - Non-matching URLs should be unfurled with disclosure
 */
add_task(async function test_trusted_and_untrusted_links() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.smartwindow.enabled", true]],
  });

  const tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "about:aichatcontent"
  );
  const browser = tab.linkedBrowser;

  const seenUrl = "https://trusted.example.com/page";
  const unseenUrl = "https://untrusted.example.com/page";

  try {
    await SpecialPowers.spawn(
      browser,
      [seenUrl, unseenUrl],
      async (seen, unseen) => {
        if (content.document.readyState !== "complete") {
          await ContentTaskUtils.waitForEvent(content, "load");
        }

        const doc = content.document;

        function getRoot(el) {
          return el.shadowRoot ?? el;
        }

        await content.customElements.whenDefined("ai-chat-message");

        const el = doc.createElement("ai-chat-message");
        doc.body.appendChild(el);

        const elJS = el.wrappedJSObject || el;

        // Set seenUrls BEFORE message so it's available during first render
        elJS.seenUrls = Cu.cloneInto(new Set([seen]), content);
        // See note above: drive `role` via `data-message-role` (ARIA role
        // collision; TODO rename the reactive property off `role`).
        el.setAttribute("data-message-role", "assistant");
        elJS.messageId = "test-trusted-untrusted";
        el.setAttribute("data-message-id", "test-trusted-untrusted");
        elJS.message = `Visit [Trusted](${seen}) or [Untrusted](${unseen}).`;
        el.setAttribute(
          "message",
          `Visit [Trusted](${seen}) or [Untrusted](${unseen}).`
        );

        await ContentTaskUtils.waitForCondition(() => {
          const div = getRoot(el).querySelector(".message-assistant");
          return div && div.querySelectorAll("a[href]").length === 2;
        }, "Both clickable anchors should be rendered");

        const assistantDiv = getRoot(el).querySelector(".message-assistant");

        const seenAnchor = assistantDiv.querySelector(`a[href="${seen}"]`);
        Assert.ok(seenAnchor, "Seen URL anchor should exist");
        Assert.equal(
          seenAnchor.textContent,
          "Trusted",
          "Trusted anchor should have original text"
        );

        const unseenLabel = assistantDiv.querySelector(".untrusted-link-label");
        Assert.ok(unseenLabel, "Unseen link label should exist");
        Assert.equal(
          unseenLabel.textContent,
          "Untrusted",
          "Untrusted label should have original text"
        );

        const disclosureAnchor = assistantDiv.querySelector(
          `a[href="${unseen}"]`
        );
        Assert.ok(disclosureAnchor, "Disclosure anchor should exist");
        Assert.equal(
          disclosureAnchor.textContent,
          unseen,
          "Disclosure anchor text should show the URL"
        );

        el.remove();
      }
    );
  } finally {
    await BrowserTestUtils.removeTab(tab);
    await SpecialPowers.popPrefEnv();
  }
});

/**
 * Test 3: Verify seenUrls updates trigger re-render with correct link states.
 *
 * When seenUrls is updated to a new Set instance containing a previously
 * unseen URL, the component should re-render and remove the disclosure.
 */
add_task(async function test_trust_update_triggers_rerender() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.smartwindow.enabled", true]],
  });

  const tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "about:aichatcontent"
  );
  const browser = tab.linkedBrowser;

  const testUrl = "https://example.com/article";

  try {
    await SpecialPowers.spawn(browser, [testUrl], async url => {
      if (content.document.readyState !== "complete") {
        await ContentTaskUtils.waitForEvent(content, "load");
      }

      const doc = content.document;

      function getRoot(el) {
        return el.shadowRoot ?? el;
      }

      await content.customElements.whenDefined("ai-chat-message");

      const el = doc.createElement("ai-chat-message");
      doc.body.appendChild(el);

      const elJS = el.wrappedJSObject || el;

      // Initial render with empty seenUrls (default)
      // See note above: drive `role` via `data-message-role` (ARIA role
      // collision; TODO rename the reactive property off `role`).
      el.setAttribute("data-message-role", "assistant");
      elJS.messageId = "test-trust-update";
      el.setAttribute("data-message-id", "test-trust-update");
      elJS.message = `Read this [Article](${url}).`;
      el.setAttribute("message", `Read this [Article](${url}).`);

      // Wait for unseen link disclosure
      await ContentTaskUtils.waitForCondition(() => {
        const div = getRoot(el).querySelector(".message-assistant");
        if (!div) {
          return false;
        }
        return div.querySelector(".untrusted-link-label");
      }, "Initial render should show unseen link label");

      let assistantDiv = getRoot(el).querySelector(".message-assistant");

      Assert.ok(
        assistantDiv.querySelector(".untrusted-link-label"),
        "Link should show unseen label initially (fail-closed)"
      );

      // Update seenUrls to include the URL
      elJS.seenUrls = Cu.cloneInto(new Set([url]), content);

      await ContentTaskUtils.waitForCondition(() => {
        const div = getRoot(el).querySelector(".message-assistant");
        if (!div) {
          return false;
        }
        return !div.querySelector(".untrusted-link-label");
      }, "Re-render should remove untrusted label");

      assistantDiv = getRoot(el).querySelector(".message-assistant");
      const anchor = assistantDiv.querySelector("a");

      Assert.ok(
        anchor.hasAttribute("href"),
        "Link should be enabled after seenUrls update"
      );
      Assert.equal(
        anchor.getAttribute("href"),
        url,
        "Link href should match seen URL"
      );
      Assert.equal(
        anchor.textContent,
        "Article",
        "Seen link should have original text"
      );

      el.remove();
    });
  } finally {
    await BrowserTestUtils.removeTab(tab);
    await SpecialPowers.popPrefEnv();
  }
});

/**
 * Test 5 (Integration Smoke): Verify URL seen-state validation in a real AI Window
 * via the actor chain.
 *
 * This test exercises the real push chain:
 * - dispatchSeenUrlsToChatContent sends seen URLs from parent to child
 * - Child dispatches event to ai-chat-content
 * - ai-chat-message unfurls links not in seenUrls
 */
add_task(async function test_aiwindow_component_trust_smoke() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.smartwindow.enabled", true]],
  });

  const restoreSignIn = skipSignIn();

  const seenUrl = "https://trusted.example.com/page";
  const unseenUrl = "https://untrusted.example.com/page";
  const testConversationId = "test-integration-conv-" + Date.now();

  let win;
  try {
    win = await openAIWindow();
    const browser = win.gBrowser.selectedBrowser;

    let innerBC = await SpecialPowers.spawn(browser, [], async () => {
      await content.customElements.whenDefined("ai-window");

      const aiWindowElement = content.document.querySelector("ai-window");
      Assert.ok(aiWindowElement, "ai-window element should exist");

      await ContentTaskUtils.waitForCondition(() => {
        const container =
          aiWindowElement.shadowRoot?.querySelector("#browser-container");
        return container?.querySelector("browser");
      }, "Browser container should have browser element");

      const nestedBrowser =
        aiWindowElement.shadowRoot.querySelector("#aichat-browser");
      Assert.ok(nestedBrowser, "Nested aichat-browser should exist");
      return nestedBrowser.browsingContext;
    });

    if (innerBC.currentURI.spec != "about:aichatcontent") {
      await BrowserTestUtils.browserLoaded(innerBC.embedderElement, {
        wantLoad: "about:aichatcontent",
      });
      innerBC = browser.browsingContext.children[0];
    }
    Assert.equal(
      innerBC.currentURI.spec,
      "about:aichatcontent",
      "Inner browser should load chat content"
    );
    Assert.equal(
      innerBC.currentRemoteType,
      "privilegedabout",
      "Inner browser should have privilegedabout remote type."
    );
    await SpecialPowers.spawn(innerBC, [], async () => {
      await ContentTaskUtils.waitForCondition(() => {
        return content.document?.querySelector("ai-chat-content");
      }, "ai-chat-content should exist in nested browser");

      const innerDoc = content.document;
      const chatContent = innerDoc.querySelector("ai-chat-content");
      Assert.ok(chatContent, "ai-chat-content should exist");

      await ContentTaskUtils.waitForCondition(() => {
        try {
          const chatContentJS = chatContent.wrappedJSObject || chatContent;
          return chatContentJS.shadowRoot?.querySelector(
            ".chat-content-wrapper"
          );
        } catch {
          return false;
        }
      }, "ai-chat-content shadow DOM should be rendered");
    });

    // Push seen URLs via the actor chain
    const actor = innerBC.currentWindowGlobal.getActor("AIChatContent");
    actor.dispatchSeenUrlsToChatContent({
      conversationId: testConversationId,
      seenUrls: new Set([seenUrl]),
    });

    info(
      `Dispatched seen URL ${seenUrl} for conversation ${testConversationId}`
    );

    // Dispatch message and verify links
    await SpecialPowers.spawn(
      innerBC,
      [seenUrl, unseenUrl, testConversationId],
      async (seen, unseen, convId) => {
        const innerDoc = content.document;
        const chatContent = innerDoc.querySelector("ai-chat-content");
        const chatContentJS = chatContent.wrappedJSObject || chatContent;

        // Wait for seenUrls to arrive via the actor push chain
        await ContentTaskUtils.waitForCondition(() => {
          return chatContentJS.seenUrls?.size > 0;
        }, "seenUrls should be pushed via actor chain");

        const testMessageId = "test-integration-msg";
        const eventDetail = Cu.cloneInto(
          {
            role: "assistant",
            ordinal: 0,
            id: testMessageId,
            content: {
              body: `Visit [Trusted](${seen}) or [Untrusted](${unseen}).`,
            },
            memoriesApplied: [],
            tokens: { search: [] },
            webSearchQueries: [],
            followUpSuggestions: [],
            convId,
          },
          content
        );

        const messageEvent = new content.CustomEvent(
          "aiChatContentActor:message",
          {
            detail: eventDetail,
            bubbles: true,
          }
        );

        chatContent.dispatchEvent(messageEvent);

        await ContentTaskUtils.waitForCondition(() => {
          const msg = chatContent.shadowRoot?.querySelector(
            `ai-chat-message[data-message-id="${testMessageId}"]`
          );
          if (!msg) {
            return false;
          }
          const assistantDiv = (msg.shadowRoot ?? msg).querySelector(
            ".message-assistant"
          );
          return (
            assistantDiv?.querySelectorAll("a[href]").length === 2 &&
            assistantDiv?.querySelector(".untrusted-link-label")
          );
        }, "Message with seen anchor, unseen label, and disclosure anchor should render");

        const messageEl = chatContent.shadowRoot.querySelector(
          `ai-chat-message[data-message-id="${testMessageId}"]`
        );
        const assistantDiv = (messageEl.shadowRoot ?? messageEl).querySelector(
          ".message-assistant"
        );

        const seenAnchor = assistantDiv.querySelector(`a[href="${seen}"]`);
        Assert.ok(seenAnchor, "Seen URL anchor should exist");
        Assert.equal(
          seenAnchor.textContent,
          "Trusted",
          "Seen anchor should have original text"
        );

        const unseenLabel = assistantDiv.querySelector(".untrusted-link-label");
        Assert.ok(unseenLabel, "Unseen link label should exist");
        Assert.equal(
          unseenLabel.textContent,
          "Untrusted",
          "Unseen label text should match"
        );

        const disclosureAnchor = assistantDiv.querySelector(
          `a[href="${unseen}"]`
        );
        Assert.ok(
          disclosureAnchor,
          "Disclosure anchor should exist for unseen URL"
        );
        Assert.equal(
          disclosureAnchor.textContent,
          unseen,
          "Disclosure anchor should show the URL"
        );
      }
    );
  } finally {
    if (win) {
      await BrowserTestUtils.closeWindow(win);
    }
    restoreSignIn();
    await SpecialPowers.popPrefEnv();
  }
});

/**
 * Regression test: A malformed URL that causes URL.parse() to return null must
 * not throw. Exercises the full flow from LLM response to rendered message.
 */
add_task(async function test_malformed_url_does_not_throw() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.smartwindow.enabled", true]],
  });
  const mockEngineManager = new MockEngineManager();
  const restoreSignIn = skipSignIn();

  let win;
  try {
    win = await openAIWindow();
    const browser = win.gBrowser.selectedBrowser;

    await typeInSmartbar(browser, "hello");
    await submitSmartbar(browser);
    await mockEngineManager.respondTo({
      purpose: "chat",
      response: "Oops: [click](http://[unclosed)",
    });

    // If #isSettingsURL throws on a null-parsed URL then getAssistantMessage()
    // never completes and no assistant message appears.
    await TestUtils.waitForCondition(async () => {
      const messages = await getSidebarChatMessages(browser);
      return messages.some(m => m.role === "assistant" && m.hasRendered);
    }, "Assistant message should render without throwing for a malformed URL");
  } finally {
    if (win) {
      await BrowserTestUtils.closeWindow(win);
    }
    await mockEngineManager.rejectAllRequests();
    mockEngineManager.cleanupMocks();
    restoreSignIn();
    await SpecialPowers.popPrefEnv();
  }
});
