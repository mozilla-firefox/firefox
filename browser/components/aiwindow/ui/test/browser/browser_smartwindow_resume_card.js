/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { ToolUI } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/ui/modules/ToolUI.sys.mjs"
);

// Opening an AI Window records usage timestamps at runtime.
registerCleanupFunction(() => {
  for (const pref of [
    "browser.smartwindow.lastSmartWindowUsageTime",
    "browser.smartwindow.lastLLMTelemetryRunTime",
  ]) {
    Services.prefs.clearUserPref(pref);
  }
});

const CARD_MODULE_URL =
  "chrome://browser/content/aiwindow/components/smartwindow-resume-card.mjs";

const SAMPLE_CONTENT = {
  headline: "Team offsite planning",
  status: "Reviewed venue quotes and the calendar poll for next week.",
  previewTabs: [{ url: "https://a.example/" }, { url: "https://b.example/" }],
};

const RESUME_ACTIVITY_PREFS = [
  ["browser.smartwindow.memories.generateFromConversation", true],
  ["browser.smartwindow.memories.generateFromHistory", true],
  ["browser.smartwindow.resumeCards.enabled", true],
];
const DAY_MS = 24 * 60 * 60 * 1000;

async function ensureCardDefined(doc) {
  if (doc.defaultView.customElements.get("smartwindow-resume-card")) {
    return;
  }
  const script = doc.createElement("script");
  script.type = "module";
  script.src = CARD_MODULE_URL;
  doc.head.appendChild(script);
  await doc.defaultView.customElements.whenDefined("smartwindow-resume-card");
  script.remove();
}

async function createResumeCard(doc, { content, journeyId = "memory-1" }) {
  await ensureCardDefined(doc);
  const el = doc.createElement("smartwindow-resume-card");
  el.content = content;
  el.journeyId = journeyId;
  doc.body.appendChild(el);
  await el.updateComplete;
  return el;
}

/**
 * Runs a test callback with a generated, rendered resume card.
 *
 * @param {Function} stub - Resume-activity generation stub
 * @param {Function} run - Receives {win, aiWindow, section, card, memories}
 */
async function withRenderedResumeCard(stub, run) {
  await SpecialPowers.pushPrefEnv({ set: RESUME_ACTIVITY_PREFS });

  const { memories, cleanup } = await stub();
  let win;
  try {
    win = await openAIWindow();
    const browser = win.gBrowser.selectedBrowser;
    const aiWindow = await TestUtils.waitForCondition(
      () => browser.contentDocument?.querySelector("ai-window"),
      "Wait for ai-window element"
    );
    const section = await TestUtils.waitForCondition(
      () => aiWindow.shadowRoot.querySelector("smartwindow-resume-section"),
      "Wait for smartwindow-resume-section element"
    );
    const card = await TestUtils.waitForCondition(
      () => section.shadowRoot.querySelector("smartwindow-resume-card"),
      "Wait for a resume card to render"
    );
    await run({ win, aiWindow, section, card, memories });
  } finally {
    if (win) {
      await BrowserTestUtils.closeWindow(win);
    }
    await cleanup();
  }
}

add_task(async function test_resume_card_rendering() {
  const win = await openAIWindow();
  try {
    const doc = win.gBrowser.selectedBrowser.contentDocument;

    const el = await createResumeCard(doc, { content: SAMPLE_CONTENT });
    const shadow = el.shadowRoot;

    Assert.equal(
      shadow.querySelector(".resume-card-title").textContent,
      SAMPLE_CONTENT.headline,
      "Title should match content.headline"
    );
    Assert.equal(
      shadow.querySelector(".resume-card-description").textContent,
      SAMPLE_CONTENT.status,
      "Description should match content.status"
    );
    Assert.equal(
      shadow.querySelectorAll(".resume-card-favicon").length,
      SAMPLE_CONTENT.previewTabs.length,
      "Should render one favicon per preview tab when under the visible max"
    );
    el.remove();

    const manyTabsEl = await createResumeCard(doc, {
      content: {
        ...SAMPLE_CONTENT,
        previewTabs: Array.from({ length: 5 }, (_, i) => ({
          url: `https://site${i}.example/`,
        })),
      },
    });
    Assert.equal(
      manyTabsEl.shadowRoot.querySelectorAll(".resume-card-favicon").length,
      3,
      "Should cap at MAX_VISIBLE_FAVICONS favicons regardless of tab count"
    );
    manyTabsEl.remove();
  } finally {
    await BrowserTestUtils.closeWindow(win);
  }
});

