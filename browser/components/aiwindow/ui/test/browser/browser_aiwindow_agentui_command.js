/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/* import-globals-from head_monitor_chat.js */
Services.scriptloader.loadSubScript(
  getRootDirectory(gTestPath) + "head_monitor_chat.js",
  this
);

const { AgentUI } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/ui/modules/AgentUI.sys.mjs"
);

const SUPPORTED_REGIONS_PREF = "browser.smartwindow.agent.supportedRegions";
const TEST_REGION = "US";
const TEST_MARKETS = JSON.stringify([[TEST_REGION, ["en-*"]]]);

// Pin the home region to a supported one so the region gate doesn't depend on
// where the test runs. The region-gate tasks below override the pref.
add_setup(async function () {
  const originalRegion = Region.home;
  Region._setHomeRegion(TEST_REGION, false);
  await SpecialPowers.pushPrefEnv({
    set: [[SUPPORTED_REGIONS_PREF, TEST_MARKETS]],
  });
  registerCleanupFunction(() => {
    Region._setHomeRegion(originalRegion, false);
  });
});

function makeConversationStub() {
  let resolveSeeded;
  const seeded = new Promise(resolve => {
    resolveSeeded = resolve;
  });
  const assistantMessages = [];
  const l10nMessages = [];
  const conversation = {
    addUserMessage: () => ({}),
    emit: () => {},
    addAssistantMessage: (_type, body) => assistantMessages.push(body),
    addAssistantWithL10nMessage: l10nId => {
      l10nMessages.push(l10nId);
      return { content: { body: "", l10nId } };
    },
    addUIToolToCurrentMessage: (_id, data) => resolveSeeded(data),
    resolvePendingToolConfirmation: () => false,
  };
  return { conversation, seeded, assistantMessages, l10nMessages };
}

add_task(async function test_monitor_command_prefills_condition() {
  await SpecialPowers.pushPrefEnv({ set: [[PREF_AGENT_ENABLED, true]] });
  await MonitorAgent._resetForTesting();

  try {
    const { conversation, seeded, assistantMessages } = makeConversationStub();
    const handled = AgentUI.tryHandleCommand({
      command: AGENT_COMMANDS.WATCH,
      value: `${WATCH_COMMAND} the price drops below $200`,
      contextPageUrl: "https://example.com/product",
      conversation,
    });
    Assert.ok(handled, "The /watch command is handled");

    const { properties } = await seeded;
    Assert.equal(
      properties.agent.condition,
      "the price drops below $200",
      "The watch card is seeded with the text typed after /watch"
    );
    Assert.ok(
      assistantMessages.some(body => body?.includes("watch this page")),
      "The localized monitor-setup message is shown"
    );
  } finally {
    await MonitorAgent._resetForTesting();
    await SpecialPowers.popPrefEnv();
  }
});

add_task(async function test_bare_monitor_command_seeds_empty_condition() {
  await SpecialPowers.pushPrefEnv({ set: [[PREF_AGENT_ENABLED, true]] });
  await MonitorAgent._resetForTesting();

  try {
    const { conversation, seeded } = makeConversationStub();
    AgentUI.tryHandleCommand({
      value: WATCH_COMMAND,
      contextPageUrl: "https://example.com/product",
      conversation,
    });

    const { properties } = await seeded;
    Assert.equal(
      properties.agent.condition,
      "",
      "A bare /watch command seeds an empty condition"
    );
  } finally {
    await MonitorAgent._resetForTesting();
    await SpecialPowers.popPrefEnv();
  }
});

add_task(async function test_monitor_command_rejects_non_watchable_page() {
  await SpecialPowers.pushPrefEnv({ set: [[PREF_AGENT_ENABLED, true]] });
  await MonitorAgent._resetForTesting();

  // Only http(s) pages can be watched; internal pages, empty context, and
  // invalid URLs should be rejected with a message and seed no card.
  const nonWatchableUrls = [
    "about:firefoxview#history",
    "chrome://browser/content/browser.xhtml",
    "",
    "not a url",
  ];

  try {
    for (const contextPageUrl of nonWatchableUrls) {
      const { conversation, l10nMessages } = makeConversationStub();
      let seededCard = false;
      conversation.addUIToolToCurrentMessage = () => {
        seededCard = true;
      };

      const handled = AgentUI.tryHandleCommand({
        command: AGENT_COMMANDS.WATCH,
        value: `${WATCH_COMMAND} the price drops`,
        contextPageUrl,
        conversation,
      });
      Assert.ok(
        handled,
        `The /watch command is recognized for "${contextPageUrl}"`
      );

      // Give the async handler a chance to run before asserting.
      await Promise.resolve();

      Assert.ok(
        !seededCard,
        `No watch card is seeded for non-watchable page "${contextPageUrl}"`
      );
      Assert.deepEqual(
        l10nMessages,
        ["smartwindow-agent-monitor-page-not-watchable"],
        `The page-not-watchable message is shown for "${contextPageUrl}"`
      );
    }
  } finally {
    await MonitorAgent._resetForTesting();
    await SpecialPowers.popPrefEnv();
  }
});

