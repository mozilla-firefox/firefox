/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { MockEngineManager } = ChromeUtils.importESModule(
  "resource://testing-common/AIWindowTestUtils.sys.mjs"
);

const { SEARCH_THE_WEB_FAST_PREF, SEARCH_THE_WEB_ANSWERS_PREF } =
  ChromeUtils.importESModule(
    "moz-src:///browser/components/aiwindow/models/Tools.sys.mjs"
  );

/**
 * This test case ensures direct answers preserve chat model token totals,
 * including zero counts, while missing chat model usage still omits the total.
 */
add_task(async function test_first_turn_direct_answer_tokens() {
  await SpecialPowers.pushPrefEnv({
    clear: [["browser.smartwindow.endpoint"]],
    set: [
      [SEARCH_THE_WEB_FAST_PREF, false],
      [SEARCH_THE_WEB_ANSWERS_PREF, true],
    ],
  });

  try {
    for (const outputTokens of [3, 0, undefined]) {
      const engineManager = new MockEngineManager();
      let win;

      try {
        info(`Submit a search with chat model token usage: ${outputTokens}`);
        win = await openAIWindow();
        const browser = win.gBrowser.selectedBrowser;
        Services.fog.testResetFOG();

        await typeInSmartbar(browser, "search for museums");
        await submitSmartbar(browser);
        await engineManager.respondTo({
          purpose: "chat",
          response: {
            text: "",
            usage:
              outputTokens === undefined
                ? undefined
                : { completion_tokens: outputTokens },
            toolCalls: [
              {
                id: "direct-answer-search",
                type: "function",
                function: {
                  name: "search_the_web",
                  arguments: JSON.stringify({ query: "museums" }),
                },
              },
            ],
          },
        });
        const answer = await engineManager.captureRequest({
          purpose: "unknown",
        });
        Assert.equal(
          Glean.smartWindow.timeToTurnComplete.testGetValue(),
          undefined,
          "The turn waits for the direct answer"
        );

        const aiWindow = browser.contentDocument.querySelector("ai-window");
        await aiWindow.updateComplete;
        Assert.ok(aiWindow.isGenerating, "The turn is still generating");
        const generationFinished = BrowserTestUtils.waitForMutationCondition(
          aiWindow.renderRoot,
          { attributes: true, subtree: true },
          () => !aiWindow.isGenerating
        );

        info("Deliver the direct answer and wait for generation to finish");
        answer.respond({ finalOutput: "Visit the science museum." });
        await generationFinished;

        const events = Glean.smartWindow.timeToTurnComplete.testGetValue();
        Assert.equal(events?.length, 1, "One first-turn event is recorded");

        const { extra } = events[0];
        Assert.equal(
          Number(extra.tool_call_count),
          1,
          "The search tool is counted"
        );

        if (outputTokens === undefined) {
          Assert.ok(
            !("output_tokens" in extra),
            "Missing chat model usage still omits the token total"
          );
        } else {
          Assert.equal(
            Number(extra.output_tokens),
            outputTokens,
            "The direct answer preserves the chat model token total"
          );
        }
      } finally {
        if (win) {
          await BrowserTestUtils.closeWindow(win);
        }
        await engineManager.rejectAllRequests();
        engineManager.cleanupMocks();
        Services.fog.testResetFOG();
      }
    }
  } finally {
    await SpecialPowers.popPrefEnv();
  }
});