add_task(async function test_resume_card_events() {
  const win = await openAIWindow();
  try {
    const doc = win.gBrowser.selectedBrowser.contentDocument;
    const el = await createResumeCard(doc, {
      content: SAMPLE_CONTENT,
      journeyId: "memory-42",
    });
    const shadow = el.shadowRoot;

    const resumed = new Promise(resolve =>
      el.addEventListener(
        "smartwindow-resume-card:resume",
        e => resolve(e.detail),
        { once: true }
      )
    );
    shadow.querySelector(".resume-card-resume-button").click();
    Assert.deepEqual(
      await resumed,
      { journeyId: "memory-42", source: "resume_button" },
      "Resume button should dispatch the card's journeyId"
    );

    const dismissed = new Promise(resolve =>
      el.addEventListener(
        "smartwindow-resume-card:dismiss",
        e => resolve(e.detail),
        { once: true }
      )
    );
    shadow.querySelector(".resume-card-dismiss").click();
    Assert.deepEqual(
      await dismissed,
      { journeyId: "memory-42" },
      "Dismiss button should dispatch the card's journeyId"
    );

    // Open normally so panel-list registers its close-on-select listener.
    const panelList = shadow.querySelector("panel-list");
    const shown = BrowserTestUtils.waitForEvent(panelList, "shown");
    shadow.querySelector(".resume-card-more-button").click();
    await shown;

    // Each menu item must provide its own ID; native click detail has none.
    const menuSelected = new Promise(resolve =>
      el.addEventListener(
        "smartwindow-resume-card:menu-item-selected",
        e => resolve(e.detail),
        { once: true }
      )
    );
    const hidden = BrowserTestUtils.waitForEvent(panelList, "hidden");
    shadow.querySelectorAll("panel-item")[0].click();
    Assert.deepEqual(
      await menuSelected,
      { journeyId: "memory-42", itemId: "open_tabs" },
      "Clicking the first menu item should dispatch its itemId with the card's journeyId"
    );
    await hidden;

    el.remove();
  } finally {
    await BrowserTestUtils.closeWindow(win);
  }
});

add_task(async function test_resume_card_whole_card_click_resumes() {
  const win = await openAIWindow();
  try {
    const doc = win.gBrowser.selectedBrowser.contentDocument;
    const el = await createResumeCard(doc, {
      content: SAMPLE_CONTENT,
      journeyId: "memory-42",
    });
    const shadow = el.shadowRoot;

    let resumeCount = 0;
    let lastSource;
    el.addEventListener("smartwindow-resume-card:resume", e => {
      resumeCount++;
      lastSource = e.detail.source;
    });

    shadow.querySelector(".resume-card-title").click();
    Assert.equal(
      resumeCount,
      1,
      "Clicking anywhere on the card body should resume it"
    );
    Assert.equal(lastSource, "card", "A card body click has source card");

    // Dismiss, the More button, and its menu items must not also resume the
    // card - each stops propagation so only their own event fires.
    shadow.querySelector(".resume-card-dismiss").click();
    shadow.querySelector(".resume-card-more-button").click();
    shadow.querySelectorAll("panel-item")[0].click();
    Assert.equal(
      resumeCount,
      1,
      "Dismiss, More, and menu item clicks should not also dispatch resume"
    );

    el.remove();
  } finally {
    await BrowserTestUtils.closeWindow(win);
  }
});

