/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

let win;

add_setup(async function () {
  await setupSmartbarTelemetry();
  win = await openSharedAIWindow();
});

describe("Smartbar mention telemetry", () => {
  let browser;

  beforeEach(async () => {
    await resetTelemetry();
    await openOnlyAIWindowTab(win);
    browser = win.gBrowser.selectedBrowser;
    await addBackgroundTab(win, "https://example.com/");
  });

  describe("when @ is typed", () => {
    it("records mention_start", async () => {
      await typeInSmartbar(browser, "@");

      await waitForMentionsOpen(browser);

      const events = Glean.smartWindow.mentionStart.testGetValue();
      Assert.equal(events.length, 1, "Should have mention_start events");
      const extra = events[0].extra;
      Assert.ok(extra.chat_id, "mention_start has chat_id");
      Assert.equal(
        extra.location,
        "fullpage",
        "mention_start has correct location"
      );
      Assert.equal(
        extra.mentions_available,
        "1",
        "mention_start has correct mentions_available"
      );
      Assert.equal(
        extra.message_seq,
        "0",
        "mention_start has correct message_seq"
      );
    });

    it("records mention_select when a tab is picked", async () => {
      await typeInSmartbar(browser, "@");
      await waitForMentionsOpen(browser);

      await clickFirstPanelItem(browser);

      const events = Glean.smartWindow.mentionSelect.testGetValue();
      Assert.equal(events.length, 1, "Should have mention_select events");
      const extra = events[0].extra;
      Assert.ok(extra.chat_id, "mention_select has chat_id");
      Assert.equal(
        extra.location,
        "fullpage",
        "mention_select has correct location"
      );
      Assert.greater(
        Number(extra.length),
        0,
        "mention_select has length of the mention"
      );
      Assert.equal(
        extra.mentions_available,
        "1",
        "mention_select has correct mentions_available"
      );
      Assert.equal(
        extra.mention_type,
        "tab",
        "mention_select reports a tab selection"
      );
      Assert.equal(
        extra.message_seq,
        "0",
        "mention_select has correct message_seq"
      );
    });
  });

  describe("with a mention inserted", () => {
    beforeEach(async () => {
      await insertMention(browser);
      await resetTelemetry();
    });

    it("records mention_remove when it is backspaced away", async () => {
      await BrowserTestUtils.synthesizeKey("KEY_Backspace", {}, browser);
      await BrowserTestUtils.synthesizeKey("KEY_Backspace", {}, browser);

      const events = Glean.smartWindow.mentionRemove.testGetValue();
      Assert.equal(events.length, 1, "Should have mention_remove events");
      const extra = events[0].extra;
      Assert.ok(extra.chat_id, "mention_remove has chat_id");
      Assert.equal(
        extra.location,
        "fullpage",
        "mention_remove has correct location"
      );
      Assert.equal(
        extra.mentions,
        "0",
        "mention_remove has correct remaining mentions count"
      );
      Assert.equal(
        extra.message_seq,
        "0",
        "mention_remove has correct message_seq"
      );
    });

    it("counts the mentions left when one of two is removed", async () => {
      await addBackgroundTab(win, "https://example.org/");
      await typeInSmartbar(browser, " @");
      await waitForMentionsOpen(browser);
      await clickFirstPanelItem(browser);
      await SpecialPowers.spawn(browser, [], async () => {
        const editor = content.document
          .querySelector("ai-window")
          .shadowRoot.querySelector("#ai-window-smartbar moz-multiline-editor");
        await ContentTaskUtils.waitForMutationCondition(
          editor.shadowRoot,
          { childList: true, subtree: true },
          () =>
            editor.shadowRoot.querySelectorAll("smartwindow-chip").length === 2
        );
      });
      await resetTelemetry();

      await BrowserTestUtils.synthesizeKey("KEY_Backspace", {}, browser);
      await BrowserTestUtils.synthesizeKey("KEY_Backspace", {}, browser);

      const events = Glean.smartWindow.mentionRemove.testGetValue();
      Assert.equal(events.length, 1, "Should have one mention_remove event");
      Assert.equal(
        events[0].extra.mentions,
        "1",
        "mention_remove reports 1 remaining mention after removing one of two"
      );
    });
  });
});

/**
 * Types @ and picks the first tab the mentions panel offers.
 *
 * @param {MozBrowser} browser
 */
async function insertMention(browser) {
  await typeInSmartbar(browser, "@");
  await waitForMentionsOpen(browser);
  await clickFirstPanelItem(browser);
  await waitForMentionInserted(browser);
}
