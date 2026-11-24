/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

let win;
let expectedModel;

add_setup(async function () {
  expectedModel = await setupSmartbarTelemetry();
  win = await openSharedAIWindow();
});

describe("Smartbar navigate and search telemetry", () => {
  let browser;

  beforeEach(async () => {
    await resetTelemetry();
    await openOnlyAIWindowTab(win);
    browser = win.gBrowser.selectedBrowser;
  });

  describe("when a URL is submitted", () => {
    beforeEach(async () => {
      await stubLoadURL(browser);
      await typeInSmartbar(browser, "https://example.com");
      await waitForSmartbarAction(browser, "navigate");
    });

    it("records navigate_submit for the Enter key", async () => {
      await submitSmartbar(browser);

      const events = Glean.smartWindow.navigateSubmit.testGetValue();
      Assert.equal(events.length, 1, "Should have one navigate_submit event");
      const extra = events[0].extra;
      Assert.ok(extra.chat_id, "navigate_submit has chat_id");
      Assert.equal(
        extra.message_seq,
        "0",
        "navigate_submit has correct message_seq"
      );
      Assert.equal(
        extra.model,
        expectedModel,
        "navigate_submit has correct model"
      );
      Assert.equal(extra.length, "20", "navigate_submit has correct length");
      Assert.equal(
        extra.location,
        "fullpage",
        "navigate_submit has correct location"
      );
      Assert.equal(
        extra.detected_intent,
        "navigate",
        "navigate_submit has correct detected_intent"
      );
      Assert.equal(
        extra.submit_type,
        "enter",
        "navigate_submit has correct submit_type for enter key"
      );
    });

    it("records navigate_submit for the submit button", async () => {
      await submitSmartbar(browser, { useButton: true });

      const events = Glean.smartWindow.navigateSubmit.testGetValue();
      Assert.equal(events.length, 1, "Should have one navigate_submit event");
      Assert.equal(
        events[0].extra.submit_type,
        "button",
        "navigate_submit has correct submit_type for button click"
      );
    });
  });

  describe("when a search is submitted", () => {
    it("records search_submit", async () => {
      await stubLoadURL(browser);
      await typeInSmartbar(browser, "search for cats");
      await waitForSmartbarAction(browser, "search");

      await submitSmartbar(browser, { useButton: true });

      const events = Glean.smartWindow.searchSubmit.testGetValue();
      Assert.equal(events.length, 1, "Should have one search_submit event");
      const extra = events[0].extra;
      Assert.ok(extra.chat_id, "search_submit has chat_id");
      Assert.equal(
        extra.message_seq,
        "0",
        "search_submit has correct message_seq"
      );
      Assert.equal(
        extra.model,
        expectedModel,
        "search_submit has correct model"
      );
      Assert.equal(
        extra.location,
        "fullpage",
        "search_submit has correct location"
      );
      Assert.equal(
        extra.detected_intent,
        "search",
        "search_submit has correct detected_intent"
      );
      Assert.equal(extra.length, "15", "search_submit has correct length");
      Assert.ok(extra.provider, "search_submit has provider");
      Assert.equal(
        extra.submit_type,
        "button",
        "search_submit has correct submit_type for button click"
      );
    });

    it("keeps the detected intent when the user picks search instead", async () => {
      await typeInSmartbar(browser, "test");
      await stubOpenSERP(browser);
      await openActionMenu(browser, "search");
      // Picking the action only locks the button; submit it to run the search.
      await waitForSmartbarAction(browser, "search");

      await submitSmartbar(browser, { useButton: true });

      const previewEvents =
        Glean.smartWindow.intentChangePreview.testGetValue();
      Assert.equal(
        previewEvents.length,
        1,
        "Should have one intent_change_preview event"
      );
      Assert.equal(
        previewEvents[0].extra.current_intent,
        "chat",
        "intent_change_preview current_intent reflects intent model"
      );
      const searchEvents = Glean.smartWindow.searchSubmit.testGetValue();
      Assert.equal(
        searchEvents.length,
        1,
        "Should have one search_submit event"
      );
      Assert.equal(
        searchEvents[0].extra.detected_intent,
        "chat",
        "search_submit detected_intent is chat even though user picked search"
      );
      Assert.equal(
        searchEvents[0].extra.submit_type,
        "button",
        "search_submit has correct submit_type for explicit action selection"
      );
    });
  });
});