add_task(async function test_monitor_command_seeds_blank_url_in_full_page() {
  await SpecialPowers.pushPrefEnv({ set: [[PREF_AGENT_ENABLED, true]] });
  await MonitorAgent._resetForTesting();

  try {
    const { conversation, seeded, l10nMessages } = makeConversationStub();
    const handled = AgentUI.tryHandleCommand({
      command: AGENT_COMMANDS.WATCH,
      value: `${WATCH_COMMAND} the price drops`,
      contextPageUrl: "about:firefoxview#history",
      conversation,
      isFullPage: true,
    });
    Assert.ok(handled, "The monitoring command is handled in full page mode");

    const { properties } = await seeded;
    Assert.equal(
      properties.agent.url,
      "",
      "The watch card is seeded without a url in full page mode"
    );
    Assert.deepEqual(
      properties.agent.watchUrls,
      [],
      "The watch card is seeded with no watch urls in full page mode"
    );
    Assert.deepEqual(
      l10nMessages,
      [],
      "The page-not-watchable message is not shown in full page mode"
    );
  } finally {
    await MonitorAgent._resetForTesting();
    await SpecialPowers.popPrefEnv();
  }
});

add_task(
  async function test_create_monitor_requires_daily_or_weekly_schedule() {
    await SpecialPowers.pushPrefEnv({ set: [[PREF_AGENT_ENABLED, true]] });
    await MonitorAgent._resetForTesting();
    const createMonitor = sinon.spy(MonitorAgent, "createMonitor");

    try {
      const { conversation } = makeConversationStub();
      const message = {
        content: {},
        toolUIData: { properties: { agent: {} } },
      };
      const created = await AgentUI.handleCreateMonitor({
        message,
        updateData: {
          condition: "new posts",
          watchUrls: ["https://example.com/watches"],
          schedule: { time: "09:00" },
        },
        conversation,
      });

      Assert.ok(
        !created,
        "A task without a daily or weekly schedule is refused"
      );
      Assert.ok(createMonitor.notCalled, "No monitor is created");
      Assert.equal(message.content.l10nId, undefined, "No confirmation is set");
    } finally {
      createMonitor.restore();
      await MonitorAgent._resetForTesting();
      await SpecialPowers.popPrefEnv();
    }
  }
);

add_task(async function test_created_card_carries_an_active_check_limit() {
  await SpecialPowers.pushPrefEnv({ set: [[PREF_AGENT_ENABLED, true]] });
  await MonitorAgent._resetForTesting();

  const checkLimit = { until: Date.now() + 60000, reason: "throttle" };
  const originalCheckLimit = MonitorAgent.checkLimit;
  MonitorAgent.checkLimit = () => checkLimit;

  const message = {
    id: "monitor-msg-limit",
    content: { body: "" },
    toolUIData: { toolCallId: "monitor-limit", properties: { agent: {} } },
  };
  const conversation = {
    messages: [message],
    emit: () => {},
    updateToolUI: () => {},
    resolvePendingToolConfirmation: () => false,
  };

  try {
    const handled = await AgentUI.handleUpdate(
      {
        messageId: message.id,
        toolCallId: "monitor-limit",
        updateType: "create-watch",
        updateData: {
          condition: "the price drops",
          watchUrls: ["https://example.com/product"],
          schedule: { frequency: "daily", time: "09:00", weekday: 1 },
        },
      },
      conversation
    );
    Assert.ok(handled, "The create update is handled");

    const { agent } = message.toolUIData.properties;
    Assert.deepEqual(
      agent.checkLimit,
      checkLimit,
      "A card created while checks are refused starts out holding the wait"
    );
  } finally {
    MonitorAgent.checkLimit = originalCheckLimit;
    await MonitorAgent._resetForTesting();
    await SpecialPowers.popPrefEnv();
  }
});