add_task(async function test_resume_card_click_ignores_text_selection() {
  const win = await openAIWindow();
  try {
    const doc = win.gBrowser.selectedBrowser.contentDocument;
    const el = await createResumeCard(doc, {
      content: SAMPLE_CONTENT,
      journeyId: "memory-42",
    });
    const shadow = el.shadowRoot;
    const title = shadow.querySelector(".resume-card-title");

    let resumeCount = 0;
    el.addEventListener("smartwindow-resume-card:resume", () => {
      resumeCount++;
    });

    const selection = doc.getSelection();
    const range = doc.createRange();
    range.selectNodeContents(title);
    selection.removeAllRanges();
    selection.addRange(range);

    // A pointer-style click should not resume while text is selected.
    // "click" must use PointerEvent here, or trusted dispatch crashes debug builds.
    title.dispatchEvent(
      new PointerEvent("click", { bubbles: true, composed: true, detail: 1 })
    );
    Assert.equal(
      resumeCount,
      0,
      "A real click shouldn't resume while releasing a text selection"
    );

    selection.collapseToStart();
    title.dispatchEvent(
      new PointerEvent("click", { bubbles: true, composed: true, detail: 1 })
    );
    Assert.equal(
      resumeCount,
      1,
      "A real click should resume once the selection is collapsed"
    );

    // Keyboard activation (detail 0) should still resume with an active
    // selection.
    range.selectNodeContents(title);
    selection.removeAllRanges();
    selection.addRange(range);
    title.click();
    Assert.equal(
      resumeCount,
      2,
      "Keyboard-style activation should resume regardless of selection"
    );

    selection.removeAllRanges();
    el.remove();
  } finally {
    await BrowserTestUtils.closeWindow(win);
  }
});

add_task(async function test_resume_card_click_closing_menu_does_not_resume() {
  const win = await openAIWindow();
  try {
    const doc = win.gBrowser.selectedBrowser.contentDocument;
    const el = await createResumeCard(doc, {
      content: SAMPLE_CONTENT,
      journeyId: "memory-42",
    });
    const shadow = el.shadowRoot;
    const title = shadow.querySelector(".resume-card-title");
    const panelList = shadow.querySelector("panel-list");

    let resumeCount = 0;
    el.addEventListener("smartwindow-resume-card:resume", () => {
      resumeCount++;
    });

    const shown = BrowserTestUtils.waitForEvent(panelList, "shown");
    shadow.querySelector(".resume-card-more-button").click();
    await shown;

    // panel-list closes on mousedown before the card receives click.
    const hidden = BrowserTestUtils.waitForEvent(panelList, "hidden");
    title.dispatchEvent(
      new PointerEvent("pointerdown", { bubbles: true, composed: true })
    );
    title.dispatchEvent(
      new MouseEvent("mousedown", { bubbles: true, composed: true })
    );
    title.dispatchEvent(
      new PointerEvent("pointerup", { bubbles: true, composed: true })
    );
    title.dispatchEvent(
      new MouseEvent("mouseup", { bubbles: true, composed: true })
    );
    title.dispatchEvent(
      new PointerEvent("click", { bubbles: true, composed: true, detail: 1 })
    );
    await hidden;

    Assert.equal(
      resumeCount,
      0,
      "Closing the more menu by clicking elsewhere on the card should not also resume it"
    );

    title.click();
    Assert.equal(
      resumeCount,
      1,
      "A later click with the menu already closed should resume normally"
    );

    el.remove();
  } finally {
    await BrowserTestUtils.closeWindow(win);
  }
});

