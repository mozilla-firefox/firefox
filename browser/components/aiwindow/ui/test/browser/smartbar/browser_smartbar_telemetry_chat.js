/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

let win;
let expectedModel;

add_setup(async function () {
  expectedModel = await setupSmartbarTelemetry();
  win = await openSharedAIWindow();
});

describe("Smartbar chat telemetry", () => {
  let browser;

  beforeEach(async () => {
    await resetTelemetry();
    await openOnlyAIWindowTab(win);
    browser = win.gBrowser.selectedBrowser;
  });

  describe("when a chat is submitted", () => {
    it("records chat_submit for the Enter key", async () => {
      await typeInSmartbar(browser, "tell me a joke");
      await waitForSmartbarAction(browser, "chat");

      await submitSmartbar(browser);

      const events = Glean.smartWindow.chatSubmit.testGetValue();
      Assert.equal(events.length, 1, "Should have one chat_submit event");
      const extra = events[0].extra;
      Assert.ok(extra.chat_id, "chat_submit has chat_id");
      Assert.equal(
        extra.message_seq,
        "0",
        "chat_submit has correct message_seq"
      );
      Assert.equal(extra.model, expectedModel, "chat_submit has correct model");
      Assert.equal(
        extra.location,
        "fullpage",
        "chat_submit has correct location"
      );
      Assert.equal(
        extra.detected_intent,
        "chat",
        "chat_submit has correct detected_intent"
      );
      Assert.equal(
        extra.submit_type,
        "enter",
        "chat_submit has correct submit_type for enter key"
      );
      Assert.equal(extra.tabs, "0", "chat_submit has correct tabs count");
      Assert.equal(
        extra.mentions,
        "0",
        "chat_submit has correct mentions count"
      );
      Assert.equal(extra.length, "14", "chat_submit has correct length");
    });

    it("records chat_submit for the submit button", async () => {
      await typeInSmartbar(browser, "tell me a joke");
      await waitForSmartbarAction(browser, "chat");

      await submitSmartbar(browser, { useButton: true });

      const events = Glean.smartWindow.chatSubmit.testGetValue();
      Assert.equal(events.length, 1, "Should have one chat_submit event");
      Assert.equal(
        events[0].extra.submit_type,
        "button",
        "chat_submit has correct submit_type for button click"
      );
    });

    it("counts a mentioned tab group as one tab", async () => {
      const group = await addTabsInNewGroup(
        win,
        ["https://example.com/one", "https://example.com/two"],
        { label: "Group one", color: "blue" }
      );
      await SpecialPowers.spawn(browser, [group.id], async groupId => {
        content.document
          .querySelector("ai-window")
          .shadowRoot.querySelector("#ai-window-smartbar")
          .addContextMention({
            type: "tabGroup",
            groupId,
            label: "Group one",
            color: "blue",
          });
      });
      await typeInSmartbar(browser, "summarize these");
      await waitForSmartbarAction(browser, "chat");

      await submitSmartbar(browser);

      const events = Glean.smartWindow.chatSubmit.testGetValue();
      Assert.equal(events.length, 1, "Should have one chat_submit event");
      Assert.equal(
        events[0].extra.tabs,
        "1",
        "chat_submit counts the mentioned tab group once"
      );
    });

    // The live guess is "navigate" for URL-shaped input, but locking "chat"
    // overrides it.
    it("records chat_submit without navigating when chat is locked over a navigate guess", async () => {
      await stubLoadURL(browser, { captureURL: true });
      await typeInSmartbar(browser, "https://example.com");
      await waitForSmartbarAction(browser, "navigate");
      await selectExplicitSmartbarAction(browser, "chat");
      await waitForSmartbarAction(browser, "chat");

      await submitSmartbar(browser);

      const events = Glean.smartWindow.chatSubmit.testGetValue();
      Assert.equal(
        events.length,
        1,
        "Locking chat over a navigate guess should still record chat_submit"
      );
      Assert.equal(
        events[0].extra.detected_intent,
        "navigate",
        "chat_submit preserves the live detected_intent even when chat is locked"
      );
      const { called } = await getStubLoadURLResult(browser);
      Assert.ok(!called, "A locked chat submit should not navigate");
    });
  });
});
