/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const TEST_REGION = "US";

let win;

add_setup(async function () {
  await setupSmartbarTelemetry();
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.smartwindow.agent.enabled", true],
      [
        "browser.smartwindow.agent.supportedRegions",
        JSON.stringify([[TEST_REGION, ["en-*"]]]),
      ],
    ],
  });
  const originalRegion = Region.home;
  Region._setHomeRegion(TEST_REGION, false);
  registerCleanupFunction(() => {
    Region._setHomeRegion(originalRegion, false);
  });
  win = await openSharedAIWindow();
});

describe("Smartbar agent command telemetry", () => {
  let browser;

  beforeEach(async () => {
    await resetTelemetry();
    await openOnlyAIWindowTab(win);
    browser = win.gBrowser.selectedBrowser;
  });

  // Typing past the "/" covers that agent_command_start fires only once.
  it("records agent_command_start once when / opens the palette", async () => {
    await typeInSmartbar(browser, "/watc");

    await waitForPanelItem(browser);

    const events = Glean.smartWindow.agentCommandStart.testGetValue();
    Assert.equal(
      events.length,
      1,
      "agent_command_start fires once when '/' opens the palette"
    );
    const extra = events[0].extra;
    Assert.ok(extra.chat_id, "agent_command_start has chat_id");
    Assert.equal(
      extra.location,
      "fullpage",
      "agent_command_start has correct location"
    );
    Assert.greater(
      Number(extra.commands_available),
      0,
      "agent_command_start has commands_available"
    );
    Assert.equal(
      extra.message_seq,
      "0",
      "agent_command_start has correct message_seq"
    );
    Assert.equal(
      extra.source,
      "manual",
      "agent_command_start has manual source"
    );
  });

  it("records agent_command_select when a command is picked", async () => {
    await typeInSmartbar(browser, "/");
    await waitForPanelItem(browser);

    await clickFirstPanelItem(browser);

    const events = Glean.smartWindow.agentCommandSelect.testGetValue();
    Assert.equal(events.length, 1, "Should have agent_command_select events");
    const extra = events[0].extra;
    Assert.ok(extra.chat_id, "agent_command_select has chat_id");
    Assert.equal(
      extra.agent,
      "watch",
      "agent_command_select records the agent"
    );
    Assert.equal(
      extra.source,
      "manual",
      "agent_command_select has manual source"
    );
    Assert.equal(
      extra.location,
      "fullpage",
      "agent_command_select has correct location"
    );
    Assert.greater(
      Number(extra.commands_available),
      0,
      "agent_command_select has commands_available"
    );
    Assert.equal(
      extra.message_seq,
      "0",
      "agent_command_select has correct message_seq"
    );
  });

  it("records agent_command_select when a command is typed and submitted", async () => {
    await typeInSmartbar(browser, "/watch check this page");

    await submitSmartbar(browser);

    const events = Glean.smartWindow.agentCommandSelect.testGetValue();
    Assert.equal(
      events.length,
      1,
      "Should have one agent_command_select event"
    );
    const extra = events[0].extra;
    Assert.ok(extra.chat_id, "agent_command_select has chat_id");
    Assert.equal(
      extra.agent,
      "watch",
      "agent_command_select records the typed agent"
    );
    Assert.equal(
      extra.source,
      "manual",
      "agent_command_select has manual source"
    );
    Assert.equal(
      extra.location,
      "fullpage",
      "agent_command_select has correct location"
    );
    Assert.equal(
      extra.message_seq,
      "0",
      "agent_command_select has correct message_seq"
    );
  });

  it("records agent_command_remove when the command is backspaced away", async () => {
    await typeInSmartbar(browser, "/watch");
    await waitForPanelItem(browser);

    await SpecialPowers.spawn(browser, [], async () => {
      const smartbar = content.document
        .querySelector("ai-window")
        .shadowRoot.querySelector("#ai-window-smartbar");
      smartbar.inputField.focus();
      for (let i = 0; i < "/watch".length; i++) {
        EventUtils.synthesizeKey("KEY_Backspace", {}, content);
      }
      await ContentTaskUtils.waitForCondition(
        () => !smartbar.untrimmedValue,
        "Wait for the command to be cleared from the input"
      );
    });

    const events = Glean.smartWindow.agentCommandRemove.testGetValue();
    Assert.equal(
      events.length,
      1,
      "Should have one agent_command_remove event"
    );
    const extra = events[0].extra;
    Assert.ok(extra.chat_id, "agent_command_remove has chat_id");
    Assert.equal(
      extra.agent,
      "watch",
      "agent_command_remove records the removed agent"
    );
    Assert.equal(
      extra.location,
      "fullpage",
      "agent_command_remove has correct location"
    );
    Assert.equal(
      extra.message_seq,
      "0",
      "agent_command_remove has correct message_seq"
    );
    Assert.equal(
      extra.source,
      "manual",
      "agent_command_remove has manual source"
    );
  });
});
