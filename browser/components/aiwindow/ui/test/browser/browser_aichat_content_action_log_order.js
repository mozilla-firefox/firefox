/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Action log entries render above
 * the assistant's text bubble within the same turn
 */
add_task(async function test_action_log_renders_above_assistant_bubble() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.smartwindow.enabled", true]],
  });

  await BrowserTestUtils.withNewTab("about:aichatcontent", async browser => {
    await SpecialPowers.spawn(browser, [], async () => {
      await content.customElements.whenDefined("ai-chat-content");
      const chatContent = content.document.querySelector("ai-chat-content");

      const dispatch = detail => {
        // AIChatContentChild actor clones the payload into
        // the content compartment before dispatching
        const clonedDetail = Cu.cloneInto(detail, content);
        chatContent.dispatchEvent(
          new content.CustomEvent("aiChatContentActor:message", {
            detail: clonedDetail,
            bubbles: true,
          })
        );
      };

      // User submits a prompt - ordinal 1
      dispatch({
        role: "user",
        convId: "conv1",
        ordinal: 1,
        content: { body: "What tabs are open?" },
      });

      // Empty assistant placeholder lands at ordinal 2
      dispatch({
        role: "assistant",
        convId: "conv1",
        ordinal: 2,
        id: "msg-assist-1",
        content: { body: "You have two tabs open." },
        memoriesApplied: [],
        followUpSuggestions: [],
      });

      // Tool message arrives at ordinal 3 - higher than the assistant
      // placeholder ordinal. Carries the actionLog payload that the
      // parent side dispatch in ai-window normally attaches
      dispatch({
        role: "tool",
        convId: "conv1",
        ordinal: 3,
        content: {
          tool_call_id: "tc_open_tabs",
          name: "get_open_tabs",
          body: [
            { url: "https://example.com/", title: "Example" },
            { url: "https://firefox.com/", title: "Firefox" },
          ],
        },
        actionLog: {
          uiType: "action-log",
          label: { l10nId: "action-log-got-open-tabs" },
          pendingLabel: { l10nId: "action-log-getting-open-tabs" },
          row: {
            labelL10nId: "action-log-got-open-tabs",
            labelL10nArgs: null,
            items: [],
          },
        },
      });

      // updateComplete is on the unwrapped element
      await chatContent.wrappedJSObject.updateComplete;

      const inner = chatContent.shadowRoot.querySelector(".chat-inner-wrapper");
      const children = Array.from(inner.children);

      Assert.greaterOrEqual(
        children.length,
        3,
        "User bubble, action log card, and assistant bubble are rendered"
      );

      // Find each rendered piece
      const userBubble = children.find(c =>
        c.classList?.contains("chat-bubble-user")
      );
      const assistantBubble = children.find(c =>
        c.classList?.contains("chat-bubble-assistant")
      );
      const actionCard = children.find(
        c => c.tagName?.toLowerCase() === "ai-action-result"
      );

      Assert.ok(userBubble, "User bubble rendered");
      Assert.ok(assistantBubble, "Assistant bubble rendered");
      Assert.ok(actionCard, "Action log <ai-action-result> rendered");

      // Action log card precedes the assistant
      // bubble in DOM order even though the assistant has a lower ordinal.
      Assert.less(
        children.indexOf(actionCard),
        children.indexOf(assistantBubble),
        "Action log card appears above the assistant bubble"
      );

      // And both come after the user bubble
      Assert.less(
        children.indexOf(userBubble),
        children.indexOf(actionCard),
        "User bubble appears above the action log card"
      );
    });
  });
});

add_task(async function test_action_log_skips_when_actionLog_missing() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.smartwindow.enabled", true]],
  });

  await BrowserTestUtils.withNewTab("about:aichatcontent", async browser => {
    await SpecialPowers.spawn(browser, [], async () => {
      await content.customElements.whenDefined("ai-chat-content");
      const chatContent = content.document.querySelector("ai-chat-content");

      const detail = Cu.cloneInto(
        {
          role: "tool",
          convId: "conv1",
          ordinal: 1,
          content: {
            tool_call_id: "tc_x",
            name: "get_open_tabs",
            body: [],
          },
          // no actionLog
        },
        content
      );
      chatContent.dispatchEvent(
        new content.CustomEvent("aiChatContentActor:message", {
          detail,
          bubbles: true,
        })
      );

      await chatContent.updateComplete;

      const card = chatContent.shadowRoot.querySelector("ai-action-result");
      Assert.equal(
        card,
        null,
        "No action log card rendered when actionLog payload is missing"
      );
    });
  });
});

