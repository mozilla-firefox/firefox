/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

add_setup(async function () {
  await pushStarterPrefs();
});

describe("sidebar conversation starter prompts", () => {
  let responseContent, mock, gAiWindow, backgroundTab;

  beforeEach(async () => {
    responseContent = ["prompt 1\nprompt 2"];
    mock = answerStarterRequests(responseContent);

    gAiWindow = await openAIWindow();
  });

  afterEach(async () => {
    if (backgroundTab) {
      BrowserTestUtils.removeTab(backgroundTab);
    }

    await BrowserTestUtils.closeWindow(gAiWindow);
    await mock.mockEngineManager.rejectAllRequests();
    mock.mockEngineManager.cleanupMocks();

    gAiWindow = null;
  });

  describe("when the conversation is empty", () => {
    it("should evict the oldest cached prompts after exceeding the cache limit", async () => {
      for (let i = 0; i <= 20; i++) {
        responseContent[0] = `prompt ${i}a\nprompt ${i}b`;
        await navigateTo(`https://example.com/${i}`, gAiWindow);
        await TestUtils.waitForCondition(
          () => getSidebarPromptButtons(gAiWindow).includes(`prompt ${i}a`),
          `Prompts for URI ${i} should be rendered`
        );
      }

      const requestCountAfterFillingCache = mock.requestCount;

      responseContent[0] = "prompt evicted a\nprompt evicted b";
      await navigateTo("https://example.com/0", gAiWindow);
      await TestUtils.waitForCondition(
        () => getSidebarPromptButtons(gAiWindow).includes("prompt evicted a"),
        "Evicted prompts should be regenerated for the oldest URI"
      );

      Assert.equal(
        mock.requestCount,
        requestCountAfterFillingCache + 1,
        "Revisiting the oldest URI after the cache limit should generate a new starter request"
      );
    });
  });
});
