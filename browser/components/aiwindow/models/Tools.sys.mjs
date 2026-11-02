/**
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

/**
 * This file contains LLM tool abstractions and tool definitions.
 */

/**
 * @import { ChatConversation } from "moz-src:///browser/components/aiwindow/ui/modules/ChatConversation.sys.mjs"
 * @import { TraceId } from "moz-src:///toolkit/components/pageextractor/PageExtractorEvents.sys.mjs"
 * @import { AITabContext, AITabResult } from "moz-src:///browser/components/aiwindow/models/aitab/AITab.sys.mjs"
 */

import { isSmartPageURL } from "chrome://browser/content/aiwindow/modules/TrustedInternalURLs.mjs";
import { getSkillPrompt } from "moz-src:///browser/components/aiwindow/models/PromptLoader.sys.mjs";
import { searchBrowsingHistory as implSearchBrowsingHistory } from "moz-src:///browser/components/aiwindow/models/SearchBrowsingHistory.sys.mjs";
import {
  manageTabsAction,
  TAB_ACTIONS,
} from "moz-src:///browser/components/aiwindow/models/ManageTabs.sys.mjs";
import { PageExtractorParent } from "resource://gre/actors/PageExtractorParent.sys.mjs";
import {
  ChatStore,
  MESSAGE_ROLE,
} from "moz-src:///browser/components/aiwindow/ui/modules/ChatStore.sys.mjs";
import {
  sanitizeUntrustedContent,
  isNewPageUrl,
} from "moz-src:///browser/components/aiwindow/models/ChatUtils.sys.mjs";
import { isAllowedURLProtocol } from "moz-src:///browser/components/aiwindow/models/SecurityProperties.sys.mjs";

import {
  CountVectorizer,
  cosSim,
} from "chrome://global/content/ml/NLPUtils.sys.mjs";
import { EmbeddingsGenerator } from "chrome://global/content/ml/EmbeddingsGenerator.sys.mjs";
import { SmartTabGroupingManager } from "moz-src:///browser/components/tabbrowser/SmartTabGrouping.sys.mjs";
import { UI_TYPES } from "moz-src:///browser/components/aiwindow/ui/modules/ToolUI.sys.mjs";

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  AIWindow:
    "moz-src:///browser/components/aiwindow/ui/modules/AIWindow.sys.mjs",
  BrowserWindowTracker: "resource:///modules/BrowserWindowTracker.sys.mjs",
  clearTimeout: "resource://gre/modules/Timer.sys.mjs",
  AITab: "moz-src:///browser/components/aiwindow/models/aitab/AITab.sys.mjs",
  AITabStore:
    "moz-src:///browser/components/aiwindow/ui/modules/AITabStore.sys.mjs",
  getHeadingFont:
    "moz-src:///browser/components/aiwindow/ui/modules/AITabHeadingFont.sys.mjs",
  setTimeout: "resource://gre/modules/Timer.sys.mjs",
  MemoriesManager:
    "moz-src:///browser/components/aiwindow/models/memories/MemoriesManager.sys.mjs",
  SessionStore:
    "moz-src:///browser/components/sessionstore/SessionStore.sys.mjs",
  SmartWindowNavigationInfo:
    "moz-src:///browser/components/aiwindow/models/SmartWindowNavigationInfo.sys.mjs",
  ToolUITelemetry:
    "moz-src:///browser/components/aiwindow/ui/modules/ToolUITelemetry.sys.mjs",
  // @todo Bug 2009194
  // PageDataService:
  //   "moz-src:///browser/components/pagedata/PageDataService.sys.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "console", () =>
  console.createInstance({
    prefix: "Conversation",
    maxLogLevelPref: "browser.smartwindow.conversation.logLevel",
  })
);

// Important! Changing or removing this value requires a security review.
//
// Hard code a reasonable working limit for how many tabs that a language model can retrieve.
// The metadata from each tab contains untrusted text content that we limit (for instance
// with truncation) in order to treat this information as trusted.
//
// We also make this limited in a non-configurable way so that it reduces the risk
// of exfiltration for private data. While most users only have a few tabs open at a time,
// some users can have thousands of tabs open at once.
export const MAX_TABS = 30;
// Max number of tabs to rank by semantic similarity to topic (safeguard to avoid embedding hundreds of tabs)
export const MAX_RANK_TABS = 5 * MAX_TABS;
// Max number of tabs the mentioned tab groups expand to in total.
export const MAX_TAB_GROUP_MEMBERS = MAX_TABS;

const KEYWORD_WEIGHT = 0.3; // 0 = pure embedding, 1 = pure lexical

const tokenizer = new CountVectorizer();

let _embeddingsGenerator = null;
function getEmbeddingsGenerator() {
  if (!_embeddingsGenerator) {
    _embeddingsGenerator = EmbeddingsGenerator.forGeneral();
  }
  return _embeddingsGenerator;
}

async function embedTexts(texts) {
  const result = await getEmbeddingsGenerator().embedMany(texts);
  return result.output || result;
}

function keywordRecall(topicTokens, titleTokens) {
  if (topicTokens.size == 0) {
    return 0;
  }
  let hits = 0;
  for (const token of topicTokens) {
    if (titleTokens.has(token)) {
      hits++;
    }
  }

  return hits / topicTokens.size;
}

// this exists only to make writing tests easier
export const _embeddingFunctions = { embedTexts, keywordRecall };

// Important! Changing or removing this value requires a security review.
//
// Hard code a reasonable working limit for how many history results that a language model
// can retrieve. The metadata from each of these history items contains untrusted text
// content that we limit (for instance with truncation) in order to treat this information
// as trusted.
//
// We also make this limited in a non-configurable way so that it reduces the risk
// of exfiltration for private data. A language model that can make arbitrary requests
// through prompt injection could leak the contents of a user's entire history.
const MAX_HISTORY_RESULTS = 15;

export const GET_OPEN_TABS = "get_open_tabs";
export const SEARCH_BROWSING_HISTORY = "search_browsing_history";
export const GET_PAGE_CONTENT = "get_page_content";
export const GENERATE_AITAB = "generate_page";
export const RUN_SEARCH = "run_search";
export const SEARCH_THE_WEB = "search_the_web";
export const GET_USER_MEMORIES = "get_user_memories";
export const GET_NAVIGATION_INFO = "get_navigation_info";
export const MANAGE_TABS = "manage_tabs";
export const GET_SKILL = "get_skill";
export const ADD_MEMORY = "add_memory";

// Tools gated behind a feature pref. Filtered out of the model's tool list
// in Chat.sys.mjs when the pref is off.
export const AITAB_PREF = "browser.smartwindow.aitab.enabled";
export const AITAB_TOOLS = new Set([GENERATE_AITAB]);
export const SEARCH_QUERY_ENDPOINT_PREF =
  "browser.smartwindow.searchQuery.endpointURL";
export const SEARCH_QUERY_APIKEY_PREF =
  "browser.smartwindow.searchQuery.apiKey";

// When true, search_the_web returns Exa snippets straight to the main
// assistant. When false, it runs the answer-generation flow with page reads.
// The two paths return different shapes and so need different tool configs.
export const SEARCH_THE_WEB_FAST_PREF = "browser.smartwindow.searchTheWebFast";

// When true, search_the_web asks Exa's /answers service for a written answer
// and its citations in a single call. Takes precedence over
// SEARCH_THE_WEB_FAST_PREF, which only selects between the other two paths, but
// is ignored on a custom endpoint — see selectSearchTheWebPath.
export const SEARCH_THE_WEB_ANSWERS_PREF =
  "browser.smartwindow.searchTheWebAnswers";

/**
 * The paths search_the_web can take. Each returns a different shape, so the
 * tool config offered to the model has to match the one that will run.
 *
 * @enum {string}
 */
export const SEARCH_THE_WEB_PATH = Object.freeze({
  ANSWERS: "answers",
  FAST: "fast",
  GROUNDED: "grounded",
});

