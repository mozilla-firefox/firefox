/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

let win;

add_setup(async function () {
  win = await openSharedAIWindow();
});

describe("Hiding the resume section", () => {
  afterEach(() => {
    _resetResumeSectionHiddenForTesting();
  });

  describe("with resume cards enabled", () => {
    beforeEach(async () => {
      await SpecialPowers.pushPrefEnv({
        set: [["browser.smartwindow.resumeCards.enabled", true]],
      });
    });

    afterEach(async () => {
      await SpecialPowers.popPrefEnv();
    });

    it("keeps it hidden in tabs opened later this session", async () => {
      ResumeActivity.hideSectionForSession();

      const aiWindow = await openOnlyAIWindowTab(win);

      Assert.ok(
        aiWindow.resumeSectionHidden,
        "A newly created ai-window should start hidden if a previous tab hid it this session"
      );
      Assert.equal(
        aiWindow.shadowRoot.querySelector("smartwindow-resume-section"),
        null,
        "The resume section should not render in a new tab this session"
      );
    });

    // A backgrounded tab's ai-window constructs while its document is still
    // hidden, like a preloaded new tab would, so it misses a hide from another
    // tab until it is revealed.
    it("reaches a background tab once it is revealed", async () => {
      await openOnlyAIWindowTab(win);
      const tab = BrowserTestUtils.addTab(win.gBrowser, AIWINDOW_URL);
      await BrowserTestUtils.browserLoaded(tab.linkedBrowser);
      const aiWindow = await TestUtils.waitForCondition(
        () => tab.linkedBrowser.contentDocument?.querySelector("ai-window"),
        "Wait for the backgrounded tab's ai-window element"
      );
      Assert.ok(
        tab.linkedBrowser.contentDocument.hidden &&
          !aiWindow.resumeSectionHidden,
        "The tab is still backgrounded and nothing has hidden its section yet"
      );
      ResumeActivity.hideSectionForSession();

      await BrowserTestUtils.switchTab(win.gBrowser, tab);

      await TestUtils.waitForCondition(
        () => aiWindow.resumeSectionHidden,
        "Should refresh the hidden state once the backgrounded tab is revealed"
      );
    });
  });

  // Hiding the section while it was empty should not carry over once real
  // cards are shown - a later empty state should be a fresh occurrence, not
  // still-suppressed from before.
  describe("while it is empty", () => {
    let sandbox;
    let stubs;

    beforeEach(async () => {
      await SpecialPowers.pushPrefEnv({ set: RESUME_ACTIVITY_PREFS });
      sandbox = sinon.createSandbox();
      ResumeActivity.hideSectionForSession();
      stubs = await stubResumeActivityGeneration(sandbox);
    });

    afterEach(async () => {
      await stubs?.cleanup();
      sandbox.restore();
      await SpecialPowers.popPrefEnv();
    });

    it("clears once real cards are shown", async () => {
      const aiWindow = await openOnlyAIWindowTab(win);

      await waitForResumeCard(aiWindow);

      Assert.ok(
        !aiWindow.resumeSectionHidden,
        "Real cards becoming available should clear the session-hidden flag"
      );
    });

    it("lets a later empty state show again", async () => {
      const { card } = await waitForResumeCard(await openOnlyAIWindowTab(win));
      // Dismiss the rendered journey directly, bypassing the card UI, so the
      // next tab's load finds nothing left to suggest.
      ResumeActivity.dismissMemory(card.journeyId);

      const aiWindow = await openOnlyAIWindowTab(win);

      const section = await TestUtils.waitForCondition(
        () => aiWindow.shadowRoot.querySelector("smartwindow-resume-section"),
        "The resume section should render its empty state, not stay suppressed"
      );
      const heading = await TestUtils.waitForCondition(
        () => section.shadowRoot.querySelector(".resume-section-empty-heading"),
        "Wait for the empty-state heading to render"
      );
      Assert.equal(
        heading.dataset.l10nId,
        "aiwindow-resume-section-empty-all-dismissed-heading",
        "The empty state should reappear after real cards clear the hidden state"
      );
    });
  });
});
