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
    it("should load new prompts when the tab changes URL", async () => {
      await navigateTo("https://example.com", gAiWindow);

      await TestUtils.waitForCondition(
        () => AIWindowUI.isSidebarOpen(gAiWindow),
        "Sidebar should be open"
      );
      await TestUtils.waitForCondition(
        () => getSidebarPromptButtons(gAiWindow).includes("prompt 1"),
        "First set of prompts should be rendered"
      );

      Assert.deepEqual(
        getSidebarPromptButtons(gAiWindow),
        ["prompt 1", "prompt 2"],
        "Should display first set of prompts"
      );

      responseContent[0] = "prompt 3\nprompt 4";

      await navigateTo("https://example.org", gAiWindow);
      await TestUtils.waitForCondition(
        () => getSidebarPromptButtons(gAiWindow).includes("prompt 3"),
        "Second set of prompts should be rendered"
      );
      Assert.deepEqual(
        getSidebarPromptButtons(gAiWindow),
        ["prompt 3", "prompt 4"],
        "Should display updated prompts after URL change"
      );
    });

    it("should not reload prompts when background tabs change URL", async () => {
      const updateNewChatContentSpy = sinon.spy(
        AIWindowUI,
        "updateNewChatContent"
      );

      try {
        await navigateTo("https://example.com", gAiWindow);

        await TestUtils.waitForCondition(
          () => AIWindowUI.isSidebarOpen(gAiWindow),
          "Sidebar should be open"
        );
        await TestUtils.waitForCondition(
          () => getSidebarPromptButtons(gAiWindow).includes("prompt 1"),
          "First set of prompts should be rendered"
        );

        Assert.deepEqual(
          getSidebarPromptButtons(gAiWindow),
          ["prompt 1", "prompt 2"],
          "Should display first set of prompts"
        );

        responseContent[0] = "prompt 3\nprompt 4";
        updateNewChatContentSpy.resetHistory();

        backgroundTab = await openBackgroundTab(
          "https://example.org",
          gAiWindow
        );

        Assert.deepEqual(
          getSidebarPromptButtons(gAiWindow),
          ["prompt 1", "prompt 2"],
          "Should continue to display initial starter prompts after background URL load"
        );
        Assert.equal(
          0,
          updateNewChatContentSpy.callCount,
          "There should not be any more calls to update starter prompts"
        );
      } finally {
        updateNewChatContentSpy.restore();
      }
    });

    it("should load new prompts when navigating back to a previously visited URI", async () => {
      await navigateTo("https://example.com", gAiWindow);

      await TestUtils.waitForCondition(
        () => AIWindowUI.isSidebarOpen(gAiWindow),
        "Sidebar should be open"
      );
      await TestUtils.waitForCondition(
        () => getSidebarPromptButtons(gAiWindow).includes("prompt 1"),
        "First set of prompts should be rendered"
      );

      const requestCountAfterFirstLoad = mock.requestCount;

      responseContent[0] = "prompt 3\nprompt 4";
      await navigateTo("https://example.org", gAiWindow);
      await TestUtils.waitForCondition(
        () => getSidebarPromptButtons(gAiWindow).includes("prompt 3"),
        "Second set of prompts should be rendered"
      );
      Assert.equal(
        mock.requestCount,
        requestCountAfterFirstLoad + 1,
        "Navigating to a new URI should generate a new starter request"
      );

      responseContent[0] = "prompt 5\nprompt 6";
      await navigateTo("https://example.com", gAiWindow);
      await TestUtils.waitForCondition(
        () => getSidebarPromptButtons(gAiWindow).includes("prompt 5"),
        "New prompts should be rendered when navigating back"
      );
      Assert.deepEqual(
        getSidebarPromptButtons(gAiWindow),
        ["prompt 5", "prompt 6"],
        "Should display newly generated prompts when navigating back"
      );
      Assert.equal(
        mock.requestCount,
        requestCountAfterFirstLoad + 2,
        "Navigating back to a previously visited URI should generate a new starter request"
      );
    });
  });
});
