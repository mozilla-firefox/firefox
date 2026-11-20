/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

let win;

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({ set: RESUME_ACTIVITY_PREFS });
  win = await openSharedAIWindow();
});

describe("Resume section telemetry", () => {
  let sandbox;
  let stubs;

  beforeEach(() => {
    sandbox = sinon.createSandbox();
    Services.fog.testResetFOG();
  });

  afterEach(async () => {
    await stubs?.cleanup();
    stubs = null;
    sandbox.restore();
    _resetResumeSectionHiddenForTesting();
  });

  describe("with three cards", () => {
    let aiWindow;
    let section;

    beforeEach(async () => {
      stubs = await stubResumeActivityGenerationPool(sandbox, 3);
      aiWindow = await openOnlyAIWindowTab(win);
      ({ section } = await waitForResumeCard(aiWindow));
    });

    it("records the collapsed section once", async () => {
      const events = await waitForDisplayed(aiWindow);

      Assert.equal(events.length, 1, "The section is recorded once");
      Assert.deepEqual(
        events[0].extra,
        {
          chat_id: String(aiWindow.conversationId),
          location: "fullpage",
          message_seq: "0",
          state: "cards",
          cards: "2",
          total_cards: "3",
        },
        "The collapsed section shows two of three cards"
      );
    });

    it("records show_more", async () => {
      synthesizeClick(
        section.shadowRoot.querySelector(".resume-section-toggle")
      );

      assertResumeAction(
        "resumeSectionAction",
        aiWindow,
        { action: "show_more" },
        "Show more records show_more"
      );
    });

    it("records show_less", async () => {
      synthesizeClick(
        section.shadowRoot.querySelector(".resume-section-toggle")
      );
      await section.updateComplete;

      synthesizeClick(
        section.shadowRoot.querySelector(".resume-section-toggle")
      );

      assertResumeAction(
        "resumeSectionAction",
        aiWindow,
        { action: "show_less" },
        "Show less records show_less"
      );
    });

    // Removing a card triggers the initial-impression update path.
    it("is not recorded again when a card is removed", async () => {
      await waitForDisplayed(aiWindow);
      const card = section.shadowRoot.querySelector("smartwindow-resume-card");

      // The close button only appears while the card is hovered.
      EventUtils.synthesizeMouseAtCenter(
        card,
        { type: "mousemove" },
        card.documentGlobal
      );
      synthesizeClick(card.shadowRoot.querySelector(".resume-card-dismiss"));

      await TestUtils.waitForCondition(
        () =>
          section.shadowRoot.querySelectorAll("smartwindow-resume-card")
            .length === 2,
        "Wait for the dismissed card to be removed"
      );
      await new Promise(resolve =>
        section.documentGlobal.requestAnimationFrame(resolve)
      );
      Assert.equal(
        pageEvents("resumeCardsDisplayed", aiWindow).length,
        1,
        "Removing cards should not record another displayed event"
      );
    });
  });

  describe("behind a restored chat", () => {
    let aiWindow;
    let section;

    beforeEach(async () => {
      stubs = await stubResumeActivityGeneration(sandbox);
      aiWindow = await openOnlyAIWindowTab(win);
      ({ section } = await waitForResumeCard(aiWindow));
      await waitForDisplayed(aiWindow);
      Services.fog.testResetFOG();
    });

    it("does not record cards that finish loading", async () => {
      // A restored chat hides the section while its cards are still loading.
      aiWindow.classList.add("chat-active");
      aiWindow.resumeCardsLoading = true;
      await aiWindow.updateComplete;
      await section.updateComplete;

      aiWindow.resumeCardsLoading = false;

      await aiWindow.updateComplete;
      await section.updateComplete;
      await new Promise(resolve =>
        section.documentGlobal.requestAnimationFrame(resolve)
      );
      Assert.deepEqual(
        pageEvents("resumeCardsDisplayed", aiWindow),
        [],
        "Cards that finish loading behind a chat are not recorded"
      );
    });
  });

  // The section isn't rendered while loading, so it first appears with
  // loading already done.
  describe("after the section was hidden this session", () => {
    beforeEach(async () => {
      ResumeActivity.hideSectionForSession();
      stubs = await stubResumeActivityGeneration(sandbox);
    });

    it("records cards that arrive", async () => {
      const aiWindow = await openOnlyAIWindowTab(win);
      await waitForResumeCard(aiWindow);

      const events = await waitForDisplayed(aiWindow);

      Assert.equal(events.length, 1, "The section is recorded once");
      Assert.equal(
        events[0].extra.state,
        "cards",
        "Cards that arrive after the section was hidden are recorded"
      );
    });
  });

  describe("with every card dismissed", () => {
    let aiWindow;

    beforeEach(async () => {
      stubs = await stubResumeActivityGeneration(sandbox);
      ResumeActivity.dismissMemory("memory-1");
      aiWindow = await openOnlyAIWindowTab(win);
    });

    it("records the all-dismissed empty state", async () => {
      const [event] = await waitForDisplayed(aiWindow);

      Assert.equal(
        event.extra.state,
        "empty_all_dismissed",
        "The all-dismissed empty state is recorded"
      );
      Assert.equal(event.extra.cards, "0", "No cards are visible");
    });

    it("records hide_section when the empty section is hidden", async () => {
      await waitForDisplayed(aiWindow);
      const section = aiWindow.shadowRoot.querySelector(
        "smartwindow-resume-section"
      );

      synthesizeClick(
        section.shadowRoot.querySelector(".resume-section-empty-hide")
      );

      assertResumeAction(
        "resumeSectionAction",
        aiWindow,
        { action: "hide_section" },
        "Hiding the empty section records hide_section"
      );
    });
  });
});

/**
 * @param {Element} aiWindow
 * @returns {Promise<object[]>} The page's displayed events, once it has one.
 */
function waitForDisplayed(aiWindow) {
  return TestUtils.waitForCondition(() => {
    const events = pageEvents("resumeCardsDisplayed", aiWindow);
    return events.length && events;
  }, "Wait for the resume cards displayed event");
}
