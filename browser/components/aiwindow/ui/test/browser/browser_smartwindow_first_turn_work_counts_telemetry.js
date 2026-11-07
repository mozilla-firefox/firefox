/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { MockEngineManager } = ChromeUtils.importESModule(
  "resource://testing-common/AIWindowTestUtils.sys.mjs"
);

/**
 * This test case keeps incomplete usage out of token totals, preserves genuine
 * zero counts, and distinguishes execution failures from rejected tool calls.
 */
add_task(async function test_first_turn_work_counts() {
  for (const {
    initialTokens,
    finalTokens,
    toolName,
    args,
    expectedToolCalls,
  } of [
    { initialTokens: undefined, finalTokens: 7 },
    { initialTokens: 3, finalTokens: undefined },
    { initialTokens: 0, finalTokens: 0 },
    { initialTokens: 3, finalTokens: 7, toolName: "unknown_tool" },
    { initialTokens: 3, finalTokens: 7, args: "{", expectedToolCalls: 0 },
  ]) {
    const engineManager = new MockEngineManager();
    let win;

    try {
      win = await openAIWindow();
      const browser = win.gBrowser.selectedBrowser;
      Services.fog.testResetFOG();

      info(
        `First-turn work: initial=${initialTokens}, final=${finalTokens}, tool=${toolName ?? "get_open_tabs"}, args=${args ?? "{}"}`
      );
      await typeInSmartbar(browser, "check my tabs");
      await submitSmartbar(browser);
      await engineManager.respondTo({
        purpose: "chat",
        response: {
          text: "",
          usage:
            initialTokens === undefined
              ? undefined
              : { completion_tokens: initialTokens },
          toolCalls: [
            {
              id: "work-count-tool",
              type: "function",
              function: {
                name: toolName ?? "get_open_tabs",
                arguments: args ?? "{}",
              },
            },
          ],
        },
      });
      const pending = await engineManager.captureRequest({ purpose: "chat" });
      pending.respond({
        text: "Done.",
        usage:
          finalTokens === undefined
            ? undefined
            : { completion_tokens: finalTokens },
      });
      await TestUtils.waitForCondition(
        () => Glean.smartWindow.modelResponse.testGetValue()?.length === 1,
        "The turn completes after the tool result"
      );

      const [event] = Glean.smartWindow.timeToTurnComplete.testGetValue();
      Assert.equal(
        Number(event.extra.tool_call_count),
        expectedToolCalls ?? 1,
        "Executed tools count even when they fail; rejected calls do not"
      );
      if (initialTokens === undefined || finalTokens === undefined) {
        Assert.ok(
          !("output_tokens" in event.extra),
          "Missing usage in any chat model response omits the token total"
        );
      } else {
        Assert.equal(
          Number(event.extra.output_tokens),
          initialTokens + finalTokens,
          "Complete usage is summed, including a genuine zero total"
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
});
