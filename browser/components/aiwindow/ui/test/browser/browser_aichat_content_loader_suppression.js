/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * The pending loader (chat-assistant-loader spinner) is only shown while the
 * assistant is generating AND there is nothing else on screen communicating
 * progress. It is suppressed once an action log is processing (its shimmering
 * label conveys progress) and once the assistant reply is streaming (its text
 * is visible). It is hidden entirely when the assistant is not generating.
 */
add_task(async function test_loader_suppression_across_states() {
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

      const setGenerating = isGenerating => {
        const cloned = Cu.cloneInto({ isGenerating }, content);
        chatContent.dispatchEvent(
          new content.CustomEvent("aiChatContentActor:set-generating", {
            detail: cloned,
            bubbles: true,
          })
        );
      };

      const settle = () => chatContent.wrappedJSObject.updateComplete;
      const loader = () =>
        chatContent.shadowRoot.querySelector("chat-assistant-loader");
      const actionCard = () =>
        chatContent.shadowRoot.querySelector("ai-action-result");

      // State 1: generating with only a user prompt on screen -> loader shown.
      dispatch({
        role: "user",
        convId: "c1",
        ordinal: 1,
        content: { body: "What tabs are open?" },
      });
      setGenerating(true);
      await settle();

      Assert.ok(
        loader(),
        "Spinner is shown while generating with no action log or reply yet"
      );
      Assert.ok(!actionCard(), "No action log card yet in the waiting state");

      // State 2: a tool runs and its action log starts processing (no reply
      // body yet) -> loader suppressed, action card in the loading state.
      dispatch({
        role: "tool",
        convId: "c1",
        ordinal: 2,
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
      await settle();

      Assert.ok(
        !loader(),
        "Spinner is suppressed while an action log is processing"
      );
      Assert.ok(actionCard(), "Action log card is rendered while processing");
      Assert.ok(
        actionCard().hasAttribute("is-loading"),
        "Action log card is in the loading (shimmering) state"
      );

      // State 3: the assistant reply starts streaming -> loader stays
      // suppressed, and the action log completes (no longer loading).
      dispatch({
        role: "assistant",
        convId: "c1",
        ordinal: 3,
        id: "a1",
        content: { body: "You have two tabs open." },
        memoriesApplied: [],
      });
      await settle();

      Assert.ok(
        !loader(),
        "Spinner stays suppressed once the reply is streaming"
      );
      Assert.ok(
        !actionCard().hasAttribute("is-loading"),
        "Action log completes as soon as the reply starts streaming"
      );

      // State 4: generation finishes -> loader hidden regardless.
      setGenerating(false);
      await settle();

      Assert.ok(!loader(), "Spinner is hidden once generation finishes");
    });
  });
});