add_task(async function test_action_log_persists_across_turns() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.smartwindow.enabled", true]],
  });

  await BrowserTestUtils.withNewTab("about:aichatcontent", async browser => {
    await SpecialPowers.spawn(browser, [], async () => {
      await content.customElements.whenDefined("ai-chat-content");
      const chatContent = content.document.querySelector("ai-chat-content");

      const dispatch = detail => {
        const clonedDetail = Cu.cloneInto(detail, content);
        chatContent.dispatchEvent(
          new content.CustomEvent("aiChatContentActor:message", {
            detail: clonedDetail,
            bubbles: true,
          })
        );
      };

      // Turn 1
      dispatch({
        role: "user",
        convId: "c1",
        ordinal: 1,
        content: { body: "Q1" },
      });
      dispatch({
        role: "assistant",
        convId: "c1",
        ordinal: 2,
        id: "msg-a1",
        content: { body: "A1" },
        memoriesApplied: [],
      });
      dispatch({
        role: "tool",
        convId: "c1",
        ordinal: 3,
        content: { tool_call_id: "t1", name: "get_open_tabs", body: [] },
        actionLog: {
          uiType: "action-log",
          label: { l10nId: "action-log-got-open-tabs" },
          pendingLabel: { l10nId: "action-log-getting-open-tabs" },
          row: {
            labelL10nId: "action-log-got-open-tabs",
            labelL10nArgs: null,
            items: [],
          },
        },
      });

      // Turn 2 - past turn should still be visible
      dispatch({
        role: "user",
        convId: "c1",
        ordinal: 4,
        content: { body: "Q2" },
      });

      await chatContent.updateComplete;

      const cards = chatContent.shadowRoot.querySelectorAll("ai-action-result");
      Assert.equal(
        cards.length,
        1,
        "Past turn's action log card is still rendered"
      );

      const userBubbles =
        chatContent.shadowRoot.querySelectorAll(".chat-bubble-user");
      Assert.equal(userBubbles.length, 2, "Both user bubbles rendered");
    });
  });
});

add_task(
  async function test_context_chip_shown_only_on_page_change_across_tool_call() {
    await SpecialPowers.pushPrefEnv({
      set: [["browser.smartwindow.enabled", true]],
    });

    await BrowserTestUtils.withNewTab("about:aichatcontent", async browser => {
      await SpecialPowers.spawn(browser, [], async () => {
        await content.customElements.whenDefined("ai-chat-content");
        const chatContent = content.document.querySelector("ai-chat-content");

        const dispatch = detail => {
          const cloned = Cu.cloneInto(detail, content);
          chatContent.dispatchEvent(
            new content.CustomEvent("aiChatContentActor:message", {
              detail: cloned,
              bubbles: true,
            })
          );
        };

        const PAGE_A = "https://example.com/pageA";
        const PAGE_B = "https://example.com/pageB";

        const userWithPageChip = (ordinal, pageUrl, body) => ({
          role: "user",
          convId: "c1",
          ordinal,
          content: {
            body,
            contextPageUrl: pageUrl,
            contextMentions: [{ url: pageUrl, label: "Page chip" }],
          },
        });

        // user1 on pageA - first turn, chip should be visible.
        dispatch(userWithPageChip(1, PAGE_A, "Q1"));
        dispatch({
          role: "assistant",
          convId: "c1",
          ordinal: 2,
          id: "msg-a1",
          content: { body: "A1" },
          memoriesApplied: [],
        });
        // Tool in between user message1 and user message2
        dispatch({
          role: "tool",
          convId: "c1",
          ordinal: 3,
          content: { tool_call_id: "t1", name: "get_open_tabs", body: [] },
          actionLog: {
            uiType: "action-log",
            label: { l10nId: "action-log-got-open-tabs" },
            pendingLabel: { l10nId: "action-log-getting-open-tabs" },
            row: {
              labelL10nId: "action-log-got-open-tabs",
              labelL10nArgs: null,
              items: [],
            },
          },
        });

        // user message2 on pageA - same page as user message1, chip should not show
        dispatch(userWithPageChip(4, PAGE_A, "Q2"));
        // user message3 on pageB - page changed, chip should be visible
        dispatch(userWithPageChip(5, PAGE_B, "Q3"));

        await chatContent.updateComplete;

        const userBubbles =
          chatContent.shadowRoot.querySelectorAll(".chat-bubble-user");
        Assert.equal(
          userBubbles.length,
          3,
          "Three user message bubbles rendered"
        );

        const hasChipContainer = bubble =>
          !!bubble.querySelector("website-chip-container");

        Assert.ok(
          hasChipContainer(userBubbles[0]),
          "user message1 - chip is visible on the first turn"
        );
        Assert.ok(
          !hasChipContainer(userBubbles[1]),
          "user message2 - page chip suppressed because same page as user1"
        );
        Assert.ok(
          hasChipContainer(userBubbles[2]),
          "user message3 - chip visible again because page changed to pageB"
        );
      });
    });
  }
);

