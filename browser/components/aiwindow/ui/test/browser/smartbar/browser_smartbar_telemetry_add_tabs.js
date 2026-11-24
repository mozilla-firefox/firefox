/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

let win;

add_setup(async function () {
  await setupSmartbarTelemetry();
  win = await openSharedAIWindow();
});

describe("Smartbar add tabs telemetry", () => {
  let browser;

  beforeEach(async () => {
    await resetTelemetry();
    await openOnlyAIWindowTab(win);
    browser = win.gBrowser.selectedBrowser;
    await addBackgroundTab(win, "https://example.com/");
  });

  describe("add tabs button", () => {
    it("records add_tabs_click", async () => {
      await clickAddTabsButton(browser);

      const events = Glean.smartWindow.addTabsClick.testGetValue();
      Assert.equal(events.length, 1, "Should have add_tabs_click events");
      const extra = events[0].extra;
      Assert.ok(extra.chat_id, "add_tabs_click has chat_id");
      Assert.equal(
        extra.location,
        "fullpage",
        "add_tabs_click has correct location"
      );
      Assert.equal(
        extra.message_seq,
        "0",
        "add_tabs_click has correct message_seq"
      );
      Assert.equal(
        extra.tabs_available,
        "1",
        "add_tabs_click has correct tabs_available"
      );
      Assert.equal(
        extra.tabs_preselected,
        "0",
        "add_tabs_click has correct tabs_preselected"
      );
    });

    it("records add_tabs_selection when a tab is picked", async () => {
      await clickAddTabsButton(browser);

      await clickFirstPanelItem(browser);

      const events = Glean.smartWindow.addTabsSelection.testGetValue();
      Assert.equal(events.length, 1, "Should have add_tabs_selection events");
      const extra = events[0].extra;
      Assert.ok(extra.chat_id, "add_tabs_selection has chat_id");
      Assert.equal(
        extra.location,
        "fullpage",
        "add_tabs_selection has correct location"
      );
      Assert.equal(
        extra.message_seq,
        "0",
        "add_tabs_selection has correct message_seq"
      );
      Assert.equal(
        extra.tabs_available,
        "1",
        "add_tabs_selection has correct tabs_available"
      );
      Assert.equal(
        extra.tabs_preselected,
        "0",
        "add_tabs_selection has correct tabs_preselected"
      );
      Assert.equal(
        extra.tabs_selected,
        "1",
        "add_tabs_selection has correct tabs_selected"
      );
      Assert.equal(
        extra.mention_type,
        "tab",
        "add_tabs_selection reports a tab selection"
      );
    });

    it("records remove_tab when an added tab's chip is removed", async () => {
      await clickAddTabsButton(browser);
      await clickFirstPanelItem(browser);
      await resetTelemetry();

      await SpecialPowers.spawn(browser, [], async () => {
        content.document
          .querySelector("ai-window")
          .shadowRoot.querySelector(
            "#ai-window-smartbar website-chip-container"
          )
          .shadowRoot.querySelector("smartwindow-chip")
          .shadowRoot.querySelector(".chip-remove")
          .click();
      });

      const events = Glean.smartWindow.removeTab.testGetValue();
      Assert.equal(events.length, 1, "Should have remove_tab events");
      const extra = events[0].extra;
      Assert.ok(extra.chat_id, "remove_tab has chat_id");
      Assert.equal(
        extra.location,
        "fullpage",
        "remove_tab has correct location"
      );
      Assert.equal(
        extra.message_seq,
        "0",
        "remove_tab has correct message_seq"
      );
      Assert.equal(
        extra.tabs_selected,
        "0",
        "remove_tab has correct remaining tabs count"
      );
    });
  });
});
