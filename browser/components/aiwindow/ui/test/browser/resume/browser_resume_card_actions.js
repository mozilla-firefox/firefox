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

describe("Resume card actions", () => {
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

  describe("on a card that opens several tabs", () => {
    let section;
    let card;
    let journeyId;

    beforeEach(async () => {
      stubs = await stubResumeActivityGeneration(sandbox);
      // stubResumeActivityGeneration only produces one valid journey.
      [{ id: journeyId }] = stubs.memories;
      ({ section, card } = await waitForResumeCard(
        await openOnlyAIWindowTab(win)
      ));
    });

    it("snoozes it for 24 hours from the menu", async () => {
      const beforeSnooze = Date.now();

      selectMenuItem(card, journeyId, "snooze");

      await waitForNoCards(section, "Wait for the snoozed card to be removed");
      const afterSnooze = Date.now();
      assertAllDismissed(section, "Snoozing the last card");
      const metadata = await JourneyStore.get(journeyId);
      Assert.ok(!metadata.dismissed, "Snoozing should not dismiss permanently");
      Assert.greaterOrEqual(
        metadata.dismissed_until,
        beforeSnooze + DAY_MS,
        "The snooze should expire no earlier than 24 hours after the action started"
      );
      Assert.lessOrEqual(
        metadata.dismissed_until,
        afterSnooze + DAY_MS,
        "The snooze should expire no later than 24 hours after the action completed"
      );
    });

    it("dismisses it permanently from the close button", async () => {
      card.shadowRoot.querySelector(".resume-card-dismiss").click();

      await waitForNoCards(
        section,
        "Wait for the dismissed card to be removed"
      );
      assertAllDismissed(section, "Closing the last card");
      Assert.ok(
        (await JourneyStore.get(journeyId)).dismissed,
        "Closing should permanently dismiss the journey"
      );
    });

    it("opens every preview tab as a group in its window", async () => {
      const openAndGroupTabs = sandbox
        .stub(ToolUI, "openAndGroupTabs")
        .resolves({ success: true, group: { id: "group-1" } });

      selectMenuItem(card, journeyId, "open_tabs");

      await TestUtils.waitForCondition(
        () => openAndGroupTabs.called,
        "Wait for ToolUI.openAndGroupTabs to be called"
      );
      const [{ tabs, window: openWindow }] = openAndGroupTabs.firstCall.args;
      Assert.deepEqual(
        tabs,
        card.content.previewTabs,
        "Should open every preview tab, not just the visible favicons"
      );
      Assert.equal(
        openWindow,
        win,
        "Should open the tabs in the ai-window's chrome window"
      );
    });
  });

  describe("on a card with a single tab", () => {
    let card;
    let journeyId;

    beforeEach(async () => {
      stubs = await stubResumeActivityGenerationPool(sandbox, 1);
      [{ id: journeyId }] = stubs.memories;
      ({ card } = await waitForResumeCard(await openOnlyAIWindowTab(win)));
    });

    it("switches to the tab instead of grouping it", async () => {
      const openOrSwitchToTab = sandbox
        .stub(ToolUI, "openOrSwitchToTab")
        .resolves({ success: true, switched: false });
      const openAndGroupTabs = sandbox.stub(ToolUI, "openAndGroupTabs");

      selectMenuItem(card, journeyId, "open_tabs");

      await TestUtils.waitForCondition(
        () => openOrSwitchToTab.called,
        "Wait for ToolUI.openOrSwitchToTab to be called"
      );
      const [{ tab, window: openWindow }] = openOrSwitchToTab.firstCall.args;
      Assert.deepEqual(
        tab,
        card.content.previewTabs[0],
        "Should switch to the card's single preview tab"
      );
      Assert.equal(
        openWindow,
        win,
        "Should open the tab in the ai-window's chrome window"
      );
      Assert.ok(!openAndGroupTabs.called, "Should not group a single tab");
    });
  });

  describe("when the card body is clicked", () => {
    let aiWindow;
    let card;
    let fetchWithHistory;
    let since;

    beforeEach(async () => {
      sandbox.stub(openAIEngine, "build").resolves({});
      fetchWithHistory = sandbox.stub(Chat, "fetchWithHistory").resolves();
      since = Date.now();
      stubs = await stubResumeActivityGeneration(sandbox);
      aiWindow = await openOnlyAIWindowTab(win);
      ({ card } = await waitForResumeCard(aiWindow));
    });

    // The same resume flow as a pill click.
    it("resumes in the same conversation with a confirmation card", async () => {
      const conversationIdAtClick = aiWindow.conversationId;
      Services.fog.testResetFOG();

      card.shadowRoot.querySelector(".resume-card-title").click();

      assertResumeAction(
        "resumeCardAction",
        aiWindow,
        {
          action: "card_click",
          position: "0",
          rank: "0",
          component_summaries: "0",
        },
        "Clicking the card records card_click",
        // The memory has no created_at, and its newest preview tab was
        // visited 2 minutes before the stub ran.
        { lastVisitAge: 2 * 60_000, since }
      );
      await TestUtils.waitForCondition(
        () => fetchWithHistory.calledOnce,
        "Should generate a response for the resume-activity conversation"
      );
      Assert.equal(
        aiWindow.conversationId,
        conversationIdAtClick,
        "Should resume in the conversation the card was clicked in"
      );
      const assistantMessage = aiWindow.conversation.messages.at(-1);
      Assert.equal(
        assistantMessage.toolUIData?.uiType,
        "tab-group-confirmation",
        "Clicking the card should attach the same confirmation card as a resume pill"
      );
      // fetchWithHistory is stubbed, so stand in for a streamed update.
      aiWindow.conversation.emit(
        "chat-conversation:message-update",
        assistantMessage
      );
      const [prompt] = await TestUtils.waitForCondition(
        () => Glean.smartWindow.browserActionPrompt.testGetValue(),
        "Wait for the confirmation card's prompt event"
      );
      Assert.equal(
        prompt.extra.entrypoint,
        "resume",
        "The confirmation card's prompt records the resume entrypoint"
      );
    });
  });
});

function selectMenuItem(card, journeyId, itemId) {
  card.dispatchEvent(
    new CustomEvent("smartwindow-resume-card:menu-item-selected", {
      detail: { journeyId, itemId },
      bubbles: true,
      composed: true,
    })
  );
}

function waitForNoCards(section, message) {
  return TestUtils.waitForCondition(
    () => !section.shadowRoot.querySelector("smartwindow-resume-card"),
    message
  );
}

/**
 * @param {Element} section
 * @param {string} action - What removed the last card.
 */
function assertAllDismissed(section, action) {
  Assert.equal(
    section.shadowRoot.querySelector(".resume-section-empty-heading")?.dataset
      .l10nId,
    "aiwindow-resume-section-empty-all-dismissed-heading",
    `${action} should show the all-dismissed empty state`
  );
}