add_task(async function test_tool_turn_with_preamble_renders_one_bubble() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.smartwindow.enabled", true]],
  });

  await BrowserTestUtils.withNewTab("about:aichatcontent", async browser => {
    await SpecialPowers.spawn(browser, [], async () => {
      await content.customElements.whenDefined("ai-chat-content");
      const chatContent = content.document.querySelector("ai-chat-content");
      const root = chatContent.shadowRoot;
      const count = selector =>
        chatContent.shadowRoot.querySelectorAll(selector).length;
      const dispatch = (type, detail) =>
        chatContent.dispatchEvent(
          new content.CustomEvent(type, {
            detail: Cu.cloneInto(detail, content),
            bubbles: true,
          })
        );
      const message = detail =>
        dispatch("aiChatContentActor:message", { convId: "conv1", ...detail });
      const assistant = (ordinal, id, body) =>
        message({
          role: "assistant",
          ordinal,
          id,
          content: { body },
          memoriesApplied: [],
          followUpSuggestions: [],
        });

      message({ role: "user", ordinal: 1, content: { body: "What tabs?" } });
      assistant(2, "msg-preamble", "Let me look.");
      await chatContent.wrappedJSObject.updateComplete;
      const chatBubble = root.querySelector(".chat-bubble-assistant");
      message({
        role: "tool",
        ordinal: 4,
        content: { tool_call_id: "tc_1", name: "get_open_tabs" },
        actionLog: {
          uiType: "action-log",
          pendingLabel: { l10nId: "action-log-getting-open-tabs" },
          row: {
            labelL10nId: "action-log-got-open-tabs",
            labelL10nArgs: null,
            items: [],
          },
        },
      });
      await chatContent.wrappedJSObject.updateComplete;
      Assert.strictEqual(
        root.querySelector(".chat-bubble-assistant"),
        chatBubble,
        "The bubble is kept when the action log appears above it"
      );

      assistant(5, "msg-reply", "You have no tabs open.");
      message({
        role: "assistant-message-complete",
        content: { id: "msg-reply" },
      });
      await chatContent.wrappedJSObject.updateComplete;

      const text = "Let me look.\n\nYou have no tabs open.";
      Assert.equal(count(".chat-bubble-assistant"), 1, "One assistant bubble");
      Assert.strictEqual(
        root.querySelector(".chat-bubble-assistant"),
        chatBubble,
        "The bubble is kept when the reply starts after the preamble"
      );
      Assert.equal(
        root.querySelector(".chat-bubble-assistant ai-chat-message")
          .wrappedJSObject.message,
        text,
        "The bubble shows the preamble and the reply"
      );
      Assert.equal(count("assistant-message-footer"), 1, "One footer");

      const copied = new Promise(resolve =>
        chatContent.addEventListener(
          "AIChatContent:DispatchAction",
          event => resolve(event.detail),
          { once: true }
        )
      );
      dispatch("copy-message", { messageId: "msg-reply" });
      Assert.equal((await copied).text, text, "Copy copies the whole turn");

      dispatch("aiChatContentActor:truncate", { messageId: "msg-reply" });
      await chatContent.wrappedJSObject.updateComplete;
      Assert.deepEqual(
        [
          count(".chat-bubble-user"),
          count("ai-action-result"),
          count(".chat-bubble-assistant"),
        ],
        [1, 0, 0],
        "Truncating the reply removes the turn but keeps the user message"
      );
    });
  });
});