add_task(async function test_resume_card_snoozes_for_24_hours() {
  const sb = sinon.createSandbox();
  try {
    await withRenderedResumeCard(
      () => stubResumeActivityGeneration(sb),
      async ({ section, card, memories }) => {
        // stubResumeActivityGeneration only produces one valid journey.
        const [{ id: journeyId }] = memories;

        const beforeSnooze = Date.now();
        card.dispatchEvent(
          new CustomEvent("smartwindow-resume-card:menu-item-selected", {
            detail: { journeyId, itemId: "snooze" },
            bubbles: true,
            composed: true,
          })
        );
        await TestUtils.waitForCondition(
          () => !section.shadowRoot.querySelector("smartwindow-resume-card"),
          "Wait for the snoozed card to be removed"
        );
        const afterSnooze = Date.now();

        Assert.equal(
          section.shadowRoot.querySelector("smartwindow-resume-card"),
          null,
          "Snoozing the only card should remove it"
        );
        Assert.equal(
          section.shadowRoot.querySelector(".resume-section-empty-heading")
            .dataset.l10nId,
          "aiwindow-resume-section-empty-all-dismissed-heading",
          "Snoozing the last card should show the all-dismissed empty state"
        );
        const metadata = await JourneyStore.get(journeyId);
        Assert.ok(
          !metadata.dismissed,
          "Snoozing should not dismiss permanently"
        );
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
      }
    );
  } finally {
    sb.restore();
  }
});

add_task(async function test_resume_card_close_button_dismisses_permanently() {
  const sb = sinon.createSandbox();
  try {
    await withRenderedResumeCard(
      () => stubResumeActivityGeneration(sb),
      async ({ section, card, memories }) => {
        const [{ id: journeyId }] = memories;

        card.shadowRoot.querySelector(".resume-card-dismiss").click();
        await TestUtils.waitForCondition(
          () => !section.shadowRoot.querySelector("smartwindow-resume-card"),
          "Wait for the dismissed card to be removed"
        );

        Assert.equal(
          section.shadowRoot.querySelector("smartwindow-resume-card"),
          null,
          "Clicking the close button should remove the card"
        );
        Assert.equal(
          section.shadowRoot.querySelector(".resume-section-empty-heading")
            .dataset.l10nId,
          "aiwindow-resume-section-empty-all-dismissed-heading",
          "Closing the last card should show the all-dismissed empty state"
        );
        Assert.ok(
          (await JourneyStore.get(journeyId)).dismissed,
          "Closing should permanently dismiss the journey"
        );
      }
    );
  } finally {
    sb.restore();
  }
});

add_task(async function test_resume_card_open_tabs_opens_tab_group() {
  const sb = sinon.createSandbox();
  try {
    await withRenderedResumeCard(
      () => stubResumeActivityGeneration(sb),
      async ({ win, card, memories }) => {
        // stubResumeActivityGeneration only produces one valid journey.
        const [{ id: journeyId }] = memories;
        const { previewTabs } = card.content;

        const openAndGroupTabsStub = sb
          .stub(ToolUI, "openAndGroupTabs")
          .resolves({ success: true, group: { id: "group-1" } });

        card.dispatchEvent(
          new CustomEvent("smartwindow-resume-card:menu-item-selected", {
            detail: { journeyId, itemId: "open_tabs" },
            bubbles: true,
            composed: true,
          })
        );

        await TestUtils.waitForCondition(
          () => openAndGroupTabsStub.called,
          "Wait for ToolUI.openAndGroupTabs to be called"
        );

        const [{ tabs, window: openWindow }] =
          openAndGroupTabsStub.firstCall.args;
        Assert.deepEqual(
          tabs,
          previewTabs,
          "Should open every preview tab, not just the visible favicons"
        );
        Assert.equal(
          openWindow,
          win,
          "Should open the tabs in the ai-window's chrome window"
        );
      }
    );
  } finally {
    sb.restore();
  }
});

add_task(async function test_resume_card_open_tabs_switches_to_single_tab() {
  const sb = sinon.createSandbox();
  try {
    await withRenderedResumeCard(
      () => stubResumeActivityGenerationPool(sb, 1),
      async ({ win, card, memories }) => {
        const [{ id: journeyId }] = memories;
        const { previewTabs } = card.content;

        const openOrSwitchToTabStub = sb
          .stub(ToolUI, "openOrSwitchToTab")
          .resolves({ success: true, switched: false });
        const openAndGroupTabsStub = sb.stub(ToolUI, "openAndGroupTabs");

        card.dispatchEvent(
          new CustomEvent("smartwindow-resume-card:menu-item-selected", {
            detail: { journeyId, itemId: "open_tabs" },
            bubbles: true,
            composed: true,
          })
        );

        await TestUtils.waitForCondition(
          () => openOrSwitchToTabStub.called,
          "Wait for ToolUI.openOrSwitchToTab to be called"
        );

        const [{ tab, window: openWindow }] =
          openOrSwitchToTabStub.firstCall.args;
        Assert.deepEqual(
          tab,
          previewTabs[0],
          "Should switch to the card's single preview tab"
        );
        Assert.equal(
          openWindow,
          win,
          "Should open the tab in the ai-window's chrome window"
        );
        Assert.ok(
          !openAndGroupTabsStub.called,
          "Should not group a single tab"
        );
      }
    );
  } finally {
    sb.restore();
  }
});

