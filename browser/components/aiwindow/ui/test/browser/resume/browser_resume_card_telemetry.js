/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { ToolUI } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/ui/modules/ToolUI.sys.mjs"
);

let win;

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({ set: RESUME_ACTIVITY_PREFS });
  win = await openSharedAIWindow();
});

describe("Resume card telemetry", () => {
  let sandbox;
  let stubs;
  let aiWindow;
  let section;
  let since;

  beforeEach(() => {
    sandbox = sinon.createSandbox();
    Services.fog.testResetFOG();
    since = Date.now();
  });

  afterEach(async () => {
    await stubs?.cleanup();
    stubs = null;
    sandbox.restore();
  });

  describe("actions", () => {
    beforeEach(async () => {
      stubs = await stubResumeActivityGenerationPool(sandbox, 3);
      aiWindow = await openOnlyAIWindowTab(win);
      ({ section } = await waitForResumeCard(aiWindow));
      await TestUtils.waitForCondition(
        () => pageEvents("resumeCardsDisplayed", aiWindow).length,
        "Wait for the initial resume cards displayed event"
      );
    });

    it("records open_tabs with the card's position", async () => {
      sandbox.stub(ToolUI, "openOrGroupTabs").resolves();

      await clickResumeCardMenuItem(
        cardAt(section, 1),
        "aiwindow-resume-card-open-tabs"
      );

      assertResumeAction(
        "resumeCardAction",
        aiWindow,
        { action: "open_tabs", position: "1", ...poolCardFeatures(1) },
        "Open tabs records the card's position",
        poolCardAges(1, 3, since)
      );
    });

    it("records snooze with the card's position", async () => {
      await clickResumeCardMenuItem(
        cardAt(section, 1),
        "aiwindow-resume-card-snooze"
      );

      await waitForCardCount(
        section,
        2,
        "Wait for the snoozed card to be removed"
      );
      assertResumeAction(
        "resumeCardAction",
        aiWindow,
        { action: "snooze", position: "1", ...poolCardFeatures(1) },
        "Snooze records the card's position",
        poolCardAges(1, 3, since)
      );
    });

    it("records dismiss from the close button", async () => {
      dismissWithCloseButton(cardAt(section, 0));

      await waitForCardCount(
        section,
        2,
        "Wait for the dismissed card to be removed"
      );
      assertResumeAction(
        "resumeCardAction",
        aiWindow,
        { action: "dismiss", position: "0", ...poolCardFeatures(0) },
        "The close button records dismiss",
        poolCardAges(0, 3, since)
      );
    });
  });

  describe("Resume button", () => {
    let fetchWithHistory;

    beforeEach(async () => {
      sandbox.stub(openAIEngine, "build").resolves({});
      fetchWithHistory = sandbox.stub(Chat, "fetchWithHistory").resolves();
      stubs = await stubResumeActivityGenerationPool(sandbox, 3);
      aiWindow = await openOnlyAIWindowTab(win);
      ({ section } = await waitForResumeCard(aiWindow));
      Services.fog.testResetFOG();
    });

    afterEach(async () => {
      await TestUtils.waitForCondition(
        () => fetchWithHistory.calledOnce,
        "Wait for the resume flow to finish before the next test"
      );
    });

    it("records a single resume", () => {
      synthesizeClick(
        cardAt(section, 1).shadowRoot.querySelector(
          ".resume-card-resume-button"
        )
      );

      assertResumeAction(
        "resumeCardAction",
        aiWindow,
        { action: "resume", position: "1", ...poolCardFeatures(1) },
        "The Resume button records resume",
        poolCardAges(1, 3, since)
      );
      Assert.equal(
        pageEvents("resumeCardAction", aiWindow).length,
        1,
        "The Resume button records a single action"
      );
    });
  });

  describe("impressions with four cards", () => {
    let context;

    beforeEach(async () => {
      stubs = await stubResumeActivityGenerationPool(sandbox, 4);
      aiWindow = await openOnlyAIWindowTab(win);
      ({ section } = await waitForResumeCard(aiWindow));
      context = {
        chat_id: String(aiWindow.conversationId),
        location: "fullpage",
        message_seq: "0",
      };
    });

    it("are recorded for the two collapsed cards at load", async () => {
      const events = await waitForImpressions(aiWindow, 2);

      Assert.deepEqual(
        events.map(({ extra }, rank) =>
          checkAges(extra, poolCardAges(rank, 4, since))
        ),
        [
          { ...context, position: "0", ...poolCardFeatures(0) },
          { ...context, position: "1", ...poolCardFeatures(1) },
        ],
        "The two collapsed cards are recorded at load"
      );
    });

    it("are recorded for a card that moves into view after a dismiss", async () => {
      await waitForImpressions(aiWindow, 2);

      dismissWithCloseButton(cardAt(section, 0));

      const events = await waitForImpressions(aiWindow, 3);
      Assert.deepEqual(
        checkAges(events[2].extra, poolCardAges(2, 4, since)),
        { ...context, position: "1", ...poolCardFeatures(2) },
        "A card that moves into view after a dismiss is recorded"
      );
    });

    it("are recorded for the cards Show more reveals", async () => {
      await waitForImpressions(aiWindow, 2);

      synthesizeClick(
        section.shadowRoot.querySelector(".resume-section-toggle")
      );

      const events = await waitForImpressions(aiWindow, 4);
      Assert.deepEqual(
        events
          .slice(2)
          .map(({ extra }, i) =>
            checkAges(extra, poolCardAges(i + 2, 4, since))
          ),
        [
          { ...context, position: "2", ...poolCardFeatures(2) },
          { ...context, position: "3", ...poolCardFeatures(3) },
        ],
        "Show more records the newly visible cards"
      );
    });

    // Every card is already recorded once the section has been expanded.
    it("are not recorded again when the section collapses and expands", async () => {
      await waitForImpressions(aiWindow, 2);
      synthesizeClick(
        section.shadowRoot.querySelector(".resume-section-toggle")
      );
      await waitForImpressions(aiWindow, 4);
      await section.updateComplete;

      synthesizeClick(
        section.shadowRoot.querySelector(".resume-section-toggle")
      );
      await section.updateComplete;
      synthesizeClick(
        section.shadowRoot.querySelector(".resume-section-toggle")
      );
      await section.updateComplete;

      Assert.equal(
        pageEvents("resumeCardImpression", aiWindow).length,
        4,
        "Collapsing and re-expanding does not record duplicate impressions"
      );
    });

    it("keep a card's rank after another card is removed", async () => {
      await waitForImpressions(aiWindow, 2);
      dismissWithCloseButton(cardAt(section, 0));
      await waitForImpressions(aiWindow, 3);

      await clickResumeCardMenuItem(
        cardAt(section, 1),
        "aiwindow-resume-card-snooze"
      );

      assertResumeAction(
        "resumeCardAction",
        aiWindow,
        { action: "snooze", position: "1", ...poolCardFeatures(2) },
        "rank stays fixed after another card is removed",
        poolCardAges(2, 4, since)
      );
    });
  });
});

