/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/* import-globals-from ../head_monitor_chat.js */
Services.scriptloader.loadSubScript(
  new URL("../head_monitor_chat.js", getRootDirectory(gTestPath)).href,
  this
);

add_setup(setupMonitorButton);

describe("Watch task confirmation in chat", () => {
  let chat;

  beforeEach(async () => {
    chat = await openWatchableChat();
    await runWatchCommand(chat, "new posts");
  });

  afterEach(async () => {
    await chat.cleanup();
  });

  it("confirms the daily schedule picked on the card", async () => {
    await createFromCard(chat, { frequency: "daily", time: "14:30" });

    const { l10nId, l10nArgs, text } = await readConfirmation(chat);
    Assert.equal(
      l10nId,
      "smartwindow-agent-monitor-watching-daily",
      "A daily task gets the daily confirmation"
    );
    Assert.equal(
      l10nArgs.scheduleTime,
      "14:30",
      "The confirmation saves the time picked on the card"
    );
    Assert.ok(
      !("weekday" in l10nArgs),
      "A daily confirmation saves no weekday"
    );

    const { time } = formatScheduleTime(14, 30);
    Assert.ok(
      text.startsWith(`I’ll check ${l10nArgs.monitorName} daily at ${time}.`),
      `The chat shows the picked daily time, got: "${text}"`
    );
  });

  it("confirms the weekly schedule picked on the card", async () => {
    await createFromCard(chat, {
      frequency: "weekly",
      weekday: 3,
      time: "18:30",
    });

    const { l10nId, l10nArgs, text } = await readConfirmation(chat);
    Assert.equal(
      l10nId,
      "smartwindow-agent-monitor-watching-weekly",
      "A weekly task gets the weekly confirmation"
    );
    Assert.equal(
      l10nArgs.scheduleTime,
      "18:30",
      "The confirmation saves the time picked on the card"
    );
    Assert.equal(
      l10nArgs.weekday,
      3,
      "The confirmation saves the day picked on the card"
    );

    const { time, weekday } = formatScheduleTime(18, 30, 3);
    Assert.ok(
      text.startsWith(
        `I’ll check ${l10nArgs.monitorName} weekly on ${weekday} at ${time}.`
      ),
      `The chat shows the picked day and time, got: "${text}"`
    );
  });
});