add_task(async function test_cancel_monitor_replaces_setup_message() {
  const message = {
    id: "monitor-msg-1",
    content: { body: "Let’s create a task to watch this page." },
    toolUIDraft: { condition: "new posts" },
    toolUIData: { toolCallId: "monitor-1", properties: { agent: {} } },
  };
  let dismissed = false;
  const conversation = {
    messages: [message],
    emit: () => {},
    updateToolUI: (msg, _data, nextUI) => {
      dismissed = nextUI === null;
      msg.toolUIData = null;
    },
    resolvePendingToolConfirmation: () => false,
  };

  const handled = await AgentUI.handleUpdate(
    {
      messageId: "monitor-msg-1",
      toolCallId: "monitor-1",
      updateType: "cancel-watch",
    },
    conversation
  );

  Assert.ok(handled, "The cancel update is handled");
  Assert.ok(dismissed, "The create card is dismissed");
  Assert.equal(
    message.content.l10nId,
    "smartwindow-agent-monitor-canceled",
    "The setup prompt is replaced with the cancellation message"
  );
  Assert.equal(message.content.link, null, "No tasks link is carried over");
  Assert.equal(message.toolUIDraft, null, "The in-progress draft is dropped");

  const l10n = new Localization(["preview/aiWindow.ftl"], true);
  Assert.ok(
    l10n.formatValueSync("smartwindow-agent-monitor-canceled"),
    "The cancellation message has a localized string"
  );
});

/**
 * Waits for the chat to hold text meant for the model, so a follow-up isn't
 * sent before the card action has been recorded.
 *
 * @param {object} chat - Result of openWatchableChat
 * @param {string} text - Text expected in an assistant message body
 */
async function waitForModelText(chat, text) {
  await TestUtils.waitForCondition(
    () =>
      chat.sidebarBrowser.contentDocument
        .querySelector("ai-window")
        ?.conversation?.messages.some(m => m.content?.body?.includes?.(text)),
    `Waiting for the chat to record "${text}" for the model`
  );
}

/**
 * Sends a follow-up message and returns the assistant turns of the history
 * the model received with it.
 *
 * @param {object} chat - Result of openWatchableChat
 * @returns {Promise<string>}
 */
async function sendFollowUp(chat) {
  const { sidebarBrowser, mockEngineManager } = chat;
  await typeInSmartbar(sidebarBrowser, "What did I just do?");
  await submitSmartbar(sidebarBrowser);
  const { request, respond } = await mockEngineManager.captureRequest({
    purpose: "chat",
  });
  respond("Mock reply.");
  return request.args
    .filter(m => m.role === "assistant")
    .map(m => m.content)
    .join("\n");
}

add_task(async function test_model_is_told_about_cancelled_watch_task() {
  const chat = await openWatchableChat();
  try {
    await runWatchCommand(chat, "new posts");
    await SpecialPowers.spawn(chat.aichatBrowser, [], async () => {
      const chatContent = content.document.querySelector("ai-chat-content");
      const card = chatContent.shadowRoot.querySelector("agent-monitor-item");
      card.shadowRoot.querySelector("#cancel-create-button").click();
    });
    const cancelled =
      "User cancelled setting up a watch task. No watch task was created.";
    await waitForModelText(chat, cancelled);

    Assert.stringContains(
      await sendFollowUp(chat),
      cancelled,
      "The model is told the watch task was cancelled"
    );
  } finally {
    await chat.cleanup();
  }
});

add_task(async function test_model_is_told_about_created_watch_task() {
  const chat = await openWatchableChat();
  try {
    await runWatchCommand(chat, "new posts");
    await createFromCard(chat);
    await waitForModelText(chat, "User created a watch task in this chat.");

    const history = await sendFollowUp(chat);
    Assert.stringContains(
      history,
      "User created a watch task in this chat.",
      "The model is told a watch task was created"
    );
    Assert.stringMatches(
      history,
      /- \d{4}-\d{2}-\d{2} \d{2}:\d{2} created/,
      "The task's log starts with its creation"
    );
    Assert.stringContains(
      history,
      '"condition":"new posts"',
      "The model is told what the task watches for"
    );
  } finally {
    await chat.cleanup();
  }
});

add_task(async function test_model_is_told_about_paused_watch_task() {
  const chat = await openWatchableChat();
  try {
    await runWatchCommand(chat, "new posts");
    await createFromCard(chat);
    await SpecialPowers.spawn(chat.aichatBrowser, [], async () => {
      const chatContent = content.document.querySelector("ai-chat-content");
      const card = chatContent.shadowRoot.querySelector("agent-monitor-item");
      // Creating a task from the card expands it, which shows the pause button
      const pauseButton = await ContentTaskUtils.waitForCondition(
        () => card.shadowRoot.querySelector("#pause-button"),
        "Waiting for the pause button"
      );
      pauseButton.click();
    });
    await waitForModelText(chat, " paused");

    Assert.stringMatches(
      await sendFollowUp(chat),
      /- \d{4}-\d{2}-\d{2} \d{2}:\d{2} paused/,
      "The model is told the watch task was paused"
    );
  } finally {
    await chat.cleanup();
  }
});

