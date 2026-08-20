/**
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

// conversation starter/followup generation functions

import { openAIEngine } from "moz-src:///browser/components/aiwindow/models/openAIEngine.sys.mjs";
import { MESSAGE_ROLE } from "moz-src:///browser/components/aiwindow/ui/modules/ChatStore.sys.mjs";
import { MemoriesManager } from "moz-src:///browser/components/aiwindow/models/memories/MemoriesManager.sys.mjs";
import { sanitizeUntrustedContent } from "moz-src:///browser/components/aiwindow/models/ChatUtils.sys.mjs";

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  renderPrompt: "moz-src:///browser/components/aiwindow/models/Utils.sys.mjs",
  MODEL_FEATURES: "moz-src:///browser/components/aiwindow/models/Utils.sys.mjs",
  parseAndExtractJSON:
    "moz-src:///browser/components/aiwindow/models/Utils.sys.mjs",
  indexInferenceResultsById:
    "moz-src:///browser/components/aiwindow/models/Utils.sys.mjs",
  HISTORY:
    "moz-src:///browser/components/aiwindow/models/memories/MemoriesConstants.sys.mjs",
  MEMORY_SENSITIVITY_CATEGORY_NOT_SENSITIVE:
    "moz-src:///browser/components/aiwindow/models/memories/MemoriesConstants.sys.mjs",
  SESSION:
    "moz-src:///browser/components/aiwindow/models/memories/MemoriesConstants.sys.mjs",
  MEMORY_FILTER_COMPARATOR:
    "moz-src:///browser/components/aiwindow/services/MemoryStoreConstants.sys.mjs",
  buildConversation:
    "moz-src:///browser/components/aiwindow/models/PromptLoader.sys.mjs",
  loadPrompt:
    "moz-src:///browser/components/aiwindow/models/PromptLoader.sys.mjs",
  getHistorySourceIdsFromMemory:
    "moz-src:///browser/components/aiwindow/models/memories/MemoriesHistorySource.sys.mjs",
  resolveUrlsForMemories:
    "moz-src:///browser/components/aiwindow/models/memories/MemoriesHistorySource.sys.mjs",
  getConversationSourceIdsFromMemory:
    "moz-src:///browser/components/aiwindow/models/memories/MemoriesChatSource.sys.mjs",
  getConversationsById:
    "moz-src:///browser/components/aiwindow/models/memories/MemoriesChatSource.sys.mjs",
  MESSAGE_LENGTH_THRESHOLD:
    "moz-src:///browser/components/aiwindow/models/memories/MemoriesChatSource.sys.mjs",
  ROLE_LABEL:
    "moz-src:///browser/components/aiwindow/models/Conversation.sys.mjs",
  ChatConversation:
    "moz-src:///browser/components/aiwindow/ui/modules/ChatConversation.sys.mjs",
  SYSTEM_PROMPT_TYPE:
    "moz-src:///browser/components/aiwindow/ui/modules/ChatEnums.sys.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "console", () =>
  console.createInstance({
    prefix: "ConversationSuggestions",
    maxLogLevelPref: "browser.smartwindow.conversation.logLevel",
  })
);

let _savedLoadPromptDescriptor = null;
export function _setLoadPromptForTesting(fn) {
  if (fn !== null) {
    _savedLoadPromptDescriptor = Object.getOwnPropertyDescriptor(
      lazy,
      "loadPrompt"
    );
    lazy.loadPrompt = fn;
  } else if (_savedLoadPromptDescriptor) {
    // eslint-disable-next-line mozilla/valid-lazy
    Object.defineProperty(lazy, "loadPrompt", _savedLoadPromptDescriptor);
    _savedLoadPromptDescriptor = null;
  }
}

let _savedBuildConversationDescriptor = null;
export function _setBuildConversationForTesting(fn) {
  if (fn !== null) {
    _savedBuildConversationDescriptor = Object.getOwnPropertyDescriptor(
      lazy,
      "buildConversation"
    );
    lazy.buildConversation = fn;
  } else if (_savedBuildConversationDescriptor) {
    // eslint-disable-next-line mozilla/valid-lazy
    Object.defineProperty(
      lazy,
      "buildConversation",
      _savedBuildConversationDescriptor
    );
    _savedBuildConversationDescriptor = null;
  }
}

let _savedGetConversationsByIdDescriptor = null;
export function _setGetConversationsByIdForTesting(fn) {
  if (fn !== null) {
    _savedGetConversationsByIdDescriptor = Object.getOwnPropertyDescriptor(
      lazy,
      "getConversationsById"
    );
    lazy.getConversationsById = fn;
  } else if (_savedGetConversationsByIdDescriptor) {
    // eslint-disable-next-line mozilla/valid-lazy
    Object.defineProperty(
      lazy,
      "getConversationsById",
      _savedGetConversationsByIdDescriptor
    );
    _savedGetConversationsByIdDescriptor = null;
  }
}

// Max number of memories to include in prompts
const MAX_NUM_MEMORIES = 8;

// Max number of memories to surface as "Pick up where you left off" pills.
// The New Tab UI maintains a fixed pill count regardless; this only bounds how
// many memory-backed candidates the models layer returns.
export const MAX_NUM_MEMORIES_FOR_RESUME_ACTIVITY = 2;
// Max number of URLs to include per memory in the "Pick up where you left off" prompt.
export const MAX_NUM_URLS_PER_MEMORY = 10;

// Max number of messages to include per conversation in the "Pick up where you left off" prompt.
export const MAX_MESSAGES_PER_CONVERSATION_FOR_RESUME_ACTIVITY_PROMPT = 3;

// Single-slot cache for resume activity suggestion.
// This avoids re-querying additional LLM, places/chat DB calls for each suggestion request between memory generation runs.
const _resumeActivityCache = new Map();

export function _clearResumeActivityCacheForTesting() {
  _resumeActivityCache.clear();
}

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
 * Unpacks JSON Array output from inference response into array of results
 *
 * @param {*} response - Inference output response object
 * @returns {Array} - Array of parsed JSON objects, or empty array if parsing fails
 */
