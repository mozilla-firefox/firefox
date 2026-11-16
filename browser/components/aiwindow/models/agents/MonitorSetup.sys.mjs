/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import {
  MODEL_FEATURES,
  makeJSONSchemaBlob,
  parseAndExtractJSON,
  renderPrompt,
} from "moz-src:///browser/components/aiwindow/models/Utils.sys.mjs";
import { AuthProvider } from "moz-src:///browser/components/aiwindow/models/AuthProvider.sys.mjs";
import { sanitizeUntrustedContent } from "moz-src:///browser/components/aiwindow/models/ChatUtils.sys.mjs";
import {
  TOTAL_NUM_URLS_IN_MONITOR,
  isAllowedWatchUrl,
} from "moz-src:///browser/components/aiwindow/models/agents/Monitor.sys.mjs";
import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  buildConversation:
    "moz-src:///browser/components/aiwindow/models/PromptLoader.sys.mjs",
  clearTimeout: "resource://gre/modules/Timer.sys.mjs",
  GetPageContent: "moz-src:///browser/components/aiwindow/models/Tools.sys.mjs",
  loadPrompt:
    "moz-src:///browser/components/aiwindow/models/PromptLoader.sys.mjs",
  MemoriesManager:
    "moz-src:///browser/components/aiwindow/models/memories/MemoriesManager.sys.mjs",
  MonitorAgent:
    "moz-src:///browser/components/aiwindow/models/agents/MonitorAgent.sys.mjs",
  setTimeout: "resource://gre/modules/Timer.sys.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "log", () =>
  console.createInstance({
    logModule: "MonitorSetup",
    maxLogLevelPref: "browser.smartwindow.monitorAgent.logLevel",
  })
);

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "suggestionsEnabled",
  "browser.smartwindow.agent.suggestions.enabled",
  true
);

// The answer is only cut when it is far longer than anything the form is for
export const MAX_TITLE_LENGTH = 200;
export const MAX_CONDITION_LENGTH = 1000;
// Bounds on what goes into the prompt. They are there to stop a runaway
// prompt and should almost never be reached.
export const MAX_CHAT_HISTORY_LENGTH = 8000;
export const MAX_MEMORIES = 3;
export const MAX_MEMORY_LENGTH = 500;
const MAX_PREVIOUS_DRAFTS = 3;
const MAX_MEMORIES_QUERY_LENGTH = 2000;
const MAX_EXISTING_TITLES = 30;
const MAX_PAGE_TEXT_LENGTH = 8000;
const PAGE_TEXT_TIMEOUT_MS = 5 * 1000;
// Memories are a nice-to-have, so the lookup is waited for no longer than this
export const MEMORIES_TIMEOUT_MS = 3 * 1000;
// The create card gives up a little later than this, see agent-monitor-item
export const SUGGESTION_TIMEOUT_MS = 40 * 1000;

const SUGGESTION_SCHEMA = {
  type: "object",
  additionalProperties: false,
  required: ["title", "condition"],
  properties: {
    title: { type: "string" },
    condition: { type: "string" },
  },
};

// How each chat message is labelled in 'chatHistory', one message per line
export const CHAT_ROLE_LABELS = Object.freeze({
  user: "User",
  assistant: "Assistant",
});

/**
 * @typedef {object} MonitorSetupSuggestion
 * @property {string} title - A name for the task. Empty when none could be
 *   generated.
 * @property {string} condition - What to watch for. Empty when none could be
 *   generated.
 */

/**
 * @typedef {object} MonitorSetupDraft
 * @property {string} title - A task name written for this form earlier
 * @property {string} condition - The condition written with it
 */

/**
 * Suggests what to fill a new monitor's form with: a task name and a condition
 * to watch for, grounded in what the watched pages say, in what the user was
 * just chatting about and in what Smart Window remembers about the user.
 */