add_task(async function test_model_is_told_about_edits_made_outside_chat() {
  const chat = await openWatchableChat();
  try {
    await runWatchCommand(chat, "new posts");
    await createFromCard(chat);

    // The tasks page saves edits through MonitorAgent
    const [monitor] = await MonitorAgent.listMonitors();
    await MonitorAgent.updateMonitor(monitor.id, {
      monitorPrompt: "price drops",
    });
    await waitForModelText(chat, "edited (condition)");

    const history = await sendFollowUp(chat);
    Assert.stringMatches(
      history,
      /- \d{4}-\d{2}-\d{2} \d{2}:\d{2} edited \(condition\)/,
      "The model is told the watch task was edited outside the chat"
    );
    Assert.stringContains(
      history,
      '"condition":"price drops"',
      "The model is told the task's new condition"
    );
  } finally {
    await chat.cleanup();
  }
});

// The region gate has to stop the command before it reaches a handler, both
// for an explicit palette pick and for a `/watch` the user typed by hand.
add_task(async function test_watch_command_ignored_in_unsupported_region() {
  await SpecialPowers.pushPrefEnv({
    set: [
      [PREF_AGENT_ENABLED, true],
      [SUPPORTED_REGIONS_PREF, ""],
    ],
  });
  await MonitorAgent._resetForTesting();

  try {
    for (const command of [AGENT_COMMANDS.WATCH, undefined]) {
      const { conversation, assistantMessages, l10nMessages } =
        makeConversationStub();
      let seededCard = false;
      conversation.addUIToolToCurrentMessage = () => {
        seededCard = true;
      };

      const handled = AgentUI.tryHandleCommand({
        command,
        value: `${WATCH_COMMAND} the price drops`,
        contextPageUrl: "https://example.com/product",
        conversation,
      });

      const label = command ? "a palette pick" : "typed input";
      Assert.ok(
        !handled,
        `The /watch command is not handled in an unsupported region for ${label}`
      );

      // The handler is async, so give it a turn before asserting it never ran.
      await Promise.resolve();

      Assert.ok(
        !seededCard,
        `No watch card is seeded in an unsupported region for ${label}`
      );
      Assert.deepEqual(
        [...assistantMessages, ...l10nMessages],
        [],
        `No chat message is added in an unsupported region for ${label}`
      );
    }
  } finally {
    await MonitorAgent._resetForTesting();
    await SpecialPowers.popPrefEnv();
  }
});

// A home region listed alongside others is still supported.
add_task(async function test_watch_command_handled_in_secondary_region() {
  await SpecialPowers.pushPrefEnv({
    set: [
      [PREF_AGENT_ENABLED, true],
      [
        SUPPORTED_REGIONS_PREF,
        JSON.stringify([
          ["US", ["en-*"]],
          ["CA", ["en-*"]],
        ]),
      ],
    ],
  });
  await MonitorAgent._resetForTesting();

  const originalRegion = Region.home;
  Region._setHomeRegion("CA", false);

  try {
    const { conversation, seeded } = makeConversationStub();
    const handled = AgentUI.tryHandleCommand({
      command: AGENT_COMMANDS.WATCH,
      value: `${WATCH_COMMAND} the price drops`,
      contextPageUrl: "https://example.com/product",
      conversation,
    });
    Assert.ok(handled, "The /watch command is handled for a CA home region");

    const { properties } = await seeded;
    Assert.equal(
      properties.agent.condition,
      "the price drops",
      "The watch card is seeded for a CA home region"
    );
  } finally {
    Region._setHomeRegion(originalRegion, false);
    await MonitorAgent._resetForTesting();
    await SpecialPowers.popPrefEnv();
  }
});

// The agent pref gates the command independently of the region.
add_task(async function test_watch_command_ignored_when_agent_disabled() {
  await SpecialPowers.pushPrefEnv({
    set: [
      [PREF_AGENT_ENABLED, false],
      [SUPPORTED_REGIONS_PREF, TEST_MARKETS],
    ],
  });
  await MonitorAgent._resetForTesting();

  try {
    const { conversation } = makeConversationStub();
    let seededCard = false;
    conversation.addUIToolToCurrentMessage = () => {
      seededCard = true;
    };

    const handled = AgentUI.tryHandleCommand({
      command: AGENT_COMMANDS.WATCH,
      value: `${WATCH_COMMAND} the price drops`,
      contextPageUrl: "https://example.com/product",
      conversation,
    });
    Assert.ok(
      !handled,
      "The /watch command is not handled when the agent is disabled"
    );

    await Promise.resolve();
    Assert.ok(
      !seededCard,
      "No watch card is seeded when the agent is disabled"
    );
  } finally {
    await MonitorAgent._resetForTesting();
    await SpecialPowers.popPrefEnv();
  }
});