add_task(async function test_action_log_loads_until_text_after_its_tools() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.smartwindow.enabled", true]],
  });

  await BrowserTestUtils.withNewTab("about:aichatcontent", async browser => {
    await SpecialPowers.spawn(browser, [], async () => {
      await content.customElements.whenDefined("ai-chat-content");
      const chatContent = content.document.querySelector("ai-chat-content");
      const dispatch = (type, detail) =>
        chatContent.dispatchEvent(
          new content.CustomEvent(type, {
            detail: Cu.cloneInto(detail, content),
            bubbles: true,
          })
        );
      const message = detail =>
        dispatch("aiChatContentActor:message", { convId: "conv1", ...detail });
      const assistant = (ordinal, id, body) =>
        message({
          role: "assistant",
          ordinal,
          id,
          content: { body },
          memoriesApplied: [],
          followUpSuggestions: [],
        });
      const isLoading = async () => {
        await chatContent.wrappedJSObject.updateComplete;
        return chatContent.shadowRoot.querySelector("ai-action-result")
          .wrappedJSObject.isLoading;
      };

      dispatch("aiChatContentActor:set-generating", { isGenerating: true });
      message({ role: "user", ordinal: 1, content: { body: "What tabs?" } });
      assistant(2, "msg-preamble", "Let me look.");
      message({
        role: "tool",
        ordinal: 4,
        content: { tool_call_id: "tc_1", name: "get_open_tabs" },
        actionLog: {
          uiType: "action-log",
          pendingLabel: { l10nId: "action-log-getting-open-tabs" },
          row: {
            labelL10nId: "action-log-got-open-tabs",
            labelL10nArgs: null,
            items: [],
          },
        },
      });
      Assert.ok(
        await isLoading(),
        "The action log keeps loading while its tool runs after the preamble"
      );

      assistant(5, "msg-reply", "You have no tabs open.");
      Assert.ok(
        !(await isLoading()),
        "The action log completes once the reply streams after its tools"
      );
    });
  });
});

add_task(async function test_localized_message_after_reply_stays_separate() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.smartwindow.enabled", true]],
  });

  await BrowserTestUtils.withNewTab("about:aichatcontent", async browser => {
    await SpecialPowers.spawn(browser, [], async () => {
      await content.customElements.whenDefined("ai-chat-content");
      const chatContent = content.document.querySelector("ai-chat-content");
      const message = detail =>
        chatContent.dispatchEvent(
          new content.CustomEvent("aiChatContentActor:message", {
            detail: Cu.cloneInto(
              { convId: "conv1", isPreviousMessage: true, ...detail },
              content
            ),
            bubbles: true,
          })
        );

      message({ role: "user", ordinal: 1, content: { body: "Hello" } });
      message({
        role: "assistant",
        ordinal: 2,
        id: "msg-reply",
        content: { body: "Hi!" },
      });
      message({
        role: "assistant",
        ordinal: 3,
        id: "msg-l10n",
        content: {
          body: "",
          l10nId: "smartwindow-agent-monitor-limit-reached",
          l10nArgs: { count: 3 },
        },
      });
      await chatContent.wrappedJSObject.updateComplete;

      Assert.equal(
        chatContent.shadowRoot.querySelectorAll(".chat-bubble-assistant")
          .length,
        2,
        "A localized assistant message keeps its own bubble"
      );
    });
  });
});
