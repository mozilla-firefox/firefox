/**
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

// conversation starter/followup generation functions

import { AuthProvider } from "moz-src:///browser/components/aiwindow/models/AuthProvider.sys.mjs";
import { MESSAGE_ROLE } from "moz-src:///browser/components/aiwindow/ui/modules/ChatStore.sys.mjs";
import { MemoriesManager } from "moz-src:///browser/components/aiwindow/models/memories/MemoriesManager.sys.mjs";
import { sanitizeUntrustedContent } from "moz-src:///browser/components/aiwindow/models/ChatUtils.sys.mjs";

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  renderPrompt: "moz-src:///browser/components/aiwindow/models/Utils.sys.mjs",
  MODEL_FEATURES: "moz-src:///browser/components/aiwindow/models/Utils.sys.mjs",
  buildConversation:
    "moz-src:///browser/components/aiwindow/models/PromptLoader.sys.mjs",
  loadPrompt:
    "moz-src:///browser/components/aiwindow/models/PromptLoader.sys.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "console", () =>
  console.createInstance({
    logModule: "ConversationSuggestions",
    maxLogLevelPref: "browser.smartwindow.conversation.logLevel",
  })
);

// Max number of memories to include in prompts
const MAX_NUM_MEMORIES = 8;

/**
 * Helper to trim conversation history to recent messages, dropping empty messages, tool calls and responses
 *
 * @param {Array} messages - Array of chat messages
 * @param {number} maxMessages - Max number of messages to keep (default 15)
 * @returns {Array} Trimmed array of user/assistant messages
 */
export function trimConversation(messages, maxMessages = 15) {
  const out = [];

  for (const m of messages) {
    if (
      (m.role === MESSAGE_ROLE.USER || m.role === MESSAGE_ROLE.ASSISTANT) &&
      m.content &&
      m.content.trim()
    ) {
      const roleString = m.role === MESSAGE_ROLE.USER ? "user" : "assistant";
      out.push({ role: roleString, content: m.content });
    }
  }

  return out.slice(-maxMessages);
}

/**
 * Helper to add memories to base prompt if applicable
 *
 * @param {string} base - base prompt
 * @param {string} conversationMemoriesPrompt - the memories prompt template
 * @returns {Promise<string>} - prompt with memories added if applicable
 */
export async function addMemoriesToPrompt(base, conversationMemoriesPrompt) {
  let memorySummaries =
    await MemoriesGetterForSuggestionPrompts.getMemorySummariesForPrompt(
      MAX_NUM_MEMORIES
    );
  if (memorySummaries.length) {
    const memoriesBlock = memorySummaries.map(s => `- ${s}`).join("\n");
    const memoryPrompt = lazy.renderPrompt(conversationMemoriesPrompt, {
      memories: memoriesBlock,
    });
    return `${base}\n${memoryPrompt}`;
  }
  return base;
}

/**
 * Cleans inference output into array of prompts
 *
 * @param {*} result - Inference output result object
 * @returns {Array<string>} - Cleaned array of prompts
 */
export function cleanInferenceOutput(result) {
  const text = (result.finalOutput || "").trim();
  const lines = text
    .split(/\n+/)
    .map(l => l.trim())
    .filter(Boolean);

  const prompts = lines
    .map(line => line.replace(/^[-*\d.)\[\]]+\s*/, ""))
    .filter(p => p.length)
    .map(p => p.replace(/\.$/, "").replace(/^[^:]*:\s*/, ""));
  return prompts;
}

/**
 * Format object to JSON string safely
 *
 * @param {*} obj - Object to format
 * @returns {string} JSON string or string representation
 */
const formatJson = obj => {
  try {
    return JSON.stringify(obj);
  } catch {
    return String(obj);
  }
};