add_task(async function test_resume_section_hide_is_session_scoped() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.smartwindow.resumeCards.enabled", true]],
  });

  ResumeActivity.hideSectionForSession();
  let win;
  try {
    win = await openAIWindow();
    const browser = win.gBrowser.selectedBrowser;
    const aiWindow = await TestUtils.waitForCondition(
      () => browser.contentDocument?.querySelector("ai-window"),
      "Wait for ai-window element"
    );

    Assert.ok(
      aiWindow.resumeSectionHidden,
      "A newly created ai-window should start hidden if a previous tab hid it this session"
    );
    Assert.equal(
      aiWindow.shadowRoot.querySelector("smartwindow-resume-section"),
      null,
      "The resume section should not render in a new tab this session"
    );
  } finally {
    if (win) {
      await BrowserTestUtils.closeWindow(win);
    }
    _resetResumeSectionHiddenForTesting();
  }
});

add_task(
  async function test_resume_section_hide_refreshes_on_preloaded_tab_reveal() {
    await SpecialPowers.pushPrefEnv({
      set: [["browser.smartwindow.resumeCards.enabled", true]],
    });

    let win, tab;
    try {
      win = await openAIWindow();

      // A backgrounded tab's ai-window constructs while its document is
      // still hidden, like a preloaded new tab would.
      tab = BrowserTestUtils.addTab(win.gBrowser, AIWINDOW_URL);
      await BrowserTestUtils.browserLoaded(tab.linkedBrowser);
      const aiWindow = await TestUtils.waitForCondition(
        () => tab.linkedBrowser.contentDocument?.querySelector("ai-window"),
        "Wait for the backgrounded tab's ai-window element"
      );

      Assert.ok(
        tab.linkedBrowser.contentDocument.hidden,
        "Sanity check: the tab should still be backgrounded"
      );
      Assert.ok(
        !aiWindow.resumeSectionHidden,
        "Sanity check: nothing has hidden the section yet"
      );

      // Another tab hides the section while this one is still backgrounded,
      // so its already-constructed ai-window missed the change.
      ResumeActivity.hideSectionForSession();

      await BrowserTestUtils.switchTab(win.gBrowser, tab);

      await TestUtils.waitForCondition(
        () => aiWindow.resumeSectionHidden,
        "Should refresh the hidden state once the backgrounded tab is revealed"
      );
    } finally {
      if (tab) {
        BrowserTestUtils.removeTab(tab);
      }
      if (win) {
        await BrowserTestUtils.closeWindow(win);
      }
      _resetResumeSectionHiddenForTesting();
    }
  }
);