export const MonitorSetup = {
  /**
   * @param {object} options
   * @param {string[]} [options.watchUrls] - Pages the monitor will watch
   * @param {Record<string, string>} [options.pageTitles] - Titles of those
   *   pages, keyed by URL
   * @param {string} [options.chatHistory] - The recent chat messages, one
   *   per line, each starting with one of CHAT_ROLE_LABELS and a colon
   * @param {string[]} [options.existingTitles] - Names of the user's monitors,
   *   which the model is asked not to repeat
   * @param {MonitorSetupDraft[]} [options.previousDrafts] - What was already
   *   suggested for this form, oldest first. The new suggestion is told to
   *   differ.
   * @param {boolean} [options.useMemories] - False when the chat the form sits
   *   in has memories switched off
   * @param {number} [options.timeoutMs] - How long the suggestion may take
   * @returns {Promise<?MonitorSetupSuggestion>} null when suggestions are
   *   turned off, there is nothing to ground one in, or nothing usable came
   *   back
   */
  async suggest({
    watchUrls = [],
    pageTitles = {},
    chatHistory = "",
    existingTitles = [],
    previousDrafts = [],
    useMemories = true,
    timeoutMs = SUGGESTION_TIMEOUT_MS,
  } = {}) {
    if (!lazy.suggestionsEnabled || lazy.MonitorAgent.isBlocked) {
      return null;
    }
    const urls = (Array.isArray(watchUrls) ? watchUrls : [])
      .filter(url => typeof url === "string" && isAllowedWatchUrl(url))
      .slice(0, TOTAL_NUM_URLS_IN_MONITOR);
    // The newest messages are at the end, so those are the ones kept
    const history = (typeof chatHistory === "string" ? chatHistory : "")
      .trim()
      .slice(-MAX_CHAT_HISTORY_LENGTH);
    if (!urls.length && !history) {
      return null;
    }

    const abortController = new AbortController();
    try {
      return await withTimeout(
        this._suggest({
          urls,
          pageTitles: pageTitles ?? {},
          chatHistory: history,
          existingTitles: (Array.isArray(existingTitles) ? existingTitles : [])
            .map(title => String(title ?? "").trim())
            .filter(Boolean),
          previousDrafts: cleanPreviousDrafts(previousDrafts),
          useMemories: useMemories === true,
          signal: abortController.signal,
        }),
        timeoutMs
      );
    } catch (error) {
      // Cancels the model request when the time ran out with it in flight
      abortController.abort(error);
      lazy.log.warn(`No monitor setup suggestion: ${error.message ?? error}`);
      return null;
    }
  },

  async _suggest({
    urls,
    pageTitles,
    chatHistory,
    existingTitles,
    previousDrafts,
    useMemories,
    signal,
  }) {
    const conversation = await lazy.buildConversation(
      MODEL_FEATURES.AGENT_MONITOR_SETUP
    );
    const [{ prompt: systemTemplate }, { prompt: userTemplate }] =
      await Promise.all([
        lazy.loadPrompt(MODEL_FEATURES.AGENT_MONITOR_SETUP, {
          module: "system-instructions",
        }),
        lazy.loadPrompt(MODEL_FEATURES.AGENT_MONITOR_SETUP, {
          module: "user-data",
        }),
      ]);

    const [pageText, memories] = await Promise.all([
      this._extractOpenPageText(urls, conversation),
      useMemories
        ? this._getRelevantMemories(
            urls.map(url => pageTitles[url]),
            chatHistory
          )
        : "",
    ]);
    if (memories) {
      conversation.securityProperties?.setPrivateData();
    }

    const locale = Services.locale.appLocaleAsBCP47;
    const language = describeLanguage(locale);
    conversation.setSystemMessage(
      renderPrompt(systemTemplate, { locale, language })
    );
    // Page text goes last so nothing it contains is read as a placeholder.
    // Each block of untrusted text has its own closing tag made harmless, so
    // the text can't end the block early.
    conversation.addUserMessage(
      renderPrompt(userTemplate, {
        locale,
        language,
        pages: urls
          .map(url => {
            const title = pageTitles[url];
            return title
              ? `- ${sanitizeUntrustedContent(title)} (${url})`
              : `- ${url}`;
          })
          .join("\n"),
        existingTitles: existingTitles
          .slice(0, MAX_EXISTING_TITLES)
          .map(title => `- ${title}`)
          .join("\n"),
        memories,
        chatHistory: escapeBlockTag(chatHistory, "recent_chat"),
        previousDrafts: escapeBlockTag(
          previousDrafts.map(draft => JSON.stringify(draft)).join("\n"),
          "earlier_drafts"
        ),
        pageText: escapeBlockTag(pageText, "page_text"),
      })
    );

    const response = await conversation.run({
      token: await AuthProvider.getToken(),
      signal,
      inferenceParams: {
        response_format: makeJSONSchemaBlob(
          "MonitorSetupSuggestion",
          SUGGESTION_SCHEMA
        ),
      },
      tools: [],
    });

    const parsed = parseAndExtractJSON(response, null);
    if (!parsed || typeof parsed !== "object" || Array.isArray(parsed)) {
      throw new Error("The model did not return a suggestion.");
    }
    const title = collapseWhitespace(parsed.title).slice(0, MAX_TITLE_LENGTH);
    const condition = collapseWhitespace(parsed.condition).slice(
      0,
      MAX_CONDITION_LENGTH
    );
    if (!title && !condition) {
      throw new Error("The model returned an empty suggestion.");
    }
    return { title, condition };
  },

  /**
   * What Smart Window remembers about the user that is close to these pages
   * and to what the user said in the chat. Anything wrong, slow or empty
   * means no memories.
   *
   * @param {string[]} titles - Titles of the watched pages
   * @param {string} chatHistory
   * @returns {Promise<string>} One "- summary" line per memory, or ""
   */
  async _getRelevantMemories(titles, chatHistory) {
    const userPrefix = `${CHAT_ROLE_LABELS.user}: `;
    const query = [
      ...titles,
      // Newest first, in case the embedder only reads the start
      ...chatHistory
        .split("\n")
        .filter(line => line.startsWith(userPrefix))
        .map(line => line.slice(userPrefix.length))
        .reverse(),
    ]
      .map(collapseWhitespace)
      .filter(Boolean)
      .join("\n")
      .slice(0, MAX_MEMORIES_QUERY_LENGTH);
    if (!query) {
      return "";
    }
    try {
      const memories = await withTimeout(
        lazy.MemoriesManager.getRelevantMemories(query, MAX_MEMORIES),
        MEMORIES_TIMEOUT_MS,
        "Finding memories timed out."
      );
      return (Array.isArray(memories) ? memories : [])
        .map(memory => collapseWhitespace(memory?.memory_summary))
        .filter(Boolean)
        .slice(0, MAX_MEMORIES)
        .map(
          summary =>
            `- ${escapeBlockTag(summary.slice(0, MAX_MEMORY_LENGTH), "memories")}`
        )
        .join("\n");
    } catch (error) {
      lazy.log.warn(
        `No memories for the suggestion: ${error.message ?? error}`
      );
      return "";
    }
  },

  /**
   * Reads the pages that are open in a tab. Pages that would need a fresh
   * load are left out: the suggestion has to arrive while the form is open.
   *
   * @param {string[]} urls
   * @param {object} conversation
   * @returns {Promise<string>} The text of the pages that could be read, or ""
   */
  async _extractOpenPageText(urls, conversation) {
    const openUrls = urls.filter(url => lazy.GetPageContent.getTabWithURL(url));
    if (!openUrls.length) {
      return "";
    }
    const abortController = new AbortController();
    const timer = lazy.setTimeout(
      () => abortController.abort(new Error("Reading the pages timed out.")),
      PAGE_TEXT_TIMEOUT_MS
    );
    try {
      const results = await lazy.GetPageContent.getPageContent(
        { url_list: openUrls, signal: abortController.signal },
        conversation
      );
      return results
        .filter(result => result.ok)
        .map(result => result.content)
        .join("\n\n<----- PAGE BREAK ---->\n\n")
        .slice(0, MAX_PAGE_TEXT_LENGTH);
    } catch (error) {
      lazy.log.warn(`Could not read the pages: ${error.message ?? error}`);
      return "";
    } finally {
      lazy.clearTimeout(timer);
    }
  },
};

