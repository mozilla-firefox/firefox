/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

let expectedModel;

add_setup(async function () {
  expectedModel = await setupSmartbarTelemetry();
});

// The sidebar is window state, so each test opens a window of its own.
describe("Smartbar telemetry in the sidebar", () => {
  let win;
  let sidebarBrowser;

  beforeEach(async () => {
    ({ win, sidebarBrowser } = await openAIWindowWithSidebar(
      "https://example.com/"
    ));
    await resetTelemetry();
  });

  afterEach(async () => {
    if (win) {
      await BrowserTestUtils.closeWindow(win);
      win = null;
    }
  });

  it("records an abandonment with the Smart Window extra keys", async () => {
    await typeInSmartbar(sidebarBrowser, "test");
    await waitForSmartbarAction(sidebarBrowser, "chat");

    await SpecialPowers.spawn(sidebarBrowser, [], async () => {
      content.document
        .querySelector("ai-window")
        .shadowRoot.querySelector("#ai-window-smartbar")
        .blur();
    });

    const events = Glean.urlbar.abandonment.testGetValue() ?? [];
    const smartbarEvent = events.find(e => e.extra.sap === "smartbar");
    Assert.ok(smartbarEvent, "Should have a smartbar abandonment event");
    const extra = smartbarEvent.extra;
    Assert.ok(extra.chat_id, "abandonment has chat_id");
    Assert.equal(extra.intent, "chat", "abandonment has correct intent");
    Assert.equal(
      extra.window_mode,
      "smartwindow",
      "abandonment has correct window mode"
    );
    Assert.equal(extra.location, "sidebar", "abandonment has correct location");
    Assert.equal(extra.model, expectedModel, "abandonment has correct model");
  });

  it("records mention_start when @ is typed", async () => {
    await typeInSmartbar(sidebarBrowser, "@");

    await waitForMentionsOpen(sidebarBrowser);

    const events = Glean.smartWindow.mentionStart.testGetValue();
    Assert.equal(events.length, 1, "Should have mention_start events");
    const extra = events[0].extra;
    Assert.ok(extra.chat_id, "mention_start has chat_id");
    Assert.equal(
      extra.location,
      "sidebar",
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
});