/**
 * Which path search_the_web takes for the current profile. The single source of
 * truth for the precedence: SearchWorkflow dispatches on it and Chat picks the
 * matching tool config from it, so the two cannot drift apart.
 *
 * The answers path is only available on Mozilla MLPA endpoint - custom endpoints
 * must use search_the_web_fast or grounded
 *
 * @param {ChatConversation} conversation
 * @returns {SEARCH_THE_WEB_PATH}
 */
export function selectSearchTheWebPath(conversation) {
  if (
    Services.prefs.getBoolPref(SEARCH_THE_WEB_ANSWERS_PREF, false) &&
    conversation.engine?.isDefaultEndpoint
  ) {
    return SEARCH_THE_WEB_PATH.ANSWERS;
  }
  return Services.prefs.getBoolPref(SEARCH_THE_WEB_FAST_PREF, true)
    ? SEARCH_THE_WEB_PATH.FAST
    : SEARCH_THE_WEB_PATH.GROUNDED;
}

export const TOOLS = [
  GET_OPEN_TABS,
  SEARCH_BROWSING_HISTORY,
  GET_PAGE_CONTENT,
  GET_USER_MEMORIES,
  GET_NAVIGATION_INFO,
  MANAGE_TABS,
  ADD_MEMORY,
  SEARCH_THE_WEB,
  GET_SKILL,
  GENERATE_AITAB,
];

export const RUN_SEARCH_VERBATIM_QUERY_DESCRIPTION =
  "Perform a web search using the browser's default search engine and return " +
  "the search results page content. Use this when the user needs current web " +
  "information that would benefit from a live search. This tool uses the current user message as the query.";

export const RUN_SEARCH_GENERATED_QUERY_DESCRIPION =
  "Perform a web search using the browser's default search engine and return " +
  "the search results page content. Use this when the user needs current web " +
  "information that would benefit from a live search.";

export const RUN_SEARCH_TOOL_CONFIG_VERBATIM_QUERY = {
  type: "function",
  function: {
    name: RUN_SEARCH,
    description: RUN_SEARCH_VERBATIM_QUERY_DESCRIPTION,
    parameters: {
      type: "object",
      properties: {},
    },
  },
};

export const RUN_SEARCH_TOOL_CONFIG_GENERATED_QUERY = {
  type: "function",
  function: {
    name: RUN_SEARCH,
    description: RUN_SEARCH_GENERATED_QUERY_DESCRIPION,
    parameters: {
      type: "object",
      properties: {
        query: {
          type: "string",
          description:
            "The search query to execute. Should be specific and search-engine optimized.",
        },
      },
      required: ["query"],
    },
  },
};

export const SEARCH_THE_WEB_DESCRIPTION =
  "Answer a question using the web. Retrieves and reads web content in the " +
  "background and returns a grounded answer. Use this whenever the user asks " +
  "an informational question that needs fresh or external knowledge. Pass a " +
  "clear, self-contained query; you may rewrite the user's phrasing (for " +
  "example resolve 'near me' to a place) and add brief context.";

export const SEARCH_THE_WEB_FAST_DESCRIPTION =
  "Search the web. Returns a short list of results, each with a title, a URL, " +
  "and a snippet of text from the page. Use this whenever the user asks an " +
  "informational question that needs fresh or external knowledge. Answer from " +
  "the snippets when they are enough, and call get_page_content on one of the " +
  "returned URLs when you need the full page. Pass a clear, self-contained " +
  "query; you may rewrite the user's phrasing (for example resolve 'near me' " +
  "to a place).";

const SEARCH_THE_WEB_TOOL_CONFIG = {
  type: "function",
  function: {
    name: SEARCH_THE_WEB,
    description: SEARCH_THE_WEB_DESCRIPTION,
    parameters: {
      type: "object",
      properties: {
        query: {
          type: "string",
          description:
            "The self-contained question or query to answer from the web.",
        },
        context: {
          type: "string",
          description:
            "Optional additional context that helps answer the query, such as " +
            "a location or a clarification the user gave earlier.",
        },
      },
      required: ["query"],
    },
  },
};

export const SEARCH_THE_WEB_ANSWERS_DESCRIPTION =
  "Answer a question using the web. Searches and reads web content in the " +
  "background and returns an answer together with the URLs it cites. Use this " +
  "whenever the user asks an informational question that needs fresh or " +
  "external knowledge. Pass a clear, self-contained query; you may rewrite the " +
  "user's phrasing (for example resolve 'near me' to a place).";

// Answers-path variant. No `context` parameter: only the query is sent to the
// /answers service.
export const SEARCH_THE_WEB_TOOL_CONFIG_ANSWERS = {
  type: "function",
  function: {
    name: SEARCH_THE_WEB,
    description: SEARCH_THE_WEB_ANSWERS_DESCRIPTION,
    parameters: {
      type: "object",
      properties: {
        query: {
          type: "string",
          description:
            "The self-contained question or query to answer from the web.",
        },
      },
      required: ["query"],
    },
  },
};

// Fast-path variant. No `context` parameter: the fast path has no sub-agent
// prompt to feed it.
export const SEARCH_THE_WEB_TOOL_CONFIG_FAST = {
  type: "function",
  function: {
    name: SEARCH_THE_WEB,
    description: SEARCH_THE_WEB_FAST_DESCRIPTION,
    parameters: {
      type: "object",
      properties: {
        query: {
          type: "string",
          description:
            "The self-contained question or query to answer from the web.",
        },
      },
      required: ["query"],
    },
  },
};

/**
 * The search_the_web config for the path that will run, or null for the
 * grounded path, whose config is the one already in `toolsConfig`.
 *
 * @param {ChatConversation} conversation
 * @returns {object|null}
 */
export function searchTheWebToolConfig(conversation) {
  switch (selectSearchTheWebPath(conversation)) {
    case SEARCH_THE_WEB_PATH.ANSWERS:
      return SEARCH_THE_WEB_TOOL_CONFIG_ANSWERS;
    case SEARCH_THE_WEB_PATH.FAST:
      return SEARCH_THE_WEB_TOOL_CONFIG_FAST;
    default:
      return null;
  }
}

