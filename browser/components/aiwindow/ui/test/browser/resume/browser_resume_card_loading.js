/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

let win;

add_setup(async function () {
  win = await openSharedAIWindow();
});

describe("Resume card loading", () => {
  let sandbox;
  let stubs;

  beforeEach(() => {
    sandbox = sinon.createSandbox();
  });

  afterEach(async () => {
    await stubs?.cleanup();
    stubs = null;
    sandbox.restore();
  });

  describe("when resume activity is enabled", () => {
    let extraTab;

    beforeEach(async () => {
      await SpecialPowers.pushPrefEnv({ set: RESUME_ACTIVITY_PREFS });
    });

    afterEach(async () => {
      if (extraTab) {
        BrowserTestUtils.removeTab(extraTab);
        extraTab = null;
      }
      await SpecialPowers.popPrefEnv();
    });

    it("omits entries with an empty headline and keeps the tab payload", async () => {
      stubs = await stubResumeActivityGeneration(sandbox);
      await openOnlyAIWindowTab(win);

      const aiWindow = await waitForResumeCards(win.gBrowser.selectedBrowser);

      Assert.deepEqual(
        aiWindow.resumeCards.map(({ memory }) => memory.id),
        ["memory-1"],
        "Entries with empty generated headlines should be omitted"
      );
      Assert.equal(
        aiWindow.resumeCards[0].content.previewTabs.length,
        4,
        "The resume card should retain its tab payload"
      );
      Assert.equal(
        getResumeCardElements(aiWindow).length,
        1,
        "The resume card should render"
      );
    });

    it("does not reload after a new chat", async () => {
      stubs = await stubResumeActivityGeneration(sandbox);
      await openOnlyAIWindowTab(win);
      const aiWindow = await waitForResumeCards(win.gBrowser.selectedBrowser);

      stubs.suggestSpy.resetHistory();
      aiWindow.onCreateNewChatClick();

      await TestUtils.waitForCondition(
        () =>
          aiWindow.shadowRoot.querySelector("smartwindow-prompts")?.prompts
            .length,
        "Starters should reload after New Chat"
      );
      Assert.ok(
        stubs.suggestSpy.notCalled,
        "New Chat should not re-trigger resume-activity generation"
      );
    });

    it("gives each new tab its own fresh opportunity", async () => {
      stubs = await stubResumeActivityGeneration(sandbox);
      await openOnlyAIWindowTab(win);
      await waitForResumeCards(win.gBrowser.selectedBrowser);

      extraTab = await BrowserTestUtils.openNewForegroundTab({
        gBrowser: win.gBrowser,
        opening: AIWINDOW_URL,
        waitForLoad: true,
      });
      const aiWindow2 = await waitForResumeCards(extraTab.linkedBrowser);

      Assert.ok(
        aiWindow2.resumeCards.length,
        "A newly opened tab should get its own fresh resume cards opportunity"
      );
    });

    it("displays the top 4 of the pool and reveals the next on dismiss", async () => {
      stubs = await stubResumeActivityGenerationPool(sandbox, 5);
      await openOnlyAIWindowTab(win);
      const aiWindow = await waitForResumeCards(win.gBrowser.selectedBrowser);

      const resumeIds = window => window.resumeCards.map(c => c.memory.id);
      Assert.deepEqual(
        resumeIds(aiWindow),
        ["pool-memory-0", "pool-memory-1", "pool-memory-2", "pool-memory-3"],
        "Only the top 4 of the 5-candidate pool should display"
      );

      const [firstCard] = getResumeCardElements(aiWindow);
      // The close button only appears while the card is hovered.
      EventUtils.synthesizeMouseAtCenter(
        firstCard,
        { type: "mousemove" },
        firstCard.documentGlobal
      );
      synthesizeClick(
        firstCard.shadowRoot.querySelector(".resume-card-dismiss")
      );

      await TestUtils.waitForCondition(
        () => !resumeIds(aiWindow).includes("pool-memory-0"),
        "Dismissed candidate should be removed from the current tab"
      );

      extraTab = await BrowserTestUtils.openNewForegroundTab({
        gBrowser: win.gBrowser,
        opening: AIWINDOW_URL,
        waitForLoad: true,
      });
      const aiWindow2 = await waitForResumeCards(extraTab.linkedBrowser);

      Assert.deepEqual(
        resumeIds(aiWindow2),
        ["pool-memory-1", "pool-memory-2", "pool-memory-3", "pool-memory-4"],
        "A new tab should surface the pool's 5th candidate in the dismissed slot"
      );
    });

    it("shows skeleton cards while the lookup is slow", async () => {
      let resolveLookup;
      sandbox
        .stub(MemoriesManager, "getAllMemories")
        .resolves([{ id: "memory-1", memory_summary: "Test" }]);
      sandbox.stub(JourneyManager, "suggestJourneysToResume").returns(
        new Promise(resolve => {
          resolveLookup = resolve;
        })
      );

      const aiWindow = await openOnlyAIWindowTab(win);

      await TestUtils.waitForCondition(
        () => aiWindow.resumeCardsLoading,
        "Wait for the loading state once the lookup is slow"
      );
      const section = await TestUtils.waitForCondition(
        () => aiWindow.shadowRoot.querySelector("smartwindow-resume-section"),
        "Wait for smartwindow-resume-section element"
      );
      await TestUtils.waitForCondition(
        () => section.shadowRoot.querySelector(".resume-card-skeleton"),
        "Wait for the skeleton cards"
      );

      resolveLookup([
        {
          memory: { id: "memory-1" },
          content: {
            headline: "Pick up your research",
            status: "Continue reading",
            previewTabs: [{ url: "https://example.com/1", title: "Example 1" }],
          },
        },
      ]);
      await TestUtils.waitForCondition(
        () => !aiWindow.resumeCardsLoading && aiWindow.resumeCards.length,
        "Wait for the lookup to resolve into cards"
      );
      await section.updateComplete;

      Assert.equal(
        section.shadowRoot.querySelector(".resume-card-skeleton"),
        null,
        "Skeleton cards should be gone once the lookup resolves"
      );
      Assert.equal(
        section.shadowRoot.querySelectorAll("smartwindow-resume-card").length,
        1,
        "The resolved card should render"
      );
    });
  });

  describe("when generation is gated off", () => {
    afterEach(async () => {
      await SpecialPowers.popPrefEnv();
    });

    it("does not load with both memories prefs off", async () => {
      await SpecialPowers.pushPrefEnv({
        set: [
          ["browser.smartwindow.memories.generateFromConversation", false],
          ["browser.smartwindow.memories.generateFromHistory", false],
        ],
      });
      // Existing memories should not override disabled generation prefs.
      sandbox
        .stub(MemoriesManager, "getAllMemories")
        .resolves([{ id: "memory-1", memory_summary: "Test" }]);
      const suggestSpy = sandbox.spy(JourneyManager, "suggestJourneysToResume");

      const aiWindow = await openOnlyAIWindowTab(win);
      // Wait for the starters to render. The stubbed memories resolve
      // immediately, so the resume gate should have settled by then.
      await getPromptButtons(win.gBrowser.selectedBrowser);

      Assert.deepEqual(
        aiWindow.resumeCards,
        [],
        "Resume cards should not load with both memories prefs off"
      );
      Assert.ok(
        suggestSpy.notCalled,
        "Resume card generation should not run with both memories prefs off"
      );
    });

    it("does not load when the feature pref is off", async () => {
      await SpecialPowers.pushPrefEnv({
        set: [
          ...RESUME_ACTIVITY_PREFS,
          ["browser.smartwindow.resumeActivity.enabled", false],
        ],
      });
      stubs = await stubResumeActivityGeneration(sandbox);

      const aiWindow = await openOnlyAIWindowTab(win);
      // Wait for the starters to render. loadResumeCards waits on the same
      // smartbar-ready promise and returns right away with the feature
      // disabled.
      await getPromptButtons(win.gBrowser.selectedBrowser);

      Assert.ok(
        stubs.suggestSpy.notCalled,
        "Resume generation should not run when the feature is disabled"
      );
      Assert.deepEqual(aiWindow.resumeCards, [], "No resume cards should load");
      Assert.equal(
        aiWindow.shadowRoot
          .querySelector("smartwindow-resume-section")
          .shadowRoot.querySelector(".resume-section-grid"),
        null,
        "The resume section should render nothing"
      );
    });

    it("does not load without existing memories", async () => {
      // Enabled prefs cannot generate resume cards without memories.
      await SpecialPowers.pushPrefEnv({ set: RESUME_ACTIVITY_PREFS });
      sandbox.stub(MemoriesManager, "getAllMemories").resolves([]);
      const suggestSpy = sandbox.spy(JourneyManager, "suggestJourneysToResume");

      const aiWindow = await openOnlyAIWindowTab(win);
      // Wait for the starters to render. The stubbed memories resolve
      // immediately, so the resume gate should have settled by then.
      await getPromptButtons(win.gBrowser.selectedBrowser);

      Assert.deepEqual(
        aiWindow.resumeCards,
        [],
        "Resume cards should not load with no existing memories, even with prefs on"
      );
      Assert.ok(
        suggestSpy.notCalled,
        "Resume card generation should not run with no existing memories"
      );
    });
  });
});