function cardAt(section, index) {
  return section.shadowRoot.querySelectorAll("smartwindow-resume-card")[index];
}

function waitForCardCount(section, count, message) {
  return TestUtils.waitForCondition(
    () =>
      section.shadowRoot.querySelectorAll("smartwindow-resume-card").length ===
      count,
    message
  );
}

/**
 * Hovers a card so its close button appears, then clicks it.
 *
 * @param {Element} card
 */
function dismissWithCloseButton(card) {
  EventUtils.synthesizeMouseAtCenter(
    card,
    { type: "mousemove" },
    card.documentGlobal
  );
  synthesizeClick(card.shadowRoot.querySelector(".resume-card-dismiss"));
}

function waitForImpressions(aiWindow, count) {
  return TestUtils.waitForCondition(() => {
    const events = pageEvents("resumeCardImpression", aiWindow);
    return events.length === count && events;
  }, `Wait for ${count} card impressions`);
}

/**
 * Opens a resume card's "More" menu and clicks one of its items.
 *
 * @param {Element} card - The smartwindow-resume-card element
 * @param {string} l10nId - The menu item's data-l10n-id
 */
async function clickResumeCardMenuItem(card, l10nId) {
  const menu = card.shadowRoot.querySelector("panel-list");
  const shown = BrowserTestUtils.waitForEvent(menu, "shown");
  synthesizeClick(card.shadowRoot.querySelector(".resume-card-more-button"));
  await shown;
  synthesizeClick(menu.querySelector(`panel-item[data-l10n-id="${l10nId}"]`));
}

/**
 * Returns the suggestion extras, other than the ages, recorded for a
 * stubResumeActivityGenerationPool card.
 *
 * @param {number} rank - The card's load-time index
 * @returns {object}
 */
function poolCardFeatures(rank) {
  return {
    rank: String(rank),
    memory_strength: String(rank * 4.25),
    component_summaries: String(rank),
  };
}

/**
 * Returns the checkAges ages for a stubResumeActivityGenerationPool card.
 * Memory `rank` is `rank` days old, and its only preview tab was visited
 * `memoryCount - rank` minutes before the stub ran.
 *
 * @param {number} rank - The card's load-time index
 * @param {number} memoryCount - The memoryCount passed to the stub
 * @param {number} since - A Date.now() value taken before the stub ran
 * @returns {object}
 */
function poolCardAges(rank, memoryCount, since) {
  return {
    memoryAge: rank * DAY_MS,
    lastVisitAge: (memoryCount - rank) * 60_000,
    since,
  };
}