export const toolsConfig = [
  {
    type: "function",
    function: {
      name: GET_OPEN_TABS,
      description:
        `Return up to ${MAX_TABS} of the user's open tabs. ` +
        "Default behavior is to retrieve tabs ordered by most " +
        `recently viewed. If 'topic' is specified, ` +
        "the tabs whose titles are most similar to the " +
        "topic are returned instead, ordered by similarity. " +
        "Tabs sharing a `windowId` are in the same browser window.",
      parameters: {
        type: "object",
        properties: {
          topic: {
            type: "string",
            description:
              "Optional. Ranks tabs by similarity between tab metadata and this value. ",
          },
        },
      },
    },
  },
  {
    type: "function",
    function: {
      name: SEARCH_BROWSING_HISTORY,
      description:
        "Retrieve pages from the user's past browsing history, optionally filtered by " +
        "topic and/or time range.",
      parameters: {
        type: "object",
        properties: {
          searchTerm: {
            type: "string",
            description:
              "A concise phrase describing what the user is trying to find in their " +
              "browsing history (topic, site, or purpose).",
          },
          startTs: {
            type: "string",
            description:
              "Inclusive start of the time range as a local ISO 8601 datetime " +
              "('YYYY-MM-DDTHH:mm:ss', no timezone).",
          },
          endTs: {
            type: "string",
            description:
              "Inclusive end of the time range as a local ISO 8601 datetime " +
              "('YYYY-MM-DDTHH:mm:ss', no timezone).",
          },
        },
      },
    },
  },
  {
    type: "function",
    function: {
      name: GET_PAGE_CONTENT,
      description:
        "Retrieve cleaned text content of all the provided browser page URL Tokens in the list.",
      parameters: {
        type: "object",
        properties: {
          url_list: {
            type: "array",
            items: {
              type: "string",
              description:
                "A URL token that appeared in the conversation, formatted as §url_token: DOMAIN_TLD_PATH_n§. " +
                "Do NOT fabricate tokens. Only use tokens from user messages and tool results.",
            },
            minItems: 1,
            description: "List of URL tokens to fetch content from.",
          },
        },
        required: ["url_list"],
      },
    },
  },
  {
    type: "function",
    function: {
      name: GENERATE_AITAB,
      description:
        "Design a custom web page with user content, or revise a page an " +
        "earlier call generated by naming it in modify_slug.",
      parameters: {
        type: "object",
        properties: {
          focus: {
            type: "string",
            description:
              "The focus on what information the user wants in the generated page.",
          },
          url_list: {
            type: "array",
            items: {
              type: "string",
              description:
                "A URL token formatted as §url_token: DOMAIN_TLD_PATH_n§. " +
                "Do NOT fabricate tokens. Only use tokens from user messages and tool results such as searching open tabs.",
            },
            description: "List of URL tokens to fetch content from",
          },
          raw_content: {
            type: "string",
            description:
              "Content to use directly when generating or modifying a page, " +
              "such as text from search results or other tools. URLs appearing " +
              "in this block will not be fetched but should be in " +
              "§url_token: DOMAIN_TLD_PATH_n§ format. A URL's page content is " +
              "only read when the URL is also in the url_list field.",
          },
          modify_slug: {
            type: "string",
            description:
              "Slug of the page to modify, in lowercase slug format. Only use " +
              "a slug an earlier generate_page result reported, and omit it " +
              "entirely to create a new page.",
          },
          modify_instructions: {
            type: "string",
            description:
              "A description of what needs to be changed. URL additions that " +
              "require content to be read must also be included in the " +
              "url_list field",
          },
        },
      },
    },
  },
  SEARCH_THE_WEB_TOOL_CONFIG,
  {
    type: "function",
    function: {
      name: GET_NAVIGATION_INFO,
      description:
        "Find relevant Firefox preferences pages based on a user query. " +
        "Use this when the user asks where to find a setting, how to configure something in " +
        "Firefox, or where to manage Smart Window features like memories.",
      parameters: {
        type: "object",
        properties: {
          query: {
            type: "string",
            description:
              "A natural-language description of what the user is looking for, " +
              "e.g. 'where to manage memories' or 'privacy settings'.",
          },
        },
        required: ["query"],
      },
    },
  },
  {
    type: "function",
    function: {
      name: GET_USER_MEMORIES,
      description:
        'Retrieves all memories saved about the user to answer questions like "What do you know about me?", "What memories have you saved?", "What do you remember about me?", etc. Respond to the user that these are memories.',
      parameters: {
        type: "object",
        properties: {},
      },
    },
  },
  {
    type: "function",
    function: {
      name: MANAGE_TABS,
      description:
        "Perform a management action on a list of selected open tabs. You can " +
        "set ask_confirmation to true to request user confirmation and allow " +
        "them to confirm the selected list before the action is finalized.",
      parameters: {
        type: "object",
        properties: {
          action: {
            type: "string",
            description: "The action to be performed on the tabs.",
            enum: TAB_ACTIONS,
          },
          ask_confirmation: {
            type: "boolean",
            description:
              "Whether to show the user the list of tabs and require their confirmation " +
              "before executing the action. Default to true. Only set to false when " +
              "the user's request unambiguously identifies the specific tabs to act on, " +
              "and the action does not affect all (or nearly all) of the user's open tabs. " +
              "When in doubt, set to true.",
          },
          url_tokens: {
            type: "array",
            items: {
              type: "string",
              description: "A URL token corresponding to an open tab",
            },
            minItems: 1,
            description:
              "List of URL tokens identifying the tabs the action should be taken on.",
          },
          label: {
            type: "string",
            description:
              "Optional concise label (1-3 words) for the new tab group. " +
              "Only used when action is 'group_tabs'. Derive from the common " +
              "theme of the selected tabs. Omit when no clear theme exists.",
          },
        },
        required: ["action", "ask_confirmation", "url_tokens"],
      },
    },
  },
  {
    type: "function",
    function: {
      name: ADD_MEMORY,
      description:
        "Save a memory when the user explicitly asks to be remembered something " +
        '(e.g. "remember that I prefer Walmart for shopping"). Transform the ' +
        "request into a concise third-person summary of the preference or fact " +
        '(e.g. "Prefers Walmart for shopping"). Do NOT save personally ' +
        "identifiable or financial information such as names, addresses, phone " +
        "numbers, emails, SSNs, account or card numbers.",
      parameters: {
        type: "object",
        properties: {
          memorySummary: {
            type: "string",
            maxLength: 100,
            description:
              "A concise summary of what to remember, transformed from the user's request.",
          },
          containsPersonallyIdentifiableInfo: {
            type: "boolean",
            description:
              "True if the request contains personally identifiable or financial " +
              "information (names, addresses, phone numbers, emails, SSNs, account " +
              "or card numbers, etc.).",
          },
        },
        required: ["memorySummary", "containsPersonallyIdentifiableInfo"],
      },
    },
  },
  {
    type: "function",
    function: {
      name: GET_SKILL,
      description:
        "Look up a focused instruction set ('skill') by name. Use when the user's request maps to a known specialty. The available skill names are listed in the system prompt.",
      parameters: {
        type: "object",
        properties: {
          name: {
            type: "string",
            description: "The name of the skill to retrieve.",
          },
        },
        required: ["name"],
      },
    },
  },
];

/**
 * Gets N amount of most recently opened tabs
 *
 * @param {number} [amount=MAX_TABS] How many tabs to get
 *
 * @returns {Array<TabInfo>}
 */
export function getTabList(amount = MAX_TABS) {
  /** @type {Array<TabInfo>} */
  const tabs = [];

  for (const win of lazy.BrowserWindowTracker.orderedWindows) {
    if (!lazy.AIWindow.isAIWindowActive(win)) {
      continue;
    }

    if (!win.closed && win.gBrowser) {
      const windowId = lazy.SessionStore.getWindowId(win);
      for (const tab of win.gBrowser.tabs) {
        const browser = tab.linkedBrowser;
        const url = browser?.currentURI?.spec;
        const title = tab.label;

        if (isAllowedURLProtocol(url) && !isNewPageUrl(url)) {
          tabs.push({
            url,
            title: sanitizeUntrustedContent(title),
            lastAccessed: tab.lastAccessed,
            windowId,
            pinned: tab.pinned,
          });
        }
      }
    }
  }

  tabs.sort((a, b) => b.lastAccessed - a.lastAccessed);

  return tabs.slice(0, amount);
}

/**
 * Metadata about a Tab used in chat conversations.
 *
 * @typedef {object} TabInfo
 * @property {string} url - The url of the tab.
 * @property {string} title - Title of the tab.
 * @property {number} lastAccessed - When the tab was last accessed in milliseconds.
 * @property {string|null} windowId - SessionStore ID of the browser window.
 * @property {boolean} pinned - Whether the tab is pinned.
 */

/**
 * Retrieves a list of the latest open tabs from the current active browser window.
 * Tabs are sorted by most recently accessed and limited to MAX_TABS results.
 * Only includes tabs with http/https URLs.
 *
 * @param {object} toolParams
 * @param {string} [toolParams.topic] - Optional. If specified, tabs are ranked by
 *   similarity between the topic and each tab's title.
 * @param {ChatConversation} conversation
 * @returns {Promise<Array<TabInfo>>}
 */
