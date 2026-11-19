/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

/* import-globals-from head.js */

/**
 * Shared helpers for the "Write it for me" tests, which press the real button
 * in a real host and answer from the mocked model. Loaded explicitly via
 * `Services.scriptloader.loadSubScript` from those test files so they aren't
 * pulled into every test in the directory (unlike head.js).
 */

/**
 * @type {import("../AIWindowTestUtils.sys.mjs")}
 */
const { MockEngineManager, MockMemoriesRetrieval } = ChromeUtils.importESModule(
  "resource://testing-common/AIWindowTestUtils.sys.mjs"
);

/**
 * @type {import("../../../../../../toolkit/components/ml/tests/MLTestUtils.sys.mjs")}
 */
const { MLTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/MLTestUtils.sys.mjs"
);

const { PURPOSES } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/Utils.sys.mjs"
);

const { MonitorAgent } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/agents/MonitorAgent.sys.mjs"
);

const {
  CHAT_ROLE_LABELS,
  MAX_CONDITION_LENGTH,
  MAX_MEMORIES,
  MAX_MEMORY_LENGTH,
  MAX_TITLE_LENGTH,
  describeLanguage,
} = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/agents/MonitorSetup.sys.mjs"
);

const { AGENT_COMMANDS } = ChromeUtils.importESModule(
  "chrome://browser/content/aiwindow/modules/AgentCommands.mjs"
);

const { sanitizeUntrustedContent } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/ChatUtils.sys.mjs"
);

const { MemoryStore } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/services/MemoryStore.sys.mjs"
);

const SUGGEST_REGION = "US";

const PRODUCT_TITLE = "Trail Runner 2 shoes";
const PRODUCT_PRICE = "$129.99";
// Shares a word with the product page's title, which is how the mock embedder
// finds it.
const SHOES_MEMORY = "Wears size ten shoes";

// Fluent ids of what the button and its error line read
const SUGGEST_L10N_IDS = {
  first: "ai-tasks-alert-suggest-button",
  generating: "ai-tasks-alert-suggest-generating-button",
  retry: "ai-tasks-alert-suggest-retry-button",
  failed: "ai-tasks-alert-error-suggestion-failed",
};

add_setup(async function setup_monitor_suggest() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.smartwindow.agent.enabled", true],
      ["browser.smartwindow.agent.toolbar.enabled", true],
      [
        "browser.smartwindow.agent.supportedRegions",
        JSON.stringify([[SUGGEST_REGION, ["en-*"]]]),
      ],
    ],
  });
  // Pin the home region so the region gate doesn't depend on where the test
  // runs.
  const originalRegion = Region.home;
  Region._setHomeRegion(SUGGEST_REGION, false);
  AIWindow._updateMonitorWidgetRegistration();
  await MonitorAgent._resetForTesting();

  registerCleanupFunction(async () => {
    Region._setHomeRegion(originalRegion, false);
    await MonitorAgent._resetForTesting();
    for (const pref of [
      "browser.smartwindow.monitorStore.removeDatabaseOnStartup",
      "browser.smartwindow.agent.monitorAttention",
      "browser.smartwindow.lastSmartWindowUsageTime",
      "browser.smartwindow.lastLLMTelemetryRunTime",
    ]) {
      Services.prefs.clearUserPref(pref);
    }
  });
});

const SUGGESTION = {
  title: "Trail Runner 2 size 10",
  condition: "Size 10 is back in stock",
};

/**
 * Serves a product page and opens it in a new tab of the window.
 *
 * @param {Window} win
 * @returns {Promise<{url: string, cleanup: () => Promise<void>}>}
 */
function openProductPage(win) {
  const { html } = MLTestUtils.serveHTMLInTab({ browser: win.gBrowser });
  return html`
    <title>${PRODUCT_TITLE}</title>
    <h1>${PRODUCT_TITLE}</h1>
    <p>Price: ${PRODUCT_PRICE}</p>
    <p>Size 10 is sold out</p>
  `;
}

/**
 * Waits for the request a press of "Write it for me" sends to the model.
 *
 * @param {MockEngineManager} mockEngineManager
 * @returns {Promise<{request: object, system: string, user: string, respond: Function, reject: Function}>}
 *   The request with its system and user messages, and how to answer it
 */
async function captureSuggestionRequest(mockEngineManager) {
  const captured = await mockEngineManager.captureRequest({
    purpose: PURPOSES.MONITOR,
  });
  return {
    ...captured,
    system: captured.request.args[0].content,
    user: captured.request.args.at(-1).content,
  };
}

/**
 * @param {string} message - The user message of a suggestion request
 * @param {string} tag - A block of the user-data prompt in MOCK_RS_RECORDS
 * @returns {?string} What the block holds, or null when it was left out
 */
function promptBlock(message, tag) {
  const open = `<${tag}>\n`;
  const start = message.indexOf(open);
  const end = message.lastIndexOf(`\n</${tag}>`);
  return start === -1 || end === -1
    ? null
    : message.slice(start + open.length, end);
}

/**
 * @param {string} text
 * @param {string} part
 * @returns {number} How many times the part appears in the text
 */