export const NewTabStarterGenerator = {
  writingPrompts: [
    "aiwindow-starter-writing-first-draft",
    "aiwindow-starter-writing-improve",
    "aiwindow-starter-writing-proofread",
  ],

  planningPrompts: [
    "aiwindow-starter-planning-simplify",
    "aiwindow-starter-planning-brainstorm",
    "aiwindow-starter-planning-plan",
  ],

  // TODO: discuss with design about updating phrasing to "pages" instead of "tabs"
  browsingPrompts: [
    {
      id: "aiwindow-starter-browsing-history",
      minTabs: 0,
      needsHistory: true,
    },
    {
      id: "aiwindow-starter-browsing-summarize",
      minTabs: 1,
      needsHistory: false,
    },
    {
      id: "aiwindow-starter-browsing-compare",
      minTabs: 2,
      needsHistory: false,
    },
  ],

  getRandom(arr) {
    return arr[Math.floor(Math.random() * arr.length)];
  },

  /**
   * Generate conversation starter prompt l10n ids based on number of open tabs and browsing history prefs.
   * "places.history.enabled" covers "Remember browsing and download history" while
   * "browser.privatebrowsing.autostart" covers "Always use private mode" and "Never remember history".
   * We need to check both prefs to cover all cases where history can be disabled.
   *
   * @param {number} tabCount - number of open tabs
   * @returns {Array<{l10nId: string, type: string}>} suggestion objects with l10nId and type
   */
  async getPrompts(tabCount) {
    const historyEnabled = Services.prefs.getBoolPref("places.history.enabled");
    const privateBrowsing = Services.prefs.getBoolPref(
      "browser.privatebrowsing.autostart"
    );
    const validBrowsingPrompts = this.browsingPrompts.filter(
      p =>
        tabCount >= p.minTabs &&
        (!p.needsHistory || (historyEnabled && !privateBrowsing))
    );

    const writingPrompt = this.getRandom(this.writingPrompts);
    const planningPrompt = this.getRandom(this.planningPrompts);
    const browsingPrompt = validBrowsingPrompts.length
      ? this.getRandom(validBrowsingPrompts)
      : null;

    const ids = [writingPrompt, planningPrompt];
    if (browsingPrompt) {
      ids.push(browsingPrompt.id);
    }

    return ids.map(l10nId => ({ l10nId, type: "chat" }));
  },
};

/**
 * Generates conversation starter prompts based on tab context + (optional) user memories
 *
 * @param {Array} contextTabs - Array of tab objects with title, url, favicon
 * @param {number} n - Number of suggestions to generate (default 6)
 * @param {boolean} useMemories - Whether to include user memories in prompt (default false)
 * @param {string | null} flowId - Flow ID for correlating with firefox_ai_runtime telemetry
 * @param {AbortSignal} signal - Signal to cancel the inference request
 * @returns {Promise<Array>} Array of {text, type} suggestion objects
 */
export async function generateConversationStartersSidebar(
  contextTabs = [],
  n = 2,
  useMemories = false,
  flowId = null,
  signal = new AbortController().signal
) {
  try {
    const today = new Date().toISOString().slice(0, 10);

    // Format current tab (first in context or empty)
    const currentTab = contextTabs.length
      ? formatJson({
          title: sanitizeUntrustedContent(contextTabs[0].title),
          url: contextTabs[0].url,
        })
      : "No current tab";

    // Format opened tabs
    let openedTabs;
    if (contextTabs.length >= 1) {
      openedTabs =
        contextTabs.length === 1
          ? "Only current tab is open"
          : formatJson(
              contextTabs.slice(1).map(t => ({
                title: sanitizeUntrustedContent(t.title),
                url: t.url,
              }))
            );
    } else {
      openedTabs = "No tabs available";
    }
    // Data extracted into currentTab/openedTabs strings; release the
    // caller-allocated array so it cannot prevent the window from being GC'd
    // while awaiting inference.
    contextTabs = null;

    const conversation = await lazy.buildConversation(
      lazy.MODEL_FEATURES.CONVERSATION_SUGGESTIONS_SIDEBAR_STARTER,
      { flowId }
    );
    const [
      { prompt: conversationStarterSystemPrompt },
      { prompt: conversationStarterPrompt },
      { prompt: assistantLimitations },
    ] = await Promise.all([
      lazy.loadPrompt(lazy.MODEL_FEATURES.CONVERSATION_STARTERS_SIDEBAR_SYSTEM),
      lazy.loadPrompt(
        lazy.MODEL_FEATURES.CONVERSATION_SUGGESTIONS_SIDEBAR_STARTER
      ),
      lazy.loadPrompt(
        lazy.MODEL_FEATURES.CONVERSATION_SUGGESTIONS_ASSISTANT_LIMITATIONS
      ),
    ]);

    // Base template
    const base = lazy.renderPrompt(conversationStarterPrompt, {
      current_tab: currentTab,
      open_tabs: openedTabs,
      n: String(n),
      date: today,
      locale: Services.locale.appLocaleAsBCP47,
      assistant_limitations: assistantLimitations,
    });

    let filled = base;
    if (useMemories) {
      const { prompt: conversationMemoriesPrompt } = await lazy.loadPrompt(
        lazy.MODEL_FEATURES.CONVERSATION_SUGGESTIONS_MEMORIES
      );
      filled = await addMemoriesToPrompt(base, conversationMemoriesPrompt);
    }

    conversation.setSystemMessage(conversationStarterSystemPrompt);
    conversation.addUserMessage(filled);

    const token = await AuthProvider.getToken();
    signal.throwIfAborted();

    let runPromise = conversation.run({ token });
    runPromise = Promise.race([
      runPromise,
      new Promise((_, reject) => {
        if (signal.aborted) {
          reject(signal.reason);
        } else {
          signal.addEventListener("abort", () => reject(signal.reason), {
            once: true,
          });
        }
      }),
    ]);
    const result = await runPromise;

    const prompts = cleanInferenceOutput(result);

    return prompts.slice(0, n).map(t => ({ text: t, type: "chat" }));
  } catch (e) {
    if (e.name !== "AbortError") {
      lazy.console.warn("[sidebar-conversation-starters] failed:", e);
    }
    return [];
  }
}