export async function getOpenTabs({ topic = "" } = {}, conversation) {
  // No security check needed. The security checks prevent data exfiltration,
  // which requires external communication. This tool makes no external requests.

  const startTime = ChromeUtils.now();

  const tabs = getTabList(topic ? MAX_RANK_TABS : MAX_TABS);

  if (topic) {
    const rankStart = ChromeUtils.now(); // for profiling topic-based ranking
    try {
      const titles = tabs.map(t =>
        SmartTabGroupingManager.preprocessText(t.title || "")
      );
      const [topicEmbedding, ...titleEmbeddings] =
        await _embeddingFunctions.embedTexts([topic, ...titles]);

      const topicTokens = new Set(tokenizer.tokenize(topic));

      const scored = tabs.map((tab, i) => {
        // rescale cosine similarity to 0 - 1 to match keyword recall scale
        const dense = (cosSim(topicEmbedding, titleEmbeddings[i]) + 1) / 2;
        const titleTokens = new Set(tokenizer.tokenize(titles[i]));
        const sparse = _embeddingFunctions.keywordRecall(
          topicTokens,
          titleTokens
        );
        return {
          tab,
          score: (1 - KEYWORD_WEIGHT) * dense + KEYWORD_WEIGHT * sparse,
        };
      });
      scored.sort((a, b) => b.score - a.score);
      tabs.length = 0;
      tabs.push(...scored.map(s => s.tab));

      ChromeUtils.addProfilerMarker(
        "SmartWindow",
        { startTime: rankStart },
        `Tool:get_open_tabs:rank(n=${tabs.length})`
      );
    } catch (e) {
      lazy.console.warn("[Tool] getOpenTabs topic embedding failed", e);
    }
  }

  const recentTabs = tabs.slice(0, MAX_TABS);

  // Tab titles are truncated to 100 characters and therefore not expected to
  // contain enough untrusted data for a prompt injection attack.
  conversation.securityProperties.setPrivateData();
  lazy.console.log("[Tool] getOpenTabs", recentTabs);

  conversation.addSeenUrls(recentTabs.map(({ url }) => url));

  ChromeUtils.addProfilerMarker(
    "SmartWindow",
    { startTime },
    `Tool:get_open_tabs(${recentTabs.length})`
  );

  return recentTabs;
}

/**
 * Tool entrypoint for search_browsing_history.
 *
 * Parameters (defaults shown):
 * - searchTerm: ""        - string used for search
 * - startTs: null         - local ISO timestamp lower bound, or null
 * - endTs: null           - local ISO timestamp upper bound, or null
 * Detailed behavior and implementation are in SearchBrowsingHistory.sys.mjs.
 *
 * @param {object} toolParams
 *  The search parameters.
 * @param {string} toolParams.searchTerm
 *  The search string. If null or empty, semantic search is skipped and
 *  results are filtered by time range and sorted by last_visit_date and frecency.
 * @param {string|null} toolParams.startTs
 *  Optional local ISO-8601 start timestamp (e.g. "2025-11-07T09:00:00").
 * @param {string|null} toolParams.endTs
 *  Optional local ISO-8601 end timestamp (e.g. "2025-11-07T09:00:00").
 * @param {number} toolParams.historyLimit
 *  Maximum number of history results to return.
 * @param {ChatConversation} conversation
 * @returns {Promise<object>}
 *  A promise resolving to an object with the search term and history results.
 *  Includes `count` when matches exist, a `message` when none are found, or an
 *  `error` string on failure.
 */
export async function searchBrowsingHistory(toolParams, conversation) {
  // No security check, always allowed because it makes no external requests.
  const params = toolParams && typeof toolParams === "object" ? toolParams : {};

  const { searchTerm = "", startTs = null, endTs = null } = params;

  const result = await implSearchBrowsingHistory({
    searchTerm,
    startTs,
    endTs,
    historyLimit: MAX_HISTORY_RESULTS,
  });

  conversation.addSeenUrls(result.results.map(({ url }) => url));
  // Set history result on convsersation so it can be
  // sent to the content page.
  conversation.addHistoryResults(
    result.results.map(
      ({ url, title, favicon, thumbnail, visitDate, visitCount }) => ({
        url,
        title,
        favicon,
        thumbnail,
        visitDate,
        visitCount,
      })
    )
  );

  // The model only needs link metadata. thumbnail is a heavy, page-controlled
  // field the UI renders from the history grid above, so keep it out of the
  // model-facing payload.
  result.results = result.results.map(
    ({ url, title, visitDate, visitCount, relevanceScore }) => ({
      url,
      title: sanitizeUntrustedContent(title),
      visitDate,
      visitCount,
      relevanceScore,
    })
  );

  conversation.securityProperties.setPrivateData();
  lazy.console.log("[Tool] searchBrowsingHistory", result);
  return result;
}

/**
 * Performs a web search using the browser's default search engine,
 * waits for the results page to load, and extracts its content.
 */
export class RunSearch {
  static NAVIGATION_TIMEOUT_MS = 15000;
  static CONTENT_SETTLE_MS = 2000;
  static MAX_CHARACTERS = 15000;