export function unpackJsonArrayOutput(response) {
  let parsed = [];
  try {
    parsed = lazy.parseAndExtractJSON(response, []);
    if (!Array.isArray(parsed)) {
      lazy.console.warn(
        "Inference output is not an array, returning empty array"
      );
      return [];
    }
  } catch (e) {
    lazy.console.warn(
      "Failed to parse inference output into expected schema",
      e
    );
    parsed = [];
  }
  parsed = parsed.filter(
    item =>
      typeof item === "object" &&
      item !== null &&
      item.id != null &&
      typeof item.headline === "string" &&
      typeof item.status === "string"
  );
  return parsed;
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

    const fxAccountToken = await openAIEngine.getFxAccountToken();
    signal.throwIfAborted();

    let runPromise = conversation.run({ fxAccountToken });
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
      fxAccountToken: await openAIEngine.getFxAccountToken(),
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

/**
 * Gets memories suitable for Resume Activity suggestions.
 * Selects memories that are not sensitive and have associated browsing history.
 * Sorted by frecency and updated_at, returning the most frecent/recently updated memories.
 *
 * @param {number} count - Maximum number of memories to return
 * @returns {Promise<Array<object>>}
 */
export async function getMemoriesForResumeActivityConversationStarter(
  count = MAX_NUM_MEMORIES_FOR_RESUME_ACTIVITY
) {
  let attributeFilters = [
    {
      field: "sensitivity_category",
      comparator: lazy.MEMORY_FILTER_COMPARATOR.EQUAL_TO,
      value: lazy.MEMORY_SENSITIVITY_CATEGORY_NOT_SENSITIVE,
    },
    {
      field: "sources",
      comparator: lazy.MEMORY_FILTER_COMPARATOR.SOME,
      value: [lazy.HISTORY, lazy.SESSION],
    },
  ];
  let memories = await MemoriesManager.getMemoriesByAttribute(attributeFilters);

  // Filter out memories that don't have any associated history.
  // The case of HISTORY as a source, but no history_source_ids,
  // come from v1 memories, before lineage was tracked.
  memories = memories.filter(memory => {
    const sourceIds = memory?.source_ids ?? {};
    const hasHistory =
      Array.isArray(sourceIds.history_source_ids) &&
      !!sourceIds.history_source_ids.length;
    return hasHistory;
  });

  // Re-sort by frecency (decreasing) and updated_at (most recent first)
  memories.sort(
    (a, b) =>
      (b.frecency ?? 0) - (a.frecency ?? 0) ||
      (b.updated_at ?? 0) - (a.updated_at ?? 0)
  );

  // Slice to requested count
  return memories.slice(0, count);
}

/**
 * Attaches resolved Places URLs/titles to a single memory, dropping unresolved
 * URLs and keeping the most recently visited up to maxUrlsPerMemory.
 *
 * @param {object} memory - Memory carrying history place hashes
 * @param {Map<number, object>} urlsByHash - Places data keyed by URL hash
 * @param {number} maxUrlsPerMemory - Max URLs to keep per memory
 * @returns {{memory: object, urls: Array<object>}}
 */
function attachUrlsToMemory(memory, urlsByHash, maxUrlsPerMemory) {
  return {
    memory,
    urls: lazy
      .getHistorySourceIdsFromMemory(memory)
      .map(urlHash => urlsByHash.get(urlHash))
      .filter(Boolean)
      .sort((a, b) => a.lastVisitDate - b.lastVisitDate)
      .slice(-maxUrlsPerMemory),
  };
}

/**
 * Format chats for resume activity prompt
 *
 * @param {Array<object>} memories Array of memory objects
 * @returns {Promise<Map<string, object>>} Map of conversationId to formatted conversation object
 */
async function formatChatsForResumeActivityPrompt(memories) {
  const conversationIds = [
    ...new Set(
      memories.flatMap(memory =>
        lazy.getConversationSourceIdsFromMemory(memory)
      )
    ),
  ];
  const conversations = await lazy.getConversationsById(conversationIds);
  const chatsById = new Map();
  for (const conversation of conversations) {
    chatsById.set(conversation.id, {
      ...conversation,
      formattedMessagesForResumeActivity: conversation.messages
        .filter(
          message =>
            ![MESSAGE_ROLE.SYSTEM, MESSAGE_ROLE.TOOL].includes(message.role)
        )
        .slice(-MAX_MESSAGES_PER_CONVERSATION_FOR_RESUME_ACTIVITY_PROMPT)
        .map(message => {
          const role = lazy.ROLE_LABEL[message.role];
          let bodyOrContent = message.content?.body ?? message.content;
          if (
            bodyOrContent &&
            bodyOrContent.length > lazy.MESSAGE_LENGTH_THRESHOLD
          ) {
            bodyOrContent = bodyOrContent.substring(
              0,
              lazy.MESSAGE_LENGTH_THRESHOLD
            );
          }
          return `[role: ${role}] ${bodyOrContent}`;
        })
        .join("\n"),
    });
  }
  return chatsById;
}

/**
 * Builds the prompt text block describing a single memory and its associated
 * pages and chats. When provided, `index` doubles as the id echoed back by the model.
 *
 * @param {{memory: object, urls: Array<object>}} entry - Memory and its URLs
 * @param {number|undefined} index - Optional position of the memory, used as its stable id
 * @param {Map<string, object>} chatsById - Formatted chats keyed by conversation id
 * @returns {string} Prompt block for the memory
 */
function buildMemoryInputBlock({ memory, urls }, index, chatsById) {
  const pages = urls
    .map(url => `  - ${sanitizeUntrustedContent(url.title)}`)
    .join("\n");

  const chats = lazy
    .getConversationSourceIdsFromMemory(memory)
    .map(conversationId => chatsById.get(conversationId))
    .filter(Boolean)
    .sort((a, b) => a.updatedDate - b.updatedDate)
    .map(
      ({ title, formattedMessagesForResumeActivity }) =>
        ` - title: ${title}
            messages:
            ${formattedMessagesForResumeActivity
              .split("\n")
              .map(line => `     ${line}`)
              .join("\n")}`
    );

  const header = Number.isInteger(index)
    ? `-------------------------\nid: ${index}\n`
    : "";

  let block = `${header}memory_summary: ${memory.memory_summary}
frecency: ${memory.frecency ?? 0}
reasoning: ${memory.reasoning ?? ""}
pages:\n${pages}`;

  if (chats.length) {
    block += `\nchats:\n${chats.join("\n")}`;
  }
  return block;
}

/**
 * Returns cached resume activity for the input watermark.
 *
 * @param {number} watermark
 * @returns {Array<object>|Promise<Array<object>>|undefined} Cached result,
 *   in-flight request, or undefined if not available
 */
function _getCachedResumeActivity(watermark) {
  const entry = _resumeActivityCache.get(watermark);
  if (!entry) {
    return undefined;
  }
  return "result" in entry ? entry.result : entry.inFlight;
}

/**
 * Removes cached suggestions for memories that have since been deleted.
 *
 * @param {Array<object>} result Cached resume activity suggestions
 * @returns {Promise<Array<object>>} Suggestions for current memories
 */
async function filterDeletedMemoriesFromResumeActivity(result) {
  const currentMemoryIds = new Set(
    (await MemoriesManager.getAllMemories({ includeSoftDeleted: false })).map(
      memory => memory.id
    )
  );
  const filteredResult = result.filter(({ memory }) =>
    currentMemoryIds.has(memory.id)
  );
  return filteredResult.length === result.length ? result : filteredResult;
}

/**
 * @returns {Promise<Array<object>>} Generated resume activity suggestions
 */
async function generateUncachedResumeActivityConversationStarters() {
  try {
    // Query for memories that would be suitable for resume activity suggestions
    const memoriesWithPlaceHashes =
      await getMemoriesForResumeActivityConversationStarter(
        MAX_NUM_MEMORIES_FOR_RESUME_ACTIVITY
      );
    if (!memoriesWithPlaceHashes.length) {
      return [];
    }

    const urlsByHash = await lazy.resolveUrlsForMemories(
      memoriesWithPlaceHashes,
      { filterBySummary: true }
    );
    const memoriesWithUrlsAndTitles = memoriesWithPlaceHashes
      .map(memory =>
        attachUrlsToMemory(memory, urlsByHash, MAX_NUM_URLS_PER_MEMORY)
      )
      .filter(s => !!s.urls.length);
    if (!memoriesWithUrlsAndTitles.length) {
      return [];
    }

    // Load prompts and build the conversation for inference
    const conversation = await lazy.buildConversation(
      lazy.MODEL_FEATURES.RESUME_ACTIVITY_CONVERSATION_STARTER,
      { flowId: null }
    );
    const [
      { prompt: resumeActivitySystemPrompt },
      { prompt: resumeActivityUserPrompt },
    ] = await Promise.all([
      lazy.loadPrompt(
        lazy.MODEL_FEATURES.RESUME_ACTIVITY_CONVERSATION_STARTER,
        {
          module: "system-instructions",
        }
      ),
      lazy.loadPrompt(
        lazy.MODEL_FEATURES.RESUME_ACTIVITY_CONVERSATION_STARTER,
        {
          module: "user-data",
        }
      ),
    ]);
    conversation.setSystemMessage(resumeActivitySystemPrompt);

    const chatsById = await formatChatsForResumeActivityPrompt(
      memoriesWithUrlsAndTitles.map(m => m.memory)
    );
    const memoryInput = memoriesWithUrlsAndTitles.map((entry, index) =>
      buildMemoryInputBlock(entry, index, chatsById)
    );

    const userDataMessage = lazy.renderPrompt(resumeActivityUserPrompt, {
      memoryCount: memoryInput.length,
      memoryInput: memoryInput.join("\n\n"),
    });
    conversation.addUserMessage(userDataMessage);

    // SECURITY: the prompt mixes private user data (memory summaries, browsing
    // history, past chats) with untrusted webpage titles. The conversation is
    // single-shot and exposes no tools, so nothing can act on either, but the
    // flags must be accurate in case of later reuse of this conversation.
    conversation.securityProperties.setPrivateData();
    conversation.securityProperties.setUntrustedInput();
    conversation.securityProperties.commit();

    const runResult = await conversation.run({
      fxAccountToken: await openAIEngine.getFxAccountToken(),
    });
    const cardsById = lazy.indexInferenceResultsById(
      unpackJsonArrayOutput(runResult)
    );

    const result = memoriesWithUrlsAndTitles.map(({ memory, urls }, index) => {
      const card = cardsById.get(index);
      if (!card) {
        return null;
      }
      return {
        memory,
        content: {
          headline: (card?.headline || "").replace(/[.!?;:,]+$/, ""),
          status: (card?.status || "").replace(/[.!?;:,]+$/, ""),
          previewTabs: urls.map(({ url, title }) => ({ url, title })),
        },
      };
    });
    return result.filter(Boolean);
  } catch (e) {
    lazy.console.warn("[resume-activity] Conversation starters failed:", e);
    return [];
  }
}

/**
 * Generates conversation starter prompts for suggestions to resume activity based
 * on user memories and associated browsing history.
 *
 * @returns {Promise<Array<object>>} Array of objects containing memory and
 *   content with headline, status, and previewTabs
 */
export async function generateResumeActivityConversationStarters() {
  const watermark = await MemoriesManager.getLastSessionMemoryTimestamp();
  const cached = _getCachedResumeActivity(watermark);
  if (cached !== undefined) {
    return filterDeletedMemoriesFromResumeActivity(await cached);
  }

  const inFlight = generateUncachedResumeActivityConversationStarters();
  const entry = { inFlight };
  _resumeActivityCache.clear();
  _resumeActivityCache.set(watermark, entry);

  const result = await inFlight;
  if (_resumeActivityCache.get(watermark) === entry) {
    _resumeActivityCache.set(watermark, { result });
  }
  return result;
}

/**
 * Generates a conversation initialized with a resume activity suggestion.
 *
 * @param {object} resumeActivitySuggestion - Resume activity suggestion data
 * @param {object} resumeActivitySuggestion.memory - The memory associated with the resume activity suggestion
 * @param {object} resumeActivitySuggestion.content - Content of the suggestion
 * @param {string} resumeActivitySuggestion.content.headline - Headline for the suggestion
 * @param {string} resumeActivitySuggestion.content.status - Status for the suggestion
 * @param {Array<object>} resumeActivitySuggestion.content.previewTabs - Array of preview tabs
 * @param {string} resumeActivitySuggestion.content.previewTabs[].url - URL of a preview tab
 * @param {string} resumeActivitySuggestion.content.previewTabs[].title - Title of a preview tab
 * @returns {Promise<ChatConversation>} ChatConversation instance initialized with the resume activity context
 */
export async function constructConversationToResumeActivity(
  resumeActivitySuggestion
) {
  if (
    !resumeActivitySuggestion ||
    !resumeActivitySuggestion.memory ||
    !resumeActivitySuggestion.content
  ) {
    lazy.console.warn(
      "[resume-activity] Missing required inputs for conversation."
    );
    return null;
  }

  // Check required content fields
  const content = resumeActivitySuggestion.content;
  if (
    !content.headline ||
    !content.status ||
    !content.previewTabs ||
    !Array.isArray(content.previewTabs) ||
    content.previewTabs.length === 0
  ) {
    lazy.console.warn(
      "[resume-activity] Missing fields in resume activity content fields."
    );
    return null;
  }

  const conversation = new lazy.ChatConversation({
    title: resumeActivitySuggestion.content.headline,
  });

  const [
    { prompt: chatSystemPrompt },
    { prompt: resumeActivitySystemPrompt },
    { prompt: resumeActivityUserPrompt },
  ] = await Promise.all([
    lazy.loadPrompt(lazy.MODEL_FEATURES.CHAT, {
      model: conversation.engine?.model,
    }),
    lazy.loadPrompt(lazy.MODEL_FEATURES.RESUME_ACTIVITY_CONVERSATION, {
      module: "system-instructions",
    }),
    lazy.loadPrompt(lazy.MODEL_FEATURES.RESUME_ACTIVITY_CONVERSATION, {
      module: "user-data",
    }),
  ]);

  conversation.setSystemMessage({
    type: lazy.SYSTEM_PROMPT_TYPE.TEXT,
    body: [chatSystemPrompt, resumeActivitySystemPrompt].join("\n"),
  });

  const memoryInput = buildMemoryInputBlock(
    {
      memory: resumeActivitySuggestion.memory,
      urls: resumeActivitySuggestion.content.previewTabs,
    },
    undefined,
    await formatChatsForResumeActivityPrompt(
      Array.of(resumeActivitySuggestion.memory)
    )
  );

  const resumeActivityData = lazy.renderPrompt(resumeActivityUserPrompt, {
    memoryInput,
    conversationStarter: `headline: ${resumeActivitySuggestion.content.headline}
status: ${resumeActivitySuggestion.content.status}`,
  });

  const userMessage = conversation.addUserMessage(
    resumeActivitySuggestion.content.headline,
    null,
    undefined,
    {
      resumeActivityContext: resumeActivityData,
    }
  );

  userMessage.content.relevantMemories = [
    {
      id: resumeActivitySuggestion.memory.id,
      memory_summary: resumeActivitySuggestion.memory.memory_summary,
    },
  ];

  // SECURITY: the prompt mixes private user data (memory summaries, browsing
  // history, past chats) with untrusted webpage titles. Flags set so unallowed
  // tools will not be available for use in this conversation.
  conversation.securityProperties.setPrivateData();
  conversation.securityProperties.setUntrustedInput();
  conversation.securityProperties.commit();
  return conversation;
}
