/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { MockEngineManager, MockSearchManager } = ChromeUtils.importESModule(
  "resource://testing-common/AIWindowTestUtils.sys.mjs"
);

const {
  SEARCH_QUERY_ENDPOINT_PREF,
  SEARCH_QUERY_APIKEY_PREF,
  SEARCH_THE_WEB_FAST_PREF,
} = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/Tools.sys.mjs"
);

/**
 * This test case measures pending and completed tool entries independently
 * of response text, and keeps old turns from recording again in either layout.
 */
add_task(async function test_first_action_log_telemetry() {
  await SpecialPowers.pushPrefEnv({
    set: [
      [SEARCH_QUERY_ENDPOINT_PREF, "https://search.example.test/v1/search"],
      [SEARCH_QUERY_APIKEY_PREF, "mock-search-api-key"],
      [SEARCH_THE_WEB_FAST_PREF, true],
    ],
  });

  try {
    for (const location of ["home", "sidebar"]) {
      const engineManager = new MockEngineManager();
      const searchManager = new MockSearchManager();
      const sandbox = sinon.createSandbox();
      let win;

      try {
        let browser;
        if (location === "home") {
          win = await openAIWindow();
          browser = win.gBrowser.selectedBrowser;
        } else {
          ({ win, sidebarBrowser: browser } = await openAIWindowWithSidebar());
        }
        const aiWindow = browser.contentDocument.querySelector("ai-window");
        const firstCharacterDisplayed = sandbox.spy(
          aiWindow,
          "onFirstCharacterDisplayed"
        );
        Services.fog.testResetFOG();

        info(`${location}: Submit a turn without any action log`);
        await typeInSmartbar(browser, "hello");
        await submitSmartbar(browser);
        await engineManager.respondTo({ purpose: "chat", response: "Hello." });
        await TestUtils.waitForCondition(
          () => Glean.smartWindow.modelResponse.testGetValue()?.length === 1,
          "The text-only response completes"
        );
        await TestUtils.waitForCondition(
          () =>
            Glean.smartWindow.timeToFirstCharacter.testGetValue()?.length === 1,
          "The text-only response records its first rendered character before the next turn"
        );
        Assert.equal(
          Glean.smartWindow.timeToFirstActionLog.testGetValue(),
          undefined,
          "A text-only response does not record action-log latency"
        );

        for (const [index, tool] of [
          "search_the_web",
          "get_open_tabs",
        ].entries()) {
          info(`${location}: Submit a turn using ${tool}`);
          await typeInSmartbar(browser, `please use ${tool}`);
          await submitSmartbar(browser);
          const pendingRequest = await engineManager.captureRequest({
            purpose: "chat",
          });
          const requestEvent =
            Glean.smartWindow.modelRequest.testGetValue()[index + 1];
          const initialAssistant = aiWindow.conversation.messages.at(-1);
          const context = aiWindow.conversation.compactChatCompletions();

          await SpecialPowers.spawn(
            browser,
            [requestEvent.extra.chat_id],
            async conversationId => {
              const contentAIWindow =
                content.document.querySelector("ai-window");
              const currentMessage =
                contentAIWindow.conversation.messages.at(-1);
              const previousAssistant =
                contentAIWindow.conversation.messages.find(
                  message =>
                    message.role === currentMessage.role &&
                    message.turnIndex < currentMessage.turnIndex
                );
              contentAIWindow.onFirstCharacterDisplayed({
                conversationId: "old-conversation",
                messageId: currentMessage.id,
              });
              for (const messageId of [
                "unknown-message",
                previousAssistant.id,
                contentAIWindow.conversation.messages.at(-2).id,
              ]) {
                contentAIWindow.onFirstCharacterDisplayed({
                  conversationId,
                  messageId,
                });
              }
              contentAIWindow.onFirstActionLogDisplayed({
                conversationId: "old-conversation",
                turnIndex: contentAIWindow.conversation.currentTurnIndex(),
              });
              contentAIWindow.onFirstActionLogDisplayed({
                conversationId,
                turnIndex: -1,
              });
            }
          );
          Assert.equal(
            Glean.smartWindow.timeToFirstActionLog.testGetValue()?.length ?? 0,
            index,
            "Unrelated conversations and turns cannot record against a pending request"
          );
          Assert.equal(
            Glean.smartWindow.timeToFirstCharacter.testGetValue().length,
            index + 1,
            "Unrelated conversations, old turns, unknown messages, and user messages cannot record first-character latency"
          );
          pendingRequest.respond({
            text: index === 1 ? "Let me check." : "",
            toolCalls: [
              {
                id: `tool-${index}`,
                type: "function",
                function: {
                  name: tool,
                  arguments: JSON.stringify(
                    tool === "search_the_web" ? { query: "test" } : {}
                  ),
                },
              },
            ],
          });

          await TestUtils.waitForCondition(
            () =>
              Glean.smartWindow.timeToFirstActionLog.testGetValue()?.length ===
              index + 1,
            "The first visible action log records an event"
          );
          if (tool === "search_the_web") {
            Assert.equal(
              Glean.smartWindow.toolCall.testGetValue(),
              undefined,
              "The pending action log is reported before the search tool completes"
            );
            await searchManager.respondTo({ response: { results: [] } });
          }

          const event =
            Glean.smartWindow.timeToFirstActionLog.testGetValue()[index];
          const request =
            Glean.smartWindow.modelRequest.testGetValue()[index + 1];
          Assert.equal(
            event.extra.location,
            location,
            "The event retains the request location"
          );
          Assert.equal(
            event.extra.chat_id,
            request.extra.chat_id,
            "The event belongs to the submitted conversation"
          );
          Assert.equal(
            event.extra.request_id,
            request.extra.request_id,
            "The event belongs to the submitted request"
          );
          Assert.equal(
            Number(event.extra.message_seq),
            Number(request.extra.message_seq) + 1,
            "The event identifies the initial assistant response sequence"
          );
          Assert.equal(
            Number(event.extra.turn_index),
            index + 1,
            "Action logs identify later user turns independently of tool messages"
          );
          Assert.equal(
            Number(event.extra.context_message_count),
            context.length,
            "Context message count uses the initial compacted model input"
          );
          Assert.equal(
            Number(event.extra.context_length),
            JSON.stringify(context).length,
            "Context size is captured before tool results grow the history"
          );
          Assert.ok(
            event.extra.model,
            "The event identifies the response model"
          );
          Assert.ok(
            /^\d+$/.test(event.extra.latency),
            "Latency is an integer number of milliseconds"
          );

          if (index === 1) {
            await TestUtils.waitForCondition(
              () =>
                Glean.smartWindow.timeToFirstCharacter.testGetValue()
                  ?.length ===
                index + 2,
              "The preamble records first-character telemetry before the tool follow-up"
            );
          }

          await engineManager.respondTo({ purpose: "chat", response: "Done." });
          await TestUtils.waitForCondition(
            () =>
              Glean.smartWindow.modelResponse.testGetValue()?.length ===
              index + 2,
            "The tool response completes"
          );
          await TestUtils.waitForCondition(
            () =>
              Glean.smartWindow.timeToFirstCharacter.testGetValue()?.length ===
              index + 2,
            "First-character telemetry also records independently"
          );
          const replyMessage = aiWindow.conversation.messages.findLast(
            message => message.role === initialAssistant.role
          );
          await TestUtils.waitForCondition(
            () =>
              firstCharacterDisplayed.calledWithMatch({
                conversationId: request.extra.chat_id,
                messageId: replyMessage.id,
              }),
            "The rendered tool follow-up reaches the first-character handler"
          );
          Assert.equal(
            Glean.smartWindow.timeToFirstCharacter.testGetValue().length,
            index + 2,
            "Tool follow-ups do not duplicate first-character telemetry for a turn"
          );
          const characterEvent =
            Glean.smartWindow.timeToFirstCharacter.testGetValue()[index + 1];

          for (const key of [
            "location",
            "chat_id",
            "request_id",
            "message_seq",
            "model",
            "turn_index",
            "context_message_count",
            "context_length",
          ]) {
            Assert.equal(
              characterEvent.extra[key],
              event.extra[key],
              "First character and action log share the original turn context"
            );
          }

          Assert.equal(
            Glean.smartWindow.timeToFirstActionLog.testGetValue().length,
            index + 1,
            "Tool completion, response text, and old turns do not duplicate the event"
          );
        }
      } finally {
        if (win) {
          await BrowserTestUtils.closeWindow(win);
        }
        searchManager.rejectAllRequests();
        await engineManager.rejectAllRequests();
        engineManager.cleanupMocks();
        searchManager.cleanupMocks();
        sandbox.restore();
        Services.fog.testResetFOG();
      }
    }
  } finally {
    await SpecialPowers.popPrefEnv();
  }
});