  static #ensureTabSelected(tab) {
    if (!tab.selected) {
      tab.documentGlobal.gBrowser.selectedTab = tab;
    }
  }

  /**
   * @param {object} [toolParams]
   * @param {BrowsingContext} browsingContext
   * @param {ChatConversation} conversation
   * @returns {Promise<string>}
   */
  static async runSearch(toolParams, browsingContext, conversation) {
    // No security check, always allowed because we assume that the search
    // provider is trusted.

    // Decide if we'll use the user message verbatim as the search query or generate one
    let query;
    if (toolParams.query) {
      query = toolParams.query;
    } else {
      const recentUserMessages = await ChatStore.getMostRecentMessages(
        MESSAGE_ROLE.USER,
        1
      );
      if (!recentUserMessages.length) {
        return "Error: no user messages stored to user as the search query.";
      }
      query = recentUserMessages[0].content.body;
    }

    if (!query || typeof query !== "string" || !query.trim()) {
      return "Error: a non-empty search query is required.";
    }

    if (!browsingContext) {
      return "Error: no browsingContext provided to perform search.";
    }

    const win = browsingContext.topChromeWindow;
    if (!win || win.closed) {
      return "Error: associated browser window not available or closed.";
    }

    // Get the original tab from the browsing context, not the currently selected tab
    const originalBrowser = browsingContext.embedderElement;
    let targetTab =
      originalBrowser && win.gBrowser?.getTabForBrowser(originalBrowser);

    if (targetTab) {
      // Switch to the original tab if it's different from currently selected
      RunSearch.#ensureTabSelected(targetTab);
    } else {
      return "Error: Original tab no longer exists, aborting search to avoid interfering with existing conversation.";
    }

    // If the original tab is the AI Window page, move to sidebar first
    if (lazy.AIWindow.isAIWindowContentPage(originalBrowser.currentURI)) {
      await RunSearch.#moveToSidebarIfNeeded(win, targetTab);

      // Ensure we're still on the correct tab after the await
      RunSearch.#ensureTabSelected(targetTab);
    }

    RunSearch.#showSearchingIndicator(win, true, query.trim());

    let result;
    try {
      await RunSearch.#performSearchAndWait(win, originalBrowser, query.trim());
      result = RunSearch.#extractSerpContent(originalBrowser, conversation);
    } catch (e) {
      console.error("[RunSearch] search failed:", e);
      result = `Error performing search for "${query}": ${e.message}`;
    } finally {
      RunSearch.#showSearchingIndicator(win, false, null);
    }

    // Mark the SERP entry as having user interaction so Back doesn't skip it
    // (browser.navigation.requireUserInteraction). The user asked for this
    // navigation via the assistant even if they never gesture on the SERP.
    const sh = originalBrowser.browsingContext?.sessionHistory;
    const entry = sh && sh.index >= 0 ? sh.getEntryAtIndex(sh.index) : null;
    if (entry) {
      entry.hasUserInteraction = true;
    }

    conversation.securityProperties.setPrivateData();
    conversation.securityProperties.setUntrustedInput();

    lazy.console.log("[Tool] runSearch", result);
    return result;
  }

  // TODO - this may be dead code. The fetch with history already yields a
  // searching state, and the sidebar implementation may not need this at all.
  // Revisit this in the future:
  // https://bugzilla.mozilla.org/show_bug.cgi?id=2016252 to find a more
  // concrete way to target what side bar needs to show the indicator, if any
  // at all. My guess is that this might be here because of the move to sidebar
  // implementation, and the indicator state does not "transfer over". Possibly
  // look into tapping into something more concrete like the conversation state
  // in the AIWindow store to trigger this kind of UI state instead of trying
  // to directly manipulate the sidebar UI from here.
  static #showSearchingIndicator(win, isSearching, searchQuery) {
    try {
      const sidebar = win.document.getElementById("ai-window-box");
      if (!sidebar) {
        return;
      }
      const aiBrowser = sidebar.querySelector("#ai-window-browser");
      if (!aiBrowser?.contentDocument) {
        return;
      }
      const aiWindow = aiBrowser.contentDocument.querySelector("ai-window");
      if (aiWindow?.showSearchingIndicator) {
        aiWindow.showSearchingIndicator(isSearching, searchQuery);
      }
    } catch {
      // Sidebar may not be available
    }
  }

  static async #moveToSidebarIfNeeded(win, tab) {
    await lazy.AIWindow.moveConversationToSidebar(win, tab);
  }

  /**
   * Navigates to the search results and waits for the page to finish loading.
   *
   * @param {Window} win
   * @param {XULElement} browser
   * @param {string} query
   */
  static async #performSearchAndWait(win, browser, query) {
    const navigationPromise = new Promise((resolve, reject) => {
      const timeout = lazy.setTimeout(() => {
        win.gBrowser.removeProgressListener(listener);
        reject(new Error("Navigation timed out"));
      }, RunSearch.NAVIGATION_TIMEOUT_MS);

      const listener = {
        QueryInterface: ChromeUtils.generateQI([
          "nsIWebProgressListener",
          "nsISupportsWeakReference",
        ]),
        onStateChange(_webProgress, _request, stateFlags) {
          const complete =
            Ci.nsIWebProgressListener.STATE_STOP |
            Ci.nsIWebProgressListener.STATE_IS_NETWORK;
          if ((stateFlags & complete) === complete) {
            lazy.clearTimeout(timeout);
            win.gBrowser.removeProgressListener(listener);
            resolve();
          }
        },
        onLocationChange() {},
        onProgressChange() {},
        onStatusChange() {},
        onSecurityChange() {},
        onContentBlockingEvent() {},
      };

      win.gBrowser.addProgressListener(listener);
    });

    await lazy.AIWindow.performSearch(query, win);
    await navigationPromise;
    await lazy.AIWindow.focusSidebar(win);

    // Allow JS rendering to settle
    await new Promise(r => lazy.setTimeout(r, RunSearch.CONTENT_SETTLE_MS));
  }

  /**
   * Run PageExtractor on the search engine page.
   *
   * @param {MozBrowser} browser
   * @param {ChatConversation} conversation
   * @returns {string}
   */
  static async #extractSerpContent(browser, conversation) {
    const windowContext = browser.browsingContext?.currentWindowContext;
    if (!windowContext) {
      return "Error: could not access search results page content.";
    }

    /** @type {string} */
    let text;
    /** @type {PageExtractorParent} */
    const pageExtractor = await windowContext.getActor("PageExtractor");
    try {
      const result = await pageExtractor.getText({
        sufficientLength: RunSearch.MAX_CHARACTERS,
        cleanWhitespace: true,
        removeBoilerplate: false,
        sourceUrl: browser.currentURI?.spec,
      });
      if (!result) {
        return "No content could be extracted from the search results page.";
      }
      text = result.text;
      conversation.addSeenUrls(result.links);
      conversation.addSerpUrlsForAnonymousFetch(result.links);
    } catch {
      return "Error: failed to extract search results content.";
    }

    const url = browser.currentURI?.spec || "unknown";

    return `Search results from ${url}:\n\n${text}`;
  }
}

/**
 * Awaits `promise`, but rejects early with an AbortError if `signal` aborts
 * first. A late rejection from `promise` (e.g. once an abort tears down the
 * page it was reading) is swallowed so it is not reported as unhandled.
 *
 * @param {Promise<any>} promise
 * @param {AbortSignal} [signal]
 * @returns {Promise<any>}
 */
function raceAbort(promise, signal) {
  if (!signal) {
    return promise;
  }
  // Keep a post-abort rejection from surfacing as an unhandled rejection.
  promise.catch(() => {});
  return new Promise((resolve, reject) => {
    const onAbort = () => reject(new DOMException("Aborted", "AbortError"));
    if (signal.aborted) {
      onAbort();
      return;
    }
    signal.addEventListener("abort", onAbort, { once: true });
    promise.then(
      value => {
        signal.removeEventListener("abort", onAbort);
        resolve(value);
      },
      error => {
        signal.removeEventListener("abort", onAbort);
        reject(error);
      }
    );
  });
}

/**
 * Class for handling page content extraction with configurable modes and limits.
 */
export class GetPageContent {
  static MAX_CHARACTERS = 10000;

  /**
   * Generous until telemetry shows real transcript lengths. 2^16
   * because `text_length` is rounded down to a power of two, so its 65536
   * bucket counts exactly the truncated transcripts.
   */
  static MAX_TRANSCRIPT_CHARACTERS = 65536;

  /**
   * getPageContent's results as plain text. A failed read contributes the
   * sentence explaining it, so callers that must tell the two apart want
   * getPageContent itself.
   *
   * @param {object} toolParams
   * @param {string[]} toolParams.url_list
   * @param {AbortSignal} [toolParams.signal]
   * @param {ChatConversation} conversation
   * @returns {Promise<Array<string>>}
   */
  static async getPageContentText(toolParams, conversation) {
    const results = await GetPageContent.getPageContent(
      toolParams,
      conversation
    );
    return results.map(result => result.content);
  }

  /**
   * Tool entrypoint for get_page_content.
   *
   * One result per requested URL, in the order requested. `content` is the
   * extracted text on success and a sentence explaining what went wrong
   * otherwise, so `ok` is the only way to tell the two apart: callers that
   * feed this to a model must not let a failure reach it as page text.
   *
   * @param {object} toolParams
   * @param {string[]} toolParams.url_list
   * @param {AbortSignal} [toolParams.signal] - Cancels in-flight extractions
   *   (and tears down any headless browser) when it aborts.
   * @param {number} [toolParams.settleDelayMs] - Extra wait for headless loads.
   * @param {ChatConversation} conversation
   * @returns {Promise<Array<{url: ?string, ok: boolean, content: string}>>}
   */
  static async getPageContent(
    { url_list, signal, settleDelayMs },
    conversation
  ) {
    // Sanitize the inputs from the language model:
    if (!Array.isArray(url_list)) {
      return [
        {
          url: null,
          ok: false,
          content: "Error: the url_list argument must be an array of strings.",
        },
      ];
    }
    // This is a decision table for allowing and blocking fetches on the configuration of the
    // SecurityProperties and the URLs. Tab URLs don't do any new page loads. Mention urls
    // have been added by the user so they should be allowed. SERP urls came from a
    // trusted search provider's SERP and are eligible for an anonymous fetch.
    // AITab URLs are an exception, reading the contents of an open
    // about:smartpage tab extracts content locally and the content for the
    // AITab is also stored locally, so there is no risk of data exfiltration.
    // All other URLs are restricted when both private and untrusted data has been seen.
    //
    // │ Flags               │ tab urls │ mention urls │ serp urls          │ any urls │
    // ├─────────────────────┼──────────┼──────────────┼────────────────────┼──────────┤
    // │ Private only        │ ALLOW    │ ALLOW        │ ALLOW              │ ALLOW    │
    // │ Untrusted only      │ ALLOW    │ ALLOW        │ ALLOW              │ ALLOW    │
    // │ Private + Untrusted │ ALLOW    │ ALLOW        │ ALLOW (anonymous)  │ BLOCK    │

    // Collect these one time before the loop below since it must iterate through
    // all of the conversations and collect a new Set of mentions.
    const mentionedUrls = conversation.getAllMentionURLs();

    const results = await Promise.all(
      url_list.map(async (url, index) => {
        if (!isAllowedURLProtocol(url) && !isSmartPageURL(URL.parse(url))) {
          return { url, ok: false, content: "This URL is not allowed: " + url };
        }
        const startTime = ChromeUtils.now();
        try {
          const { ok, content } =
            await GetPageContent.#getPageContentsForSingleURL(
              url,
              mentionedUrls,
              conversation,
              signal,
              settleDelayMs
            );
          ChromeUtils.addProfilerMarker(
            "SmartWindow",
            { startTime },
            `Tool:get_page_content(${url})`
          );
          return { url, ok, content };
        } catch (error) {
          if (signal?.aborted) {
            return {
              url,
              ok: false,
              content: `Content from ${url_list[index]}:\n\n(Page read canceled after a timeout — answer using the results you have.)`,
            };
          }
          if (error?.name === "TimeoutError") {
            lazy.console.log("[Tool] getPageContent timed out", error);
            return {
              url,
              ok: false,
              content: `The page at ${url_list[index]} did not finish loading in time, so its content is unavailable. Do not retry it.`,
            };
          }
          if (error?.name === "BlockedError") {
            lazy.console.log("[Tool] getPageContent was blocked", error);
            return {
              url,
              ok: false,
              content: `The page at ${url_list[index]} appears to block automated access (e.g. a bot-detection challenge), so its content is unavailable. Do not retry it.`,
            };
          }
          console.error(error);
          return {
            url,
            ok: false,
            content: `Could not retrieve the content for the page: ${url_list[index]}`,
          };
        }
      })
    );
    lazy.console.log("[Tool] getPageContent", results);

