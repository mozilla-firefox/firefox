/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/* import-globals-from head.js */

const { MockEngineManager } = ChromeUtils.importESModule(
  "resource://testing-common/AIWindowTestUtils.sys.mjs"
);

/**
 * Checks completion metadata and work counts across initial and later turns,
 * excluding retries.
 *
 * @param {string} location - The chat request location.
 * @returns {Promise<void>}
 */
async function testTurnCompletion(location) {
  for (const useTool of [false, true]) {
    const engineManager = new MockEngineManager();
    let win;

    try {
      let browser;
      if (location === "home") {
        win = await openAIWindow();
        browser = win.gBrowser.selectedBrowser;
      } else {
        ({ win, sidebarBrowser: browser } = await openAIWindowWithSidebar());
      }
      Services.fog.testResetFOG();

      info(`${location}: Submit the first turn, with tool use: ${useTool}`);
      const submittedAt = ChromeUtils.now();
      await typeInSmartbar(browser, "hello");
      await submitSmartbar(browser);
      let pending = await engineManager.captureRequest({ purpose: "chat" });
      const capturedAt = ChromeUtils.now();
      const context = browser.contentDocument
        .querySelector("ai-window")
        .conversation.compactChatCompletions();
      const [request] = Glean.smartWindow.modelRequest.testGetValue();

      for (const [index, outputTokens] of (useTool ? [3, 7] : []).entries()) {
        pending.respond({
          text: "Let me check your tabs.",
          usage: { completion_tokens: outputTokens },
          toolCalls: [
            {
              id: `first-turn-tool-${index}`,
              type: "function",
              function: { name: "get_open_tabs", arguments: "{}" },
            },
          ],
        });
        pending = await engineManager.captureRequest({ purpose: "chat" });
        Assert.equal(
          Glean.smartWindow.toolCall.testGetValue()?.length,
          index + 1,
          "Each real tool completes before the follow-up model request"
        );
      }

      Assert.equal(
        Glean.smartWindow.timeToTurnComplete.testGetValue(),
        undefined,
        "An unfinished turn does not record completion, even after a tool result"
      );
      const completingAt = ChromeUtils.now();
      pending.respond({ text: "Done.", usage: { completion_tokens: 11 } });
      await TestUtils.waitForCondition(
        () => Glean.smartWindow.modelResponse.testGetValue()?.length === 1,
        "The first turn completes"
      );

      const events = Glean.smartWindow.timeToTurnComplete.testGetValue();
      Assert.equal(events?.length, 1, "One first-turn event is recorded");
      const { extra } = events[0];
      Assert.equal(
        extra.location,
        location,
        "The request location is retained"
      );
      Assert.equal(
        extra.chat_id,
        request.extra.chat_id,
        "The conversation is retained"
      );
      Assert.equal(
        extra.request_id,
        request.extra.request_id,
        "The original user request is retained after tool use"
      );
      Assert.equal(
        Number(extra.message_seq),
        2,
        "The initial assistant response is identified"
      );
      Assert.equal(
        Number(extra.tool_call_count),
        useTool ? 2 : 0,
        "All attempted tool executions are counted"
      );
      Assert.equal(
        Number(extra.output_tokens),
        useTool ? 21 : 11,
        "Output tokens include every model response in the first turn"
      );
      Assert.equal(
        Number(extra.turn_index),
        0,
        "The initial turn is indexed zero"
      );
      Assert.equal(
        Number(extra.context_message_count),
        context.length,
        "Completion retains the initial context message count"
      );
      Assert.equal(
        Number(extra.context_length),
        JSON.stringify(context).length,
        "Completion retains context size from before any tool follow-ups"
      );
      Assert.ok(extra.model, "The response model is recorded");
      Assert.ok(
        /^\d+$/.test(extra.duration),
        "Duration is integer milliseconds"
      );
      Assert.greaterOrEqual(
        Number(extra.duration),
        Math.floor(completingAt - capturedAt),
        "Duration includes the entire pending request and any tool follow-up"
      );
      Assert.lessOrEqual(
        Number(extra.duration),
        Math.ceil(ChromeUtils.now() - submittedAt),
        "Duration starts after submission begins"
      );

      info("Retry the completed first turn");
      const aiWindow = browser.contentDocument.querySelector("ai-window");
      const response = aiWindow.conversation.messages.find(
        message =>
          message.parentMessageId === request.extra.request_id &&
          message.content.type === "text"
      );
      aiWindow.handleFooterAction({
        action: "retry",
        messageId: response.id,
      });
      await engineManager.respondTo({
        purpose: "chat",
        response: "Retried.",
      });
      await TestUtils.waitForCondition(
        () => Glean.smartWindow.modelResponse.testGetValue()?.length === 2,
        "The retry completes"
      );
      Assert.equal(
        Glean.smartWindow.timeToTurnComplete.testGetValue().length,
        1,
        "Retrying the first turn does not record again"
      );

      info("Submit a later turn in the same conversation");
      await typeInSmartbar(browser, "another question");
      await submitSmartbar(browser);
      const laterRequest = await engineManager.captureRequest({
        purpose: "chat",
      });
      const laterContext = aiWindow.conversation.compactChatCompletions();

      laterRequest.respond({
        text: "Another reply.",
        usage: { completion_tokens: 5 },
      });
      await TestUtils.waitForCondition(
        () => Glean.smartWindow.modelResponse.testGetValue()?.length === 3,
        "The later turn completes"
      );
      Assert.equal(
        Glean.smartWindow.timeToTurnComplete.testGetValue().length,
        2,
        "Later turns also record completion"
      );
      const later =
        Glean.smartWindow.timeToTurnComplete.testGetValue()[1].extra;

      Assert.equal(
        Number(later.turn_index),
        1,
        "A retry does not advance the turn index"
      );
      Assert.equal(
        later.chat_id,
        extra.chat_id,
        "Both turns belong to the same chat"
      );
      Assert.notEqual(
        later.request_id,
        extra.request_id,
        "Each turn retains its own request"
      );
      Assert.equal(
        Number(later.context_message_count),
        laterContext.length,
        "Later turns capture their own context message count"
      );
      Assert.equal(
        Number(later.context_length),
        JSON.stringify(laterContext).length,
        "Later turns capture their own compacted context size"
      );
      Assert.equal(
        Number(later.output_tokens),
        5,
        "Token totals reset for each turn"
      );
      Assert.equal(
        Number(later.tool_call_count),
        0,
        "Tool counts reset for each turn"
      );
    } finally {
      if (win) {
        await BrowserTestUtils.closeWindow(win);
      }
      await engineManager.rejectAllRequests();
      engineManager.cleanupMocks();
      Services.fog.testResetFOG();
    }
  }
}