function countOf(text, part) {
  return text.split(part).length - 1;
}

/**
 * @param {MockEngineManager} mockEngineManager
 * @returns {number} How many suggestion requests are waiting for an answer
 */
function pendingSuggestionRequests(mockEngineManager) {
  return mockEngineManager.engines.get(PURPOSES.MONITOR)?.runRequests.size ?? 0;
}

// A chat turn and what a suggestion request holds of it
const WATCH_COMMAND = `/${AGENT_COMMANDS.WATCH}`;

const USER_LINE = "is the trail runner ever going to restock in size 10?";
const ASSISTANT_LINE =
  "It is sold out in size 10 right now. Sizes 9 and 11 are in stock.";
const CHAT_LINES = [
  `${CHAT_ROLE_LABELS.user}: ${USER_LINE}`,
  `${CHAT_ROLE_LABELS.assistant}: ${ASSISTANT_LINE}`,
].join("\n");

/**
 * Opens a Smart Window with the chat sidebar next to the product page.
 *
 * @param {Function} task
 */
async function withSidebarChat(task) {
  const mockEngineManager = new MockEngineManager();
  const { win, sidebarBrowser } = await openAIWindowWithSidebar();
  const page = await openProductPage(win);
  try {
    await task({ win, sidebarBrowser, page, mockEngineManager });
  } finally {
    await page.cleanup();
    await BrowserTestUtils.closeWindow(win);
    await mockEngineManager.rejectAllRequests();
    mockEngineManager.cleanupMocks();
  }
}

/**
 * @param {MozBrowser} browser - The browser holding the ai-window
 * @param {string} message
 */
async function sendInSmartbar(browser, message) {
  await typeInSmartbar(browser, message);
  await submitSmartbar(browser);
}

/**
 * Has the user ask something and the mocked model answer it.
 *
 * @param {MozBrowser} browser - The browser holding the ai-window
 * @param {MockEngineManager} mockEngineManager
 */
async function chatTurn(browser, mockEngineManager) {
  await sendInSmartbar(browser, USER_LINE);
  await mockEngineManager.respondTo({
    purpose: PURPOSES.CHAT,
    response: ASSISTANT_LINE,
  });
  await mockEngineManager.respondTo({
    purpose: PURPOSES.TITLE_GENERATION,
    response: "Trail runner restock",
  });
}

/**
 * Acts on the newest create card in chat and reads it back.
 *
 * @param {MozBrowser} browser - The browser holding the ai-window
 * @param {object} [options]
 * @param {"press"|"cancel"} [options.action] - The button to click
 * @param {string} [options.filledWith] - After the click, wait for the card to
 *   take a suggestion with this name
 * @param {boolean} [options.failed] - After the click, wait for the error line
 * @returns {Promise<object>} What the card shows
 */
async function createCard(
  browser,
  { action = null, filledWith = null, failed = false } = {}
) {
  const aichatBrowser = await getAichatBrowser(browser);
  return SpecialPowers.spawn(
    aichatBrowser,
    [{ action, filledWith, failed, ids: SUGGEST_L10N_IDS }],
    async options => {
      const chatContent = content.document.querySelector("ai-chat-content");
      const newestCard = () =>
        Array.from(
          chatContent.shadowRoot.querySelectorAll(
            'agent-monitor-item[mode="create"]'
          )
        ).at(-1);
      await ContentTaskUtils.waitForMutationCondition(
        chatContent.shadowRoot,
        { childList: true, subtree: true, attributes: true },
        newestCard
      );
      const card = newestCard();
      await card.updateComplete;
      const field = selector => card.shadowRoot.querySelector(selector);
      const [first, generating, retry, failedText] =
        await content.document.l10n.formatValues(
          Object.values(options.ids).map(id => ({ id }))
        );
      const errorText = () =>
        field(".suggest-status .error-message")?.textContent.trim();

      const selectors = {
        press: ".suggest-button",
        cancel: "#cancel-create-button",
      };
      if (options.action) {
        const drafted =
          options.filledWith &&
          ContentTaskUtils.waitForEvent(
            card,
            "agent-monitor-item:draft-change",
            false,
            event => event.detail.draft?.monitorName === options.filledWith,
            true
          );
        field(selectors[options.action]).click();
        await drafted;
        if (options.failed) {
          await ContentTaskUtils.waitForMutationCondition(
            card.shadowRoot,
            { childList: true, subtree: true, characterData: true },
            () => errorText() === failedText
          );
        }
        await card.updateComplete;
      }

      const button = field(".suggest-button");
      if (options.filledWith) {
        await ContentTaskUtils.waitForMutationCondition(
          button,
          { attributes: true, childList: true, characterData: true },
          () => button.textContent.trim() === retry
        );
      }
      return {
        hasButton: !!button,
        label: button?.textContent.trim(),
        busy: !!button?.hasAttribute("disabled"),
        name: field(".monitor-name-input")?.wrappedJSObject.value,
        condition: field(".monitor-condition-input")?.wrappedJSObject.value,
        error: errorText(),
        labels: { first, generating, retry, failed: failedText },
      };
    }
  );
}