    return results;
  }

  /**
   * Whether getPageContent would surface content for `url` in this
   * conversation (same allow/deny logic). Lets callers gate other
   * page-derived data (e.g. AITab's per-tab og:image) on the same decision.
   *
   * @param {string} url
   * @param {ChatConversation} conversation
   * @returns {boolean}
   */
  static isContentAllowed(url, conversation) {
    if (!isAllowedURLProtocol(url)) {
      // Only http/https pages may be exposed to the LLM at all; internal
      // schemes (about:, chrome:, file:, ...) stay out regardless of
      // conversation state.
      return false;
    }
    if (
      GetPageContent.getTabWithURL(url) ||
      conversation.getAllMentionURLs().has(url)
    ) {
      // The user deliberately brought this page into the conversation —
      // it is open as a tab or was mentioned by them — so reading it
      // reflects direct user intent rather than a model-chosen fetch.
      return true;
    }
    if (
      conversation.securityProperties.untrustedInput &&
      conversation.securityProperties.privateData &&
      !conversation.serpUrlsForAnonymousFetch.has(url)
    ) {
      // Anything else requires a headless network fetch of a URL the user
      // never opened or mentioned. When the conversation holds both untrusted
      // input (a prompt injection could have chosen this URL) and private
      // data (something worth stealing), such a fetch is a potential
      // exfiltration channel, so it is denied. SERP URLs are exempt because
      // they get an anonymous fetch path that carries no user identity.
      return false;
    }
    // A headless fetch is acceptable here: without the untrusted + private
    // combination above, there is either no injected URL choice or no private
    // data for it to leak.
    return true;
  }

  /**
   * Search through all AI Windows to find the tab with the matching URL.
   *
   * @param {string} url
   * @returns {Tab | null}
   */
  static getTabWithURL(url) {
    for (const win of lazy.BrowserWindowTracker.orderedWindows) {
      if (!lazy.AIWindow.isAIWindowActive(win) || win.closed || !win.gBrowser) {
        continue;
      }

      for (const tab of win.gBrowser.tabs) {
        if (tab?.linkedBrowser?.currentURI?.spec === url) {
          return tab;
        }
      }
    }

    return null;
  }

  /**
   * @param {string} url
   * @param {Set<string>} mentionedUrls
   * @param {ChatConversation} conversation
   * @param {AbortSignal} [signal] - Cancels the extraction (and tears down any
   *   headless browser) when it aborts.
   * @param {number} [settleDelayMs] - Extra wait for headless loads.
   *
   * @returns {Promise<{ok: boolean, content: string}>}
   *   ok is false when content is a failure description rather than page text.
   */
  static async #getPageContentsForSingleURL(
    url,
    mentionedUrls,
    conversation,
    signal,
    settleDelayMs
  ) {
    if (signal?.aborted) {
      throw new DOMException("Aborted", "AbortError");
    }
    // First try to get the contents from an existing tab. This is always allowed from
    // a security perspective as it doesn't involve a network request, so there is
    // no risk for data exfiltration.
    const tab = GetPageContent.getTabWithURL(url);
    if (tab) {
      // Extract the tab contents.
      const currentWindowContext =
        tab.linkedBrowser.browsingContext?.currentWindowContext;

      if (!currentWindowContext) {
        return {
          ok: false,
          content: `Cannot access content from the following webpage:\n - Title: ${sanitizeUntrustedContent(tab.label)}\n - URL: ${url}.`,
        };
      }

      // Extract page content using PageExtractor
      const pageExtractor =
        await currentWindowContext.getActor("PageExtractor");

      return GetPageContent.#runExtraction(
        pageExtractor,
        conversation,
        `${sanitizeUntrustedContent(tab.label)} (${url})`,
        url,
        signal
      );
    }

    // Fetch the page headlessly since it's not loaded as a tab. This requires elevated
    // security permissions since an external network request is required, and is a
    // risk for the exfiltration of private data. If the URL is mentioned by the user
    // then the security properties check is bypassed here. URLs in the serp URL
    // ledger get a anonymous fetch path that does not carry user identity.

    let label = url; // For headless fetches, use the URL as the label since we don't have a tab title.
    if (
      !mentionedUrls.has(url) &&
      conversation.securityProperties.untrustedInput &&
      conversation.securityProperties.privateData
    ) {
      if (conversation.serpUrlsForAnonymousFetch.has(url)) {
        return PageExtractorParent.getHeadlessExtractor({
          urlString: url,
          callback: (pageExtractor, traceId) =>
            GetPageContent.#runExtraction(
              pageExtractor,
              conversation,
              label,
              url,
              signal,
              traceId
            ),
          anonymousFetch: true,
          settleDelayMs,
          signal,
        });
      }
      return {
        ok: false,
        content:
          `Access is not allowed for ${url} because of untrusted and private content ` +
          "in the conversation.",
      };
    }

    return PageExtractorParent.getHeadlessExtractor({
      urlString: url,
      callback: (pageExtractor, traceId) =>
        GetPageContent.#runExtraction(
          pageExtractor,
          conversation,
          label,
          url,
          signal,
          traceId
        ),
      settleDelayMs,
      signal,
    });
  }

  /**
   * Main extraction function.
   * label is of form `{tab.title} ({tab.url})`.
   *
   * @param {PageExtractorParent} pageExtractor
   * @param {ChatConversation} conversation
   * @param {string} label
   * @param {string} sourceUrl
   * @param {AbortSignal} [signal] - Rejects the extraction early if it aborts,
   *   which lets the headless browser hosting the read be torn down promptly.
   * @param {TraceId} [traceId] - Correlates this extraction with the enclosing
   *   headless-extractor profiler marker.
   * @returns {Promise<{ok: boolean, content: string}>}
   *  A promise resolving to a formatted string containing the page content
   *  with mode and label information, or (with ok false) a failure message
   *  if no content is available.
   */
  static async #runExtraction(
    pageExtractor,
    conversation,
    label,
    sourceUrl,
    signal,
    traceId
  ) {
    const extraction = await raceAbort(
      pageExtractor.getText(
        {
          sufficientLength: GetPageContent.MAX_CHARACTERS,
          sufficientTranscriptLength: GetPageContent.MAX_TRANSCRIPT_CHARACTERS,
          cleanWhitespace: true,
          removeBoilerplate: true,
          sourceUrl,
        },
        traceId
      ),
      signal
    );

    if (!extraction) {
      return {
        ok: false,
        content: `get_page_content returned no content for ${label}.`,
      };
    }

    const { text, links } = extraction;
    conversation.addSeenUrls(links);

    // If an extraction succeeds set the security properties.
    // The page content is private since it uses a web page load that has credentials.
    // The information is untrusted since it's arbitrary web content.
    conversation.securityProperties.setPrivateData();
    conversation.securityProperties.setUntrustedInput();

    if (!text?.trim()) {
      return {
        ok: false,
        content: `get_page_content returned no content for ${label}.`,
      };
    }

    return { ok: true, content: `Content from ${label}:\n\n${text}` };
  }
}

