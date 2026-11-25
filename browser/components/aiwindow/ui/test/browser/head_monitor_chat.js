/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

/* import-globals-from head.js */

/**
 * Shared helpers for tests that create a watch task from chat with /watch.
 * Loaded explicitly via `Services.scriptloader.loadSubScript` from those test
 * files so they aren't pulled into every test in the directory (unlike
 * head.js).
 */

"use strict";

const { MonitorAgent } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/agents/MonitorAgent.sys.mjs"
);

const { AGENT_COMMANDS } = ChromeUtils.importESModule(
  "chrome://browser/content/aiwindow/modules/AgentCommands.mjs"
);

const { MockEngineManager } = ChromeUtils.importESModule(
  "resource://testing-common/AIWindowTestUtils.sys.mjs"
);

const PREF_AGENT_ENABLED = "browser.smartwindow.agent.enabled";
const WATCH_COMMAND = `/${AGENT_COMMANDS.WATCH}`;

// Trailing slash needed, see promiseNavigateAndLoad in head.js
const WATCHABLE_PAGE = "https://example.com/";

/**
 * Opens a Smart Window with its sidebar on a page that can be watched, with
 * the model replaced by a mock engine.
 *
 * @returns {Promise<object>} The sidebar and chat browsers, the mock engine
 *   manager, and a cleanup function
 */
async function openWatchableChat() {
  await SpecialPowers.pushPrefEnv({ set: [[PREF_AGENT_ENABLED, true]] });
  await MonitorAgent._resetForTesting();
  const restoreSignIn = skipSignIn();
  const mockEngineManager = new MockEngineManager();
  const { win, sidebarBrowser } = await openAIWindowWithSidebar(WATCHABLE_PAGE);
  const aichatBrowser = await getAichatBrowser(sidebarBrowser);
  return {
    sidebarBrowser,
    aichatBrowser,
    mockEngineManager,
    async cleanup() {
      await BrowserTestUtils.closeWindow(win);
      await mockEngineManager.rejectAllRequests();
      mockEngineManager.cleanupMocks();
      restoreSignIn();
      await MonitorAgent._resetForTesting();
      await SpecialPowers.popPrefEnv();
    },
  };
}

/**
 * Runs `/watch <condition>` from the sidebar and waits for the create card.
 *
 * @param {object} chat - Result of openWatchableChat
 * @param {string} condition - Text typed after the command
 */
async function runWatchCommand(chat, condition) {
  const { sidebarBrowser, aichatBrowser } = chat;
  await typeInSmartbar(sidebarBrowser, `${WATCH_COMMAND} ${condition}`);
  await submitSmartbar(sidebarBrowser);
  await SpecialPowers.spawn(aichatBrowser, [], async () => {
    const chatContent = content.document.querySelector("ai-chat-content");
    await ContentTaskUtils.waitForCondition(
      () =>
        chatContent.shadowRoot.querySelector(
          'agent-monitor-item[mode="create"]'
        ),
      "Waiting for the watch task card"
    );
  });
}

/**
 * Optionally picks a schedule on the create card, names the task and clicks
 * Create.
 *
 * @param {object} chat - Result of openWatchableChat
 * @param {object} [schedule] - Schedule to pick, as the card's own values:
 *   '{ frequency: "daily"|"weekly", time: "HH:MM", weekday?: number }'
 */
async function createFromCard(chat, schedule = null) {
  await SpecialPowers.spawn(chat.aichatBrowser, [schedule], async pick => {
    const chatContent = content.document.querySelector("ai-chat-content");
    const card = chatContent.shadowRoot.querySelector("agent-monitor-item");
    await card.updateComplete;
    if (pick) {
      // Frequency, then weekday for weekly schedules, then time.
      const selects = () =>
        Array.from(card.shadowRoot.querySelectorAll("moz-select.form-select"));
      const choose = async (select, value) => {
        // Through the Xray, 'value' would land on the wrapper, not the
        // moz-select's own property.
        select.wrappedJSObject.value = String(value);
        select.dispatchEvent(new content.Event("change", { bubbles: true }));
        await card.updateComplete;
      };
      await choose(selects()[0], pick.frequency);
      if (pick.weekday !== undefined) {
        await choose(selects()[1], pick.weekday);
      }
      await choose(selects().at(-1), pick.time);
    }
    card.shadowRoot.querySelector("moz-input-text").focus();
    EventUtils.sendString("Watches", content);
    card.shadowRoot
      .querySelector('moz-button[data-l10n-id="ai-tasks-alert-create-button"]')
      .click();
    await ContentTaskUtils.waitForCondition(
      () => card.getAttribute("mode") === "display",
      "Waiting for the card to show the created task"
    );
  });
}

/**
 * Reads the task confirmation that replaced the setup message in chat.
 *
 * @param {object} chat - Result of openWatchableChat
 * @returns {Promise<{l10nId: string, l10nArgs: object, text: string}>} The
 *   saved Fluent id and args, and the text the chat shows
 */
async function readConfirmation(chat) {
  const message = await TestUtils.waitForCondition(
    () =>
      chat.sidebarBrowser.contentDocument
        .querySelector("ai-window")
        ?.conversation?.messages.find(m =>
          m.content?.l10nId?.startsWith("smartwindow-agent-monitor-watching")
        ),
    "Waiting for the task confirmation"
  );
  const { l10nId, l10nArgs } = message.content;
  const text = await SpecialPowers.spawn(
    chat.aichatBrowser,
    [l10nId],
    async id => {
      const chatContent = content.document.querySelector("ai-chat-content");
      let shown = "";
      await ContentTaskUtils.waitForCondition(() => {
        const el = Array.from(
          chatContent.shadowRoot.querySelectorAll("ai-chat-message")
        ).find(m => m.wrappedJSObject.messageL10n?.id === id);
        shown = el?.wrappedJSObject.localizedText ?? "";
        return shown;
      }, "Waiting for the confirmation to render");
      return shown;
    }
  );
  return { l10nId, l10nArgs: { ...l10nArgs }, text };
}

/**
 * Formats a schedule's check time the way the confirmation's DATETIME calls
 * do, on the next matching day.
 *
 * @param {number} hour
 * @param {number} minute
 * @param {number} [weekday] - 0 (Sunday) to 6, for weekly schedules
 * @returns {{time: string, weekday: string}}
 */
function formatScheduleTime(hour, minute, weekday) {
  const when = new Date();
  when.setHours(hour, minute, 0, 0);
  if (weekday !== undefined) {
    when.setDate(when.getDate() + ((weekday - when.getDay() + 7) % 7));
  }
  const format = options =>
    new Intl.DateTimeFormat(Services.locale.appLocalesAsBCP47, options).format(
      when
    );
  return {
    time: format({ hour: "numeric", minute: "2-digit" }),
    weekday: format({ weekday: "long" }),
  };
}