add_task(async function test_resume_section_hide_clears_once_cards_reappear() {
  await SpecialPowers.pushPrefEnv({ set: RESUME_ACTIVITY_PREFS });

  // Hiding the section while it was empty should not carry over once real
  // cards are shown - a later empty state should be a fresh occurrence,
  // not still-suppressed from before.
  ResumeActivity.hideSectionForSession();

  const sb = sinon.createSandbox();
  let firstWin, secondWin;
  try {
    const { cleanup } = await stubResumeActivityGeneration(sb);
    try {
      firstWin = await openAIWindow();
      const firstBrowser = firstWin.gBrowser.selectedBrowser;
      const firstAiWindow = await TestUtils.waitForCondition(
        () => firstBrowser.contentDocument?.querySelector("ai-window"),
        "Wait for the first ai-window element"
      );
      const firstSection = await TestUtils.waitForCondition(
        () =>
          firstAiWindow.shadowRoot.querySelector("smartwindow-resume-section"),
        "The resume section should render once real cards are available"
      );
      const firstCard = await TestUtils.waitForCondition(
        () => firstSection.shadowRoot.querySelector("smartwindow-resume-card"),
        "Wait for a resume card to render"
      );
      Assert.ok(
        !firstAiWindow.resumeSectionHidden,
        "Real cards becoming available should clear the session-hidden flag"
      );

      // Dismiss the rendered journey directly, bypassing the card UI, so
      // the next tab's load finds nothing left to suggest.
      ResumeActivity.dismissMemory(firstCard.journeyId);

      secondWin = await openAIWindow();
      const secondBrowser = secondWin.gBrowser.selectedBrowser;
      const secondAiWindow = await TestUtils.waitForCondition(
        () => secondBrowser.contentDocument?.querySelector("ai-window"),
        "Wait for the second ai-window element"
      );
      const secondSection = await TestUtils.waitForCondition(
        () =>
          secondAiWindow.shadowRoot.querySelector("smartwindow-resume-section"),
        "The resume section should render its empty state, not stay suppressed"
      );
      await TestUtils.waitForCondition(
        () =>
          secondSection.shadowRoot.querySelector(
            ".resume-section-empty-heading"
          ),
        "Wait for the empty-state heading to render"
      );

      Assert.equal(
        secondSection.shadowRoot.querySelector(".resume-section-empty-heading")
          .dataset.l10nId,
        "aiwindow-resume-section-empty-all-dismissed-heading",
        "The empty state should reappear after real cards clear the hidden state"
      );
    } finally {
      await cleanup();
    }
  } finally {
    if (firstWin) {
      await BrowserTestUtils.closeWindow(firstWin);
    }
    if (secondWin) {
      await BrowserTestUtils.closeWindow(secondWin);
    }
    sb.restore();
    _resetResumeSectionHiddenForTesting();
  }
});