/**
 * Returns Firefox settings navigation entries semantically relevant to the query.
 * No conversation parameter is needed.
 *
 * @param {object} toolParams
 * @param {string} toolParams.query
 * @returns {Promise<Array<{url, label, breadcrumb, description, similarity}>>}
 */
export async function getNavigationInfo(toolParams) {
  const params = toolParams && typeof toolParams === "object" ? toolParams : {};
  const { query = "" } = params;

  if (!query.trim()) {
    return [];
  }

  // No flags set: results are static browser UI metadata bundled with Firefox.
  // No network requests are made and no user data is read, so there is no
  // privacy risk and no untrusted content that could carry a prompt injection.
  return lazy.SmartWindowNavigationInfo.getRelevantNavigation(query);
}

/**
 * Retrieves the summaries of all saved memories
 *
 * @param {ChatConversation} conversation
 * @returns {Promise<Array<string>>}
 */
export async function getUserMemories(conversation) {
  // No security check, always allowed because it makes no external requests.
  const memories = await lazy.MemoriesManager.getAllMemories();

  const result = memories.map(memory => memory.memory_summary);
  // Memory summaries are private user data. They are truncated to 100
  // characters, so they are not considered untrusted input.
  conversation.securityProperties.setPrivateData();
  lazy.console.log("[Tool] getUserMemories", result);
  return result;
}

/**
 * Saves a memory the user explicitly asked to be remembered. If the model-inferred
 * containsPersonallyIdentifiableInfo flag is true, the memory will not be saved
 *  and an error message will be returned.
 *
 * @param {object} toolParams
 * @param {string} toolParams.memorySummary
 * @param {boolean} toolParams.containsPersonallyIdentifiableInfo
 * @param {ChatConversation} conversation
 * @returns {Promise<string>}
 */
export async function addMemory(
  { memorySummary, containsPersonallyIdentifiableInfo },
  conversation
) {
  // Until UI confirmation is implemented, we will not allow saving memories when untrusted
  //  input is present in the conversation. This is to mitigate writes of attacker-influenced content
  // into durable storage that is later re-injected into the user's future conversations as trusted context.
  if (conversation.securityProperties.untrustedInput) {
    return "Memory cannot be saved when untrusted content is present in the conversation.";
  }
  if (containsPersonallyIdentifiableInfo) {
    return "Error: Failed to save memory: Memory contains personally identifiable information.";
  }
  const result = await lazy.MemoriesManager.saveRequestedMemory(memorySummary);
  if (!result.ok) {
    return `Error: Failed to save memory: ${result.reason}`;
  }

  lazy.MemoriesManager.enrichExistingMemory(
    result.memory.id,
    memorySummary
  ).catch(e => lazy.console.error("[Tool] addMemory classify failed", e));

  lazy.console.log("[Tool] addMemory", result.memory, "Action:", result.action);
  return `Memory ${result.action}: "${result.memory.memory_summary}"`;
}

/**
 * Stores a generated page. A new page gets a slug of its own and belongs to
 * `conversation`; a modification is written as the next version of the slug it
 * revised, keeps the chat that owns it, and the versions it replaces are
 * dropped: only one is kept for now.
 *
 * @param {AITabResult} result - From AITab.generateAITab.
 * @param {ChatConversation} conversation - The chat that asked for the page.
 * @param {boolean} isModification - Whether `result` revises a stored page.
 * @returns {Promise<{uuid: string, convId: string, toolConvId: ?string,
 *   slug: string, version: number, title: string, createdAt: number,
 *   updatedAt: number, context: AITabContext, components: AITabResult,
 *   localState: ?object}>}
 *   The row that was written: `uuid` identifies this one version, `slug` the
 *   page across every version of it.
 */
async function persistAITabPage(
  { metadata, surface },
  conversation,
  isModification
) {
  const { context, convId, toolConvId, ...pageMetadata } = metadata;
  const page = {
    convId: convId ?? conversation.id,
    toolConvId,
    slug: metadata.id,
    title: metadata.title,
    context,
    components: { metadata: pageMetadata, surface },
  };

  if (!isModification) {
    return lazy.AITabStore.create(page);
  }

  const stored = await lazy.AITabStore.edit(page);

  // Nothing loads an older version yet, so keeping them would leave behind a
  // full copy of the page once per revision. Best-effort: the version just
  // written is already the one that resolves by slug, so a failed prune leaves
  // a stale row rather than a broken page.
  await lazy.AITabStore.deleteVersionsBefore(stored.slug, stored.version).catch(
    e =>
      lazy.console.error("[Tool] aiTab could not prune old versions", e.message)
  );

  return stored;
}

/**
 * How a generate_aitab call was requested, for telemetry.
 *
 * @param {ChatConversation} conversation
 * @param {boolean} isEdit - Whether the call modifies an existing page, which
 *   the tab menus never ask for.
 * @returns {"tab_list"|"tab_group"|"chat"|"chat_followup"}
 */
function getAITabInitiator(conversation, isEdit) {
  if (!isEdit && conversation.aitabSource) {
    return conversation.aitabSource;
  }
  return conversation.lastSubmitType == "follow-up" ? "chat_followup" : "chat";
}

const INTERNAL_ERROR_GUIDANCE =
  "Please surface to the user that there was an internal error, but the user may try again.";

// What the chat model should do next for each AITab error code. Codes not
// listed are internal failures.
const AITAB_ERROR_GUIDANCE = Object.freeze({
  content_related: "Please surface the error to the user.",
  no_content:
    "Call the tool again with the pages or content to build from, or ask the user what the page should be about.",
  nothing_to_change:
    "Call the tool again with modify_instructions describing the change, or ask the user what they would like changed.",
  page_not_found:
    "Use the slug from an earlier successful page result in this conversation. If there is none, do not guess; create a new page instead or ask the user which page they mean.",
  conversation_unavailable:
    "Tell the user this page can no longer be edited, and offer to generate a new page from the same sources.",
  history_limit:
    "Tell the user this page has reached its edit limit, and offer to generate a new page instead. Do not try to modify it again.",
  pages_unreadable:
    "Tell the user the pages could not be read and suggest trying different pages. Do not retry with the same pages.",
  canceled:
    "The user canceled page generation. Do not try again unless they ask.",
});

/**
 * Whether the modify_slug field in the tool params is configured
 * for an AITab modification
 *
 * @param {string} modify_slug
 *
 * @returns {boolean}
 */
export function isAITabModification(modify_slug) {
  return typeof modify_slug == "string" && !!modify_slug.trim();
}

/**
 * @param {object} toolParams
 * @param {string[]} [toolParams.url_list]
 * @param {string} [toolParams.focus]
 * @param {string} [toolParams.raw_content]
 * @param {string} [toolParams.modify_slug]
 * @param {string} [toolParams.modify_instructions]
 * @param {ChatConversation} conversation
 * @param {AbortSignal} [signal] - Cancels in-flight page extractions.
 * @returns {Promise<
 *   {success: true,
 *    toolResult: {message: string, page: {slug: string}},
 *    uiData?: {uiType: "aitab",
 *      properties: {state: "choose", viewerURL: string, title: string}}} |
 *   {success: false, toolResult: string}
 * >} The model response on success, plus UI data for a new page (a
 *   modification has none), or an error message on failure.
 */