/**
 * @param {*} drafts - What the caller says was suggested before
 * @returns {MonitorSetupDraft[]} The newest MAX_PREVIOUS_DRAFTS of them that
 *   say anything, oldest first, each bounded like a fresh answer
 */
function cleanPreviousDrafts(drafts) {
  return (Array.isArray(drafts) ? drafts : [])
    .map(draft => ({
      title: collapseWhitespace(draft?.title).slice(0, MAX_TITLE_LENGTH),
      condition: collapseWhitespace(draft?.condition).slice(
        0,
        MAX_CONDITION_LENGTH
      ),
    }))
    .filter(({ title, condition }) => title || condition)
    .slice(-MAX_PREVIOUS_DRAFTS);
}

/**
 * @param {string} locale - A BCP 47 tag
 * @returns {string} The language's English name with the tag, e.g.
 *   "French (fr)", or the bare tag when it has no name
 */
export function describeLanguage(locale) {
  try {
    const name = new Intl.DisplayNames("en", { type: "language" }).of(locale);
    if (name && name !== locale) {
      return `${name} (${locale})`;
    }
  } catch {}
  return locale;
}

function collapseWhitespace(value) {
  return typeof value === "string" ? value.replace(/\s+/g, " ").trim() : "";
}

/**
 * Makes the opening and closing forms of one tag harmless in text that is put
 * inside a block delimited by that tag.
 *
 * @param {string} text
 * @param {string} tag - The tag name, e.g. "recent_chat"
 * @returns {string}
 */
function escapeBlockTag(text, tag) {
  return text.replace(
    new RegExp(`<(\\s*/?\\s*${tag}\\s*)>`, "gi"),
    "&lt;$1&gt;"
  );
}

async function withTimeout(
  promise,
  timeoutMs,
  message = "The suggestion timed out."
) {
  let timer;
  const timeout = new Promise((_resolve, reject) => {
    timer = lazy.setTimeout(() => reject(new Error(message)), timeoutMs);
  });
  try {
    return await Promise.race([promise, timeout]);
  } finally {
    lazy.clearTimeout(timer);
  }
}