add_task(async function test_resume_card_click_shows_confirmation_card() {
  const sb = sinon.createSandbox();
  try {
    sb.stub(openAIEngine, "build").resolves({});
    const fetchWithHistoryStub = sb.stub(Chat, "fetchWithHistory").resolves();

    await SpecialPowers.pushPrefEnv({ set: RESUME_ACTIVITY_PREFS });

    const resumeActivityStubs = await stubResumeActivityGeneration(sb);
    let win;
    try {
      win = await openAIWindow();
      const browser = win.gBrowser.selectedBrowser;
      const aiWindow = await TestUtils.waitForCondition(
        () => browser.contentDocument?.querySelector("ai-window"),
        "Wait for ai-window element"
      );
      const section = await TestUtils.waitForCondition(
        () => aiWindow.shadowRoot.querySelector("smartwindow-resume-section"),
        "Wait for smartwindow-resume-section element"
      );
      const card = await TestUtils.waitForCondition(
        () => section.shadowRoot.querySelector("smartwindow-resume-card"),
        "Wait for a resume card to render"
      );

      const conversationIdAtClick = aiWindow.conversationId;
      Services.fog.testResetFOG();
      // Clicking the card body should follow the
      // same resume flow as a pill click.
      card.shadowRoot.querySelector(".resume-card-title").click();
      assertResumeAction(
        "resumeCardAction",
        aiWindow,
        { action: "card_click", position: "0" },
        "Clicking the card records card_click"
      );

      await TestUtils.waitForCondition(
        () => fetchWithHistoryStub.calledOnce,
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
    } finally {
      if (win) {
        await BrowserTestUtils.closeWindow(win);
      }
      await resumeActivityStubs?.cleanup();
      await SpecialPowers.popPrefEnv();
    }
  } finally {
    sb.restore();
  }
});

/**
 * Clicks an element the way a user would, with mouse events at its center.
 *
 * @param {Element} element
 */
function synthesizeClick(element) {
  element.scrollIntoView({ block: "center" });
  EventUtils.synthesizeMouseAtCenter(element, {}, element.documentGlobal);
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
 * Checks the most recent event recorded for a resume card or section metric.
 *
 * @param {"resumeCardAction"|"resumeSectionAction"} metric
 * @param {Element} aiWindow
 * @param {object} expected - Extras beyond the shared telemetry context
 * @param {string} message
 */
function assertResumeAction(metric, aiWindow, expected, message) {
  const event = Glean.smartWindow[metric].testGetValue()?.at(-1);
  Assert.ok(event, `${message}: an action event was recorded`);
  Assert.deepEqual(
    event.extra,
    {
      chat_id: String(aiWindow.conversationId),
      location: "fullpage",
      message_seq: "0",
      ...expected,
    },
    message
  );
}

add_task(async function test_resume_cards_displayed_telemetry() {
  const sb = sinon.createSandbox();
  Services.fog.testResetFOG();
  try {
    await withRenderedResumeCard(
      () => stubResumeActivityGenerationPool(sb, 3),
      async ({ aiWindow }) => {
        const events = await TestUtils.waitForCondition(
          () => Glean.smartWindow.resumeCardsDisplayed.testGetValue(),
          "Wait for the resume cards displayed event"
        );
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
      }
    );
  } finally {
    sb.restore();
  }
});

add_task(async function test_resume_cards_displayed_telemetry_behind_chat() {
  const sb = sinon.createSandbox();
  try {
    await withRenderedResumeCard(
      () => stubResumeActivityGeneration(sb),
      async ({ aiWindow, section }) => {
        await TestUtils.waitForCondition(
          () => Glean.smartWindow.resumeCardsDisplayed.testGetValue(),
          "Wait for the initial resume cards displayed event"
        );
        Services.fog.testResetFOG();

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

        Assert.equal(
          Glean.smartWindow.resumeCardsDisplayed.testGetValue(),
          null,
          "Cards that finish loading behind a chat are not recorded"
        );
      }
    );
  } finally {
    sb.restore();
  }
});

add_task(async function test_resume_cards_displayed_telemetry_after_hide() {
  const sb = sinon.createSandbox();
  Services.fog.testResetFOG();
  // The section isn't rendered while loading, so it first appears with
  // loading already done.
  ResumeActivity.hideSectionForSession();
  try {
    await withRenderedResumeCard(
      () => stubResumeActivityGeneration(sb),
      async () => {
        const events = await TestUtils.waitForCondition(
          () => Glean.smartWindow.resumeCardsDisplayed.testGetValue(),
          "Wait for the resume cards displayed event"
        );
        Assert.equal(events.length, 1, "The section is recorded once");
        Assert.equal(
          events[0].extra.state,
          "cards",
          "Cards that arrive after the section was hidden are recorded"
        );
      }
    );
  } finally {
    sb.restore();
    _resetResumeSectionHiddenForTesting();
  }
});

add_task(async function test_resume_cards_displayed_telemetry_empty_state() {
  const sb = sinon.createSandbox();
  Services.fog.testResetFOG();
  await SpecialPowers.pushPrefEnv({ set: RESUME_ACTIVITY_PREFS });

  const stubs = await stubResumeActivityGeneration(sb);
  ResumeActivity.dismissMemory("memory-1");
  let win;
  try {
    win = await openAIWindow();
    const browser = win.gBrowser.selectedBrowser;
    const aiWindow = await TestUtils.waitForCondition(
      () => browser.contentDocument?.querySelector("ai-window"),
      "Wait for ai-window element"
    );
    const [event] = await TestUtils.waitForCondition(
      () => Glean.smartWindow.resumeCardsDisplayed.testGetValue(),
      "Wait for the resume cards displayed event"
    );
    Assert.equal(
      event.extra.state,
      "empty_all_dismissed",
      "The all-dismissed empty state is recorded"
    );
    Assert.equal(event.extra.cards, "0", "No cards are visible");

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
  } finally {
    if (win) {
      await BrowserTestUtils.closeWindow(win);
    }
    await stubs.cleanup();
    sb.restore();
    _resetResumeSectionHiddenForTesting();
    await SpecialPowers.popPrefEnv();
  }
});

add_task(async function test_resume_card_action_telemetry() {
  const sb = sinon.createSandbox();
  Services.fog.testResetFOG();
  try {
    await withRenderedResumeCard(
      () => stubResumeActivityGenerationPool(sb, 3),
      async ({ aiWindow, section }) => {
        sb.stub(ToolUI, "openOrGroupTabs").resolves();
        await TestUtils.waitForCondition(
          () => Glean.smartWindow.resumeCardsDisplayed.testGetValue(),
          "Wait for the initial resume cards displayed event"
        );
        const card = () =>
          section.shadowRoot.querySelectorAll("smartwindow-resume-card")[1];

        await clickResumeCardMenuItem(card(), "aiwindow-resume-card-open-tabs");
        assertResumeAction(
          "resumeCardAction",
          aiWindow,
          { action: "open_tabs", position: "1" },
          "Open tabs records the card's position"
        );

        synthesizeClick(
          section.shadowRoot.querySelector(".resume-section-toggle")
        );
        assertResumeAction(
          "resumeSectionAction",
          aiWindow,
          { action: "show_more" },
          "Show more records show_more"
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
        await section.updateComplete;

        await clickResumeCardMenuItem(card(), "aiwindow-resume-card-snooze");
        await TestUtils.waitForCondition(
          () =>
            section.shadowRoot.querySelectorAll("smartwindow-resume-card")
              .length === 2,
          "Wait for the snoozed card to be removed"
        );
        assertResumeAction(
          "resumeCardAction",
          aiWindow,
          { action: "snooze", position: "1" },
          "Snooze records the card's position"
        );

        // The close button only appears while the card is hovered.
        const firstCard = section.shadowRoot.querySelector(
          "smartwindow-resume-card"
        );
        EventUtils.synthesizeMouseAtCenter(
          firstCard,
          { type: "mousemove" },
          firstCard.documentGlobal
        );
        synthesizeClick(
          firstCard.shadowRoot.querySelector(".resume-card-dismiss")
        );
        await TestUtils.waitForCondition(
          () =>
            section.shadowRoot.querySelectorAll("smartwindow-resume-card")
              .length === 1,
          "Wait for the dismissed card to be removed"
        );
        assertResumeAction(
          "resumeCardAction",
          aiWindow,
          { action: "dismiss", position: "0" },
          "The close button records dismiss"
        );
        // Removing a card triggers the initial-impression update path;
        // ensure the section isn't recorded again.
        await new Promise(resolve =>
          section.documentGlobal.requestAnimationFrame(resolve)
        );
        Assert.equal(
          Glean.smartWindow.resumeCardsDisplayed.testGetValue().length,
          1,
          "Removing cards should not record another displayed event"
        );
      }
    );
  } finally {
    sb.restore();
  }
});

add_task(async function test_resume_card_resume_button_telemetry() {
  const sb = sinon.createSandbox();
  sb.stub(openAIEngine, "build").resolves({});
  const fetchWithHistoryStub = sb.stub(Chat, "fetchWithHistory").resolves();
  try {
    await withRenderedResumeCard(
      () => stubResumeActivityGenerationPool(sb, 3),
      async ({ aiWindow, section }) => {
        Services.fog.testResetFOG();
        const card = section.shadowRoot.querySelectorAll(
          "smartwindow-resume-card"
        )[1];
        synthesizeClick(
          card.shadowRoot.querySelector(".resume-card-resume-button")
        );
        assertResumeAction(
          "resumeCardAction",
          aiWindow,
          { action: "resume", position: "1" },
          "The Resume button records resume"
        );
        Assert.equal(
          Glean.smartWindow.resumeCardAction.testGetValue().length,
          1,
          "The Resume button records a single action"
        );
        await TestUtils.waitForCondition(
          () => fetchWithHistoryStub.calledOnce,
          "Wait for the resume flow to finish before closing the window"
        );
      }
    );
  } finally {
    sb.restore();
  }
});