export async function createAITab(
  { url_list, focus, raw_content, modify_slug, modify_instructions },
  conversation,
  signal
) {
  lazy.console.log(
    "[Tool] aiTab",
    JSON.stringify({ url_list, focus, modify_slug })
  );
  // Fetch the heading font while the page generates, so it is usually
  // installed by the time the page opens.
  lazy
    .getHeadingFont()
    .catch(error =>
      lazy.console.error("[Tool] Failed to prefetch the heading font", error)
    );
  // Whether this call revises an existing page or writes a new one, so every
  // message the model gets back names what actually happened.
  // Matches how generateAITab reads modify_slug, so a blank one is a create.
  const isModification = isAITabModification(modify_slug);
  const action = isModification ? "updated" : "created";
  const result = await lazy.AITab.generateAITab(
    {
      urlList: lazy.AITab.selectContextUrls(url_list),
      focus,
      rawContent: raw_content,
      modifySlug: modify_slug,
      modifyInstructions: modify_instructions,
      howInitiated: getAITabInitiator(conversation, isModification),
      signal,
    },
    conversation
  );
  if (result.error) {
    const guidance =
      AITAB_ERROR_GUIDANCE[result.code] ?? INTERNAL_ERROR_GUIDANCE;
    return {
      success: false,
      toolResult: `The page could not be ${action}: ${result.error}. ${guidance}`,
    };
  }

  // The UI opens the page from its stored slug, so a page that never reached
  // the database would leave the user with a link that resolves to nothing.
  let stored;
  try {
    stored = await persistAITabPage(result, conversation, isModification);
  } catch (e) {
    lazy.console.error("[Tool] Failed to persist page", e.message);
    return {
      success: false,
      toolResult: `The page could not be ${action}: it could not be saved. Tell the user the page could not be saved. Do not try again.`,
    };
  }

  // slug addresses the page in its URL and is shared by every version, and
  // is what modify_slug takes to revise this page on a later turn. Lets a
  // later turn find this page from the conversation's tool messages.
  const page = { slug: stored.slug };

  if (isModification) {
    return {
      success: true,
      toolResult: { message: "The page was modified.", page },
    };
  }

  const viewerURL = lazy.AITab.buildViewerURL(stored.slug);

  // Mark the viewer URL as seen so the chat renders it as a trusted, labeled
  // link. An about:smartpage URL that was never seen is not trusted, so a page
  // name the model invented cannot be presented as a real page.
  conversation.addSeenUrls([viewerURL]);
  // Register the URL as a token so the model echoes the token and cannot alter
  // the slug; expandUrlTokens restores the exact URL when rendering the
  // assistant's reply.
  const token = conversation.convertUrlToToken(viewerURL);
  // Model output re-entering the prompt, so cap the length. Brackets would
  // break out of the markdown link text and expose the URL.
  const title = sanitizeUntrustedContent(
    result.metadata?.title || "the page",
    true // truncateOnly: the link label must stay readable.
  ).replace(/[[\]]/g, "");

  return {
    success: true,
    toolResult: {
      message: `The page was created. Link the user to it as [${title}](§url_token: ${token}§).`,
      page,
    },
    uiData: {
      uiType: UI_TYPES.AITAB,
      properties: { state: "choose", viewerURL, title },
    },
  };
}

// No securityProperties / trust flags: skill prompts are Remote Settings
// content and carry the same trust level as the system prompt itself.
export async function getSkill({ toolParams, model }) {
  return getSkillPrompt(toolParams?.name, model);
}

/**
 * Counts open http(s) tabs across all active AI windows. Used for
 * browser_action_submit telemetry.
 *
 * @returns {number}
 */
function countOpenAIWindowTabs() {
  let count = 0;
  for (const win of lazy.BrowserWindowTracker.orderedWindows) {
    if (!lazy.AIWindow.isAIWindowActive(win) || win.closed || !win.gBrowser) {
      continue;
    }
    for (const tab of win.gBrowser.tabs) {
      const url = tab.linkedBrowser?.currentURI?.spec;
      if (isAllowedURLProtocol(url) && !isNewPageUrl(url)) {
        count += 1;
      }
    }
  }
  return count;
}

/**
 * Determines the telemetry trigger for a manage_tabs invocation.
 *
 * @param {ChatConversation} conversation
 * @param {string} action
 * @returns {"unsupported" | "tab_mention" | "description"}
 */
function getActionTrigger(conversation, action) {
  if (!TAB_ACTIONS.includes(action)) {
    return "unsupported";
  }

  const mentions = conversation.getLatestUserMentionCount();
  return mentions > 0 ? "tab_mention" : "description";
}

/**
 * Tool entrypoint for manage_tabs. Dispatches to a per-action handler
 * based on `action`.
 *
 * @param {object} toolParams
 * @param {ChatConversation} conversation
 * @param {string} [mode] - Location/mode of the AI Window for telemetry
 * @param {string} [model] - Identifier of the model that invoked the tool
 * @param {string} [toolCallId] - Id of the tool call, used to register the
 *   confirmation's token -> permanentKey map
 * @returns {Promise<{ toolResult: object, uiData: ?object }>}
 *   `toolResult` is appended as the body of the `role: "tool"` message sent
 *   to the model. `uiData` is attached to the assistant message for UI
 *   rendering, or `null` to skip the UI attachment.
 */
export async function manageTabs(
  toolParams,
  conversation,
  mode = "",
  model = "",
  toolCallId = ""
) {
  const params = toolParams && typeof toolParams === "object" ? toolParams : {};
  const {
    action,
    ask_confirmation = true,
    url_tokens = [],
    label = "",
  } = params;

  const actionTrigger = getActionTrigger(conversation, action);

  const baseTelemetryInfo = {
    location: mode,
    chat_id: conversation.id,
    message_seq: conversation.messageCount,
    model,
    prompt_version: conversation.systemPromptVersion,
    action: TAB_ACTIONS.includes(action) ? action : "unsupported",
    trigger: actionTrigger,
  };

  lazy.ToolUITelemetry.recordBrowserActionSubmit({
    ...baseTelemetryInfo,
    tabs_open: countOpenAIWindowTabs(),
    mentions: conversation.getLatestUserMentionCount(),
    submit_type: conversation.lastSubmitType || "",
  });

  if (actionTrigger === "unsupported") {
    lazy.ToolUITelemetry.recordBrowserActionComplete({
      ...baseTelemetryInfo,
      result: "error",
      tabs_affected: 0,
      undo_available: false,
      error: "unsupported_action",
    });
    return {
      toolResult: `Error: Unsupported manage_tabs action "${action}".`,
      uiData: null,
    };
  }

  if (!Array.isArray(url_tokens)) {
    lazy.ToolUITelemetry.recordBrowserActionComplete({
      ...baseTelemetryInfo,
      result: "error",
      tabs_affected: 0,
      undo_available: false,
      error: "invalid_url_tokens",
    });
    return {
      toolResult: "Error: url_tokens must be an array.",
      uiData: null,
    };
  }

  const validUrls = new Set(
    url_tokens.filter(u => typeof u === "string" && isAllowedURLProtocol(u))
  );

  if (!validUrls.size) {
    lazy.ToolUITelemetry.recordBrowserActionComplete({
      ...baseTelemetryInfo,
      result: "no_match",
      tabs_affected: 0,
      undo_available: false,
      error: "no_valid_urls",
    });
    return {
      toolResult: "Error: No valid URLs were provided to manage_tabs.",
      uiData: null,
    };
  }

  return manageTabsAction(
    {
      action,
      validUrls,
      ask_confirmation,
      label,
      baseTelemetryInfo,
      toolCallId,
    },
    conversation
  );
}

export const toolFns = {
  getOpenTabs,
  searchBrowsingHistory,
  getUserMemories,
  getNavigationInfo,
  createAITab,
  manageTabs,
  addMemory,
  getSkill,
};
