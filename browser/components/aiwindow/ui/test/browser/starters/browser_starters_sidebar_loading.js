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

  describe("when switching tabs while starter prompts load", () => {
    let firstTab, secondTab;
    afterEach(async () => {
      if (firstTab) {
        await BrowserTestUtils.removeTab(firstTab);
      }

      if (secondTab) {
        await BrowserTestUtils.removeTab(secondTab);
      }
    });

    it("should not load prompts to the wrong tab", async () => {
      // Trigger opening the sidebar so initial starter prompts display
      await navigateTo("https://example.com", gAiWindow);
      firstTab = gAiWindow.gBrowser.selectedTab;

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

      // Open up second tab
      secondTab = await BrowserTestUtils.openNewForegroundTab({
        gBrowser: gAiWindow.gBrowser,
        opening: "https://example.net",
        waitForLoad: true,
      });

      // Start a conversation so that starter prompts should not be displayed
      const sidebarBrowser =
        gAiWindow.document.getElementById("ai-window-browser");
      await typeInSmartbar(sidebarBrowser, "Hello world");
      await submitSmartbar(sidebarBrowser);
      await mock.mockEngineManager.respondTo({
        purpose: "chat",
        response: "Hi",
      });
      await TestUtils.waitForCondition(
        () => !getSidebarPromptButtons(gAiWindow).length,
        "Starter prompts should be hidden after starting a conversation on tab 2"
      );

      // Navigate tab 1 in the background so switching back to it triggers
      // an uncached starter request instead of reusing the starters cached
      // for the initial example.com load.
      const firstTabLoaded = BrowserTestUtils.browserLoaded(
        firstTab.linkedBrowser
      );
      BrowserTestUtils.startLoadingURIString(
        firstTab.linkedBrowser,
        "https://example.org"
      );
      await firstTabLoaded;

      // wait for conversation messages
      await TestUtils.waitForCondition(() => {
        const el = sidebarBrowser?.contentDocument?.querySelector("ai-window");
        return el && el.conversationMessageCount > 0;
      }, "Tab 2's conversation should have messages after submit");

      // Hold the starter prompts mocked response
      const { promise: startersReleased, resolve: releaseStarters } =
        Promise.withResolvers();
      mock.released = startersReleased;
      const requestCountBeforeSwitch = mock.requestCount;

      // Switch back to tab 1 to trigger loading starter prompts, and before
      // they are answered switch back to tab 2 where starter prompts should
      // not display
      await BrowserTestUtils.switchTab(gAiWindow.gBrowser, firstTab);
      await TestUtils.waitForCondition(
        () => mock.requestCount > requestCountBeforeSwitch,
        "Switching to tab 1 should request starter prompts"
      );
      await BrowserTestUtils.switchTab(gAiWindow.gBrowser, secondTab);

      mock.released = null;
      releaseStarters();

      // Wait for conversation to start and starters to hide
      const sidebarBrowser2 =
        gAiWindow.document.getElementById("ai-window-browser");
      await TestUtils.waitForCondition(() => {
        const aiWindowEl =
          sidebarBrowser2?.contentDocument?.querySelector("ai-window");
        return (
          aiWindowEl &&
          aiWindowEl.conversationMessageCount > 0 &&
          !aiWindowEl.showStarters
        );
      }, "Sidebar should have settled on tab 2's conversation with starters hidden");

      // Verify that starter prompts aren't being displayed on tab 2
      Assert.deepEqual(
        getSidebarPromptButtons(gAiWindow),
        [],
        "Starter prompts from tab 1 should not appear on tab 2"
      );
    });
  });

  it("should cancel the latest starter load after an older load finishes", async () => {
    await navigateTo("https://example.com", gAiWindow);
    await TestUtils.waitForCondition(
      () => getSidebarPromptButtons(gAiWindow).includes("prompt 1"),
      "Initial starter prompts should be rendered"
    );

    const sidebarBrowser =
      gAiWindow.document.getElementById("ai-window-browser");
    const aiWindow = sidebarBrowser.contentDocument.querySelector("ai-window");
    const tab = gAiWindow.gBrowser.selectedTab;
    const sandbox = sinon.createSandbox();
    const loads = Array.from({ length: 3 }, () => ({
      entered: Promise.withResolvers(),
      release: Promise.withResolvers(),
    }));
    const generationStarted = Promise.withResolvers();
    const releaseGeneration = Promise.withResolvers();
    const formatValues = aiWindow.ownerDocument.l10n.formatValues.bind(
      aiWindow.ownerDocument.l10n
    );
    const loadSystemPrompt = aiWindow.conversation.loadSystemPrompt.bind(
      aiWindow.conversation
    );
    let loadIndex = 0;
    let olderLoad, newerLoad;

    sandbox
      .stub(aiWindow.ownerDocument.l10n, "formatValues")
      .callsFake(async (...args) => {
        const load = loads[loadIndex++];
        const values = await formatValues(...args);
        load.entered.resolve();
        await load.release.promise;
        return values;
      });
    sandbox
      .stub(aiWindow.conversation, "loadSystemPrompt")
      .callsFake(async (...args) => {
        generationStarted.resolve();
        await releaseGeneration.promise;
        return loadSystemPrompt(...args);
      });

    try {
      aiWindow.conversation.transientStarterUrl = null;
      olderLoad = aiWindow.loadStarterPrompts(true, tab);
      await loads[0].entered.promise;

      aiWindow.conversation.transientStarterUrl = null;
      newerLoad = aiWindow.loadStarterPrompts(true, tab);
      await loads[1].entered.promise;

      loads[0].release.resolve();
      await olderLoad;

      await typeInSmartbar(sidebarBrowser, "Hello world");
      await submitSmartbar(sidebarBrowser);
      await generationStarted.promise;
      Assert.equal(
        aiWindow.conversationMessageCount,
        0,
        "Chat initialization should still be waiting for its system prompt"
      );

      Assert.ok(aiWindow.isGenerating, "Chat generation is in progress");
      loads[2].release.resolve();
      aiWindow.conversation.transientStarterUrl = null;
      await aiWindow.loadStarterPrompts(true, tab);
      Assert.equal(
        loadIndex,
        2,
        "No new starter load begins while chat initialization is in progress"
      );

      loads[1].release.resolve();
      await newerLoad;
      await aiWindow.updateComplete;
      Assert.ok(
        !aiWindow.showStarters,
        "The latest starter load should remain canceled after chat submission"
      );
      Assert.deepEqual(
        getSidebarPromptButtons(gAiWindow),
        [],
        "Canceled starter prompts should not reappear before messages arrive"
      );

      releaseGeneration.resolve();
      await mock.mockEngineManager.respondTo({
        purpose: "chat",
        response: "Hi",
      });
    } finally {
      for (const load of loads) {
        load.release.resolve();
      }
      releaseGeneration.resolve();
      await Promise.allSettled([olderLoad, newerLoad]);
      sandbox.restore();
      await TestUtils.waitForCondition(
        () => !aiWindow.isGenerating,
        "Chat generation should finish before closing the window"
      );
    }
  });
});
