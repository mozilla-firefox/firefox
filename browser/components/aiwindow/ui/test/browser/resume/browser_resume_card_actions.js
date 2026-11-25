/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { ToolTabActions } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/ui/modules/ToolTabActions.sys.mjs"
);

const { MockEngineManager } = ChromeUtils.importESModule(
  "resource://testing-common/AIWindowTestUtils.sys.mjs"
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
        .stub(ToolTabActions, "openAndGroupTabs")
        .resolves({ success: true, group: { id: "group-1" } });

      selectMenuItem(card, journeyId, "open_tabs");

      await TestUtils.waitForCondition(
        () => openAndGroupTabs.called,
        "Wait for ToolTabActions.openAndGroupTabs to be called"
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
        .stub(ToolTabActions, "openOrSwitchToTab")
        .resolves({ success: true, switched: false });
      const openAndGroupTabs = sandbox.stub(ToolTabActions, "openAndGroupTabs");

      selectMenuItem(card, journeyId, "open_tabs");

      await TestUtils.waitForCondition(
        () => openOrSwitchToTab.called,
        "Wait for ToolTabActions.openOrSwitchToTab to be called"
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
      const userMessage = aiWindow.conversation.messages.findLast(
        message => message.role === MESSAGE_ROLE.USER
      );
      const submitEvents = Glean.smartWindow.chatSubmit.testGetValue();
      Assert.equal(
        submitEvents?.length,
        1,
        "Resume card click records a single chat submit event"
      );
      Assert.equal(
        submitEvents[0].extra.submit_type,
        "resume",
        "Resume card click records submit_type resume"
      );
      Assert.equal(
        submitEvents[0].extra.chat_id,
        conversationIdAtClick,
        "Chat submit event carries the clicked conversation id"
      );
      Assert.equal(
        submitEvents[0].extra.length,
        String(userMessage.content.body.length),
        "Chat submit event measures the resume-activity user turn"
      );
      const assistantMessage = aiWindow.conversation.messages.at(-1);
      Assert.deepEqual(
        assistantMessage.toolUIData,
        EXPECTED_RESUME_TOOL_UI_DATA,
        "Should attach a tab-selection confirmation card built from the card's preview tabs"
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

  describe("when a card is resumed", () => {
    it("injects real-time context into the user message", async () => {
      sandbox.stub(openAIEngine, "build").resolves({});
      const fetchWithHistory = sandbox
        .stub(Chat, "fetchWithHistory")
        .resolves();
      const realTimeStub = sandbox
        .stub(ChatConversation.prototype, "injectRealTimeContext")
        .resolves();

      await testResumeActivityClick(sandbox, async ({ resumeCard }) => {
        clickResumeCard(resumeCard);

        await TestUtils.waitForCondition(
          () => fetchWithHistory.calledOnce,
          "Should generate a response for the resume-activity conversation"
        );

        Assert.ok(
          realTimeStub.calledOnce,
          "Should inject real-time context onto the resume-activity user message"
        );
      });
    });

    it("marks the memory that drove it as applied", async () => {
      let mockEngineManager;
      try {
        await testResumeActivityClick(
          sandbox,
          async ({ browser, resumeCard }) => {
            // MockEngineManager stubs getToken itself.
            AuthProvider.getToken.restore();
            mockEngineManager = new MockEngineManager();
            clickResumeCard(resumeCard);
            await mockEngineManager.respondTo({
              purpose: "chat",
              response: "Sure, here's an update.",
            });

            const aichatBrowser = await getAichatBrowser(browser);

            await SpecialPowers.spawn(aichatBrowser, [], async () => {
              const chatRoot = await ContentTaskUtils.waitForCondition(
                () =>
                  content.document.querySelector("ai-chat-content")?.shadowRoot,
                "Wait for chat content shadow root"
              );

              await ContentTaskUtils.waitForMutationCondition(
                chatRoot,
                { childList: true, subtree: true },
                () => chatRoot.querySelector("assistant-message-footer")
              );
              let appliedButton;
              await ContentTaskUtils.waitForCondition(() => {
                const button = chatRoot
                  .querySelector("assistant-message-footer")
                  ?.shadowRoot?.querySelector("applied-memories-button");
                // Unwrap the content element so Lit's prototype-backed
                // properties are visible through SpecialPowers.
                appliedButton = button?.wrappedJSObject ?? button;
                return appliedButton?.appliedMemories?.length;
              }, "Wait for the resume-activity memory to be marked applied");

              Assert.equal(
                appliedButton.appliedMemories.length,
                1,
                "Resume-activity response should have exactly one applied memory"
              );
              Assert.equal(
                appliedButton.appliedMemories[0].memory_summary,
                "Research project",
                "Applied memory should be the one that drove the resume-activity conversation"
              );
            });
          }
        );
      } finally {
        await mockEngineManager?.rejectAllRequests();
        mockEngineManager?.cleanupMocks();
      }
    });

    it("still shows the confirmation card with memories toggled off", async () => {
      const engineBuild = sandbox
        .stub(openAIEngine, "build")
        .resolves({ model: "stub-resume-model" });
      const fetchWithHistory = sandbox
        .stub(Chat, "fetchWithHistory")
        .resolves();
      // The engine's model drives system prompt assembly, so record which
      // model each load ran against.
      const modelsAtPromptLoad = [];
      const loadSystemPrompt = ChatConversation.prototype.loadSystemPrompt;
      sandbox
        .stub(ChatConversation.prototype, "loadSystemPrompt")
        .callsFake(function (...args) {
          modelsAtPromptLoad.push(this.engine?.model ?? null);
          return loadSystemPrompt.apply(this, args);
        });

      await testResumeActivityClick(
        sandbox,
        async ({ aiWindow, resumeCard }) => {
          const memoriesButton = aiWindow.shadowRoot.querySelector(
            "memories-icon-button"
          );
          memoriesButton.dispatchEvent(
            new CustomEvent("aiwindow-memories-toggle:on-change", {
              bubbles: true,
              composed: true,
              detail: { pressed: false },
            })
          );
          await TestUtils.waitForCondition(
            () => memoriesButton.pressed === false,
            "Memories toggle should turn off"
          );

          const conversationIdAtClick = aiWindow.conversationId;
          engineBuild.resetHistory();
          modelsAtPromptLoad.length = 0;
          clickResumeCard(resumeCard);

          await TestUtils.waitForCondition(
            () => fetchWithHistory.calledOnce,
            "Should generate a response for the resume-activity conversation"
          );

          Assert.equal(
            aiWindow.conversationId,
            conversationIdAtClick,
            "Should resume in the conversation the card was clicked in on the memory-free path"
          );
          Assert.equal(
            engineBuild.callCount,
            1,
            "Should build the engine once, in the response path, not in the conversation builder"
          );
          Assert.equal(
            aiWindow.conversation.engine.model,
            "stub-resume-model",
            "Should run the memory-free conversation on the engine the response path built"
          );
          Assert.equal(
            modelsAtPromptLoad.at(-1),
            "stub-resume-model",
            "Should assemble the final system prompt for the built engine's model"
          );
          Assert.deepEqual(
            aiWindow.conversation.messages.at(-1).toolUIData,
            EXPECTED_RESUME_TOOL_UI_DATA,
            "Should still attach a tab-selection confirmation card when memories are toggled off"
          );
          Assert.ok(
            !aiWindow.conversation.messages.at(-2).content.userContext
              ?.resumeActivityContext,
            "Should not bake memory context into the conversation when memories are toggled off"
          );
        }
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