/**
 * Generates followup prompt suggestions based on conversation history
 *
 * @param {Array} conversationHistory - Array of chat messages
 * @param {object} currentTab - Current tab object with title, url
 * @param {number} n - Number of suggestions to generate (default 6)
 * @param {boolean} useMemories - Whether to include user memories in prompt (default false)
 * @param {string | null} flowId - Flow ID for correlating with firefox_ai_runtime telemetry
 * @returns {Promise<Array>} Array of {text, type} suggestion objects
 */
export async function generateFollowupPrompts(
  conversationHistory,
  currentTab,
  n = 2,
  useMemories = false,
  flowId = null
) {
  try {
    const today = new Date().toISOString().slice(0, 10);
    const convo = trimConversation(conversationHistory);
    const currentTabStr =
      currentTab && Object.keys(currentTab).length
        ? formatJson({
            title: sanitizeUntrustedContent(currentTab.title),
            url: currentTab.url,
          })
        : "No tab";

    const conversation = await lazy.buildConversation(
      lazy.MODEL_FEATURES.CONVERSATION_SUGGESTIONS_FOLLOWUP,
      { flowId }
    );
    const [
      { prompt: conversationFollowupPrompt },
      { prompt: assistantLimitationsFollowup },
    ] = await Promise.all([
      lazy.loadPrompt(lazy.MODEL_FEATURES.CONVERSATION_SUGGESTIONS_FOLLOWUP),
      lazy.loadPrompt(
        lazy.MODEL_FEATURES.CONVERSATION_SUGGESTIONS_ASSISTANT_LIMITATIONS
      ),
    ]);

    const base = lazy.renderPrompt(conversationFollowupPrompt, {
      current_tab: currentTabStr,
      conversation: formatJson(convo),
      n: String(n),
      date: today,
      assistant_limitations: assistantLimitationsFollowup,
    });

    let filled = base;
    if (useMemories) {
      const { prompt: conversationMemoriesPrompt } = await lazy.loadPrompt(
        lazy.MODEL_FEATURES.CONVERSATION_SUGGESTIONS_MEMORIES
      );
      filled = await addMemoriesToPrompt(base, conversationMemoriesPrompt);
    }

    conversation.setSystemMessage(
      "Return only the requested suggestions, one per line."
    );
    conversation.addUserMessage(filled);

    const result = await conversation.run({
      token: await AuthProvider.getToken(),
    });

    const prompts = cleanInferenceOutput(result);

    return prompts.slice(0, n).map(t => ({ text: t, type: "chat" }));
  } catch (e) {
    lazy.console.warn("[ConversationSuggestions][followup-prompts] failed:", e);
    return [];
  }
}

export const MemoriesGetterForSuggestionPrompts = {
  /**
   * Gets the requested number of unique memory summaries for prompt inclusion
   *
   * @param {number} maxMemories - Max number of memories to return (default MAX_NUM_MEMORIES)
   * @returns {Promise<Array>} Array of string memory summaries
   */

  async getMemorySummariesForPrompt(maxMemories) {
    const memorySummaries = [];
    const memoryEntries = (await MemoriesManager.getAllMemories()) || {};
    const seenSummaries = new Set();

    for (const { memory_summary } of memoryEntries) {
      const summaryText = String(memory_summary ?? "").trim();
      if (!summaryText) {
        continue;
      }
      const lower = summaryText.toLowerCase();
      if (seenSummaries.has(lower)) {
        continue;
      }
      seenSummaries.add(lower);
      memorySummaries.push(summaryText);
      if (memorySummaries.length >= maxMemories) {
        break;
      }
    }

    return memorySummaries;
  },
};
