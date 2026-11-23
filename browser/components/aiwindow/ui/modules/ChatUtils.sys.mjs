/*
 This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

const lazy = XPCOMUtils.declareLazy({
  CryptoUtils: "moz-src:///services/crypto/modules/utils.sys.mjs",
});

import { MESSAGE_ROLE, TOOL_RESULT_TYPE } from "./AIWindowConstants.sys.mjs";
import { ChatConversation } from "./ChatConversation.sys.mjs";
import { ChatMessage, ChatHistoryResult } from "./ChatMessage.sys.mjs";
import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

/** @typedef {import("./ChatConversation.sys.mjs").PooledHistoryResult} PooledHistoryResult */
/** @typedef {import("./ChatConversation.sys.mjs").Citation} Citation */

/**
 *  Gets the URL of the currently selected tab of a window.
 *  Primarily used to retrieve the current tab's url for use in
 *  ChatMessage.pageUrl and message context chips.
 *
 *  @param {Window} window
 *
 *  @returns {?URL}
 */
export function getCurrentTabUrl(window) {
  return window?.gBrowser?.selectedTab?.linkedBrowser?.currentURI;
}

/**
 * Creates a 12 characters GUID with 72 bits of entropy.
 *
 * @returns {string} A base64url encoded GUID.
 */
export function makeGuid() {
  return ChromeUtils.base64URLEncode(lazy.CryptoUtils.generateRandomBytes(9), {
    pad: false,
  });
}

/**
 * Parse a conversation row from the database into a ChatConversation
 * object.
 *
 * @param {object} row - The database row to parse.
 * @returns {ChatConversation} The parsed conversation object.
 */
export function parseConversationRow(row) {
  const seenUrlsArray = parseJSONOrNull(row.getResultByName("seen_urls"));
  const serpUrlsForAnonymousFetchArray = parseJSONOrNull(
    row.getResultByName("serp_urls_for_anonymous_fetch")
  );
  return new ChatConversation({
    id: row.getResultByName("conv_id"),
    title: row.getResultByName("title"),
    description: row.getResultByName("description"),
    pageUrl: URL.parse(row.getResultByName("page_url")),
    pageMeta: parseJSONOrNull(row.getResultByName("page_meta")),
    createdDate: row.getResultByName("created_date"),
    updatedDate: row.getResultByName("updated_date"),
    status: row.getResultByName("status"),
    securityProperties: parseJSONOrNull(
      row.getResultByName("security_properties")
    ),
    seenUrls: Array.isArray(seenUrlsArray) ? seenUrlsArray : [],
    serpUrlsForAnonymousFetch: Array.isArray(serpUrlsForAnonymousFetchArray)
      ? serpUrlsForAnonymousFetchArray
      : [],
    memoriesToggled: row.getResultByName("memories_toggled"),
  });
}

/**
 * Parse message rows from the database into an array of ChatMessage
 * objects.
 *
 * @param {object} rows - The database rows to parse.
 * @returns {Array<ChatMessage>} The parsed message objects.
 */
export function parseMessageRows(rows) {
  return rows.map(row => {
    const toolResults =
      parseJSONOrNull(row.getResultByName("tool_results")) ?? {};
    return new ChatMessage({
      id: row.getResultByName("message_id"),
      createdDate: row.getResultByName("created_date"),
      parentMessageId: row.getResultByName("parent_message_id"),
      revisionRootMessageId: row.getResultByName("revision_root_message_id"),
      ordinal: row.getResultByName("ordinal"),
      isActiveBranch: !!row.getResultByName("is_active_branch"),
      role: row.getResultByName("role"),
      modelId: row.getResultByName("model_id"),
      params: parseJSONOrNull(row.getResultByName("params")),
      usage: parseJSONOrNull(row.getResultByName("usage")),
      content: parseJSONOrNull(row.getResultByName("content")),
      convId: row.getResultByName("conv_id"),
      pageUrl: URL.parse(row.getResultByName("page_url")),
      turnIndex: row.getResultByName("turn_index"),
      memoriesEnabled: !!row.getResultByName("memories_enabled"),
      memoriesFlagSource: row.getResultByName("memories_flag_source"),
      memoriesApplied: parseJSONOrNull(row.getResultByName("memories_applied")),
      webSearchQueries: parseJSONOrNull(
        row.getResultByName("web_search_queries")
      ),
      pageHistoryDeleted: !!row.getResultByName("page_history_deleted"),
      toolUIData: toolResults[TOOL_RESULT_TYPE.TOOL_UI]?.[0],
      historyResults: toolResults[TOOL_RESULT_TYPE.HISTORY_RESULTS],
      citations: toolResults[TOOL_RESULT_TYPE.CITATIONS],
    });
  });
}

/**
 * Parse conversation rows from the database into an array of ChatHistoryResult
 * objects.
 *
 * @param {Array<object>} rows - The database rows to parse.
 * @returns {Array<ChatHistoryResult>} The parsed chat history result entries
 */
export function parseChatHistoryViewRows(rows) {
  return rows.map(row => {
    const urls = (parseJSONOrNull(row.getResultByName("urls")) ?? [])
      .filter(url => url && url.trim())
      .map(url => new URL(url.trim()));

    return new ChatHistoryResult({
      convId: row.getResultByName("conv_id"),
      title: row.getResultByName("title"),
      createdDate: row.getResultByName("created_date"),
      updatedDate: row.getResultByName("updated_date"),
      urls,
    });
  });
}

/**
 * Try to parse a JSON string, returning null if it fails or the value is falsy.
 *
 * @param {string} value - The JSON string to parse.
 * @returns {object|null} The parsed object or null.
 */
export function parseJSONOrNull(value) {
  if (!value) {
    return null;
  }
  try {
    return JSON.parse(value);
  } catch (e) {
    return null;
  }
}

/**
 * Try to stringify a value if it is truthy, otherwise return null.
 *
 * @param {*} value - A value to JSON.stringify()
 *
 * @returns {string|null} - JSON string
 */
export function toJSONOrNull(value) {
  return value ? JSON.stringify(value) : null;
}

/**
 * Strip resolved page assets from a pooled record before persisting.
 * `image` (a moz-page-thumb:// URI) and `hasFavicon` are re-resolved lazily on
 * load, since the underlying thumbnail/favicon cache can be evicted.
 *
 * @param {PooledHistoryResult|Citation} record - A pooled record.
 * @returns {Omit<PooledHistoryResult|Citation, "image" | "hasFavicon">} A copy
 *   removing the resolved asset fields `image` and `hasFavicon`.
 */
export function stripResolvedAssets(record) {
  const persisted = { ...record };
  delete persisted.image;
  delete persisted.hasFavicon;
  return persisted;
}

/**
 * Converts the different types of message roles from
 * the database numeric type to a string label
 *
 * @param {number} role - The database numeric role type
 * @returns {string} - A human readable role label
 */
export function getRoleLabel(role) {
  switch (role) {
    case MESSAGE_ROLE.USER:
      return "User";

    case MESSAGE_ROLE.ASSISTANT:
      return "Assistant";

    case MESSAGE_ROLE.SYSTEM:
      return "System";

    case MESSAGE_ROLE.TOOL:
      return "Tool";
  }

  return "";
}

/**
 * Returns whether the sidebar should be open for a given tab state and the
 * sidebarOpenByDefault pref value. The state's keepSidebarOpen field drives
 * the decision:
 * - true: user explicitly opened the sidebar for this tab
 * - false: user explicitly closed it
 * - null/undefined: no explicit preference, defer to the pref
 *
 * @param {object|null|undefined} state - The tab state object
 * @param {boolean} sidebarOpenByDefault
 * @returns {boolean}
 */
export function getKeepSidebarOpenState(state, sidebarOpenByDefault) {
  const keepSidebarOpen = state?.keepSidebarOpen;
  return (
    keepSidebarOpen === true ||
    (keepSidebarOpen == null && sidebarOpenByDefault)
  );
}

/**
 * Picks only the schema-declared fields from a ChatMessage's tokens object.
 *
 * @param {object} tokens - The raw ChatMessage tokens object.
 * @returns {object|undefined} Normalized tokens with only schema-declared fields.
 */
function normalizeTokens(tokens) {
  if (!tokens) {
    return undefined;
  }
  return {
    search: tokens.search,
    existing_memory: tokens.existing_memory,
    followup: tokens.followup,
  };
}

/**
 * Picks only the schema-declared fields from a ChatMessage's toolUIData object.
 *
 * @param {object} toolUIData - The raw ChatMessage toolUIData object.
 * @returns {object|undefined} Normalized toolUIData with only schema-declared fields.
 */
function normalizeToolUIData(toolUIData) {
  if (!toolUIData) {
    return undefined;
  }
  return {
    uiType: toolUIData.uiType,
    toolCallId: toolUIData.toolCallId,
    properties: toolUIData.properties
      ? {
          originalUserPrompt: toolUIData.properties.originalUserPrompt,
          tabs: toolUIData.properties.tabs?.map(
            ({ linkedPanel, url, title, iconSrc, checked }) => ({
              linkedPanel,
              url,
              title,
              iconSrc,
              checked,
            })
          ),
        }
      : undefined,
  };
}

/**
 * Picks only the schema-declared fields from a message's tool_calls array.
 * `type` is renamed to `call_type` since `type` is a reserved keyword in
 * Glean's code generation (Bug 1945220).
 *
 * @param {Array<object>} toolCalls - The raw tool_calls array.
 * @returns {Array<object>|undefined} Normalized tool_calls with `call_type`
 *   in place of `type`.
 */
function normalizeToolCalls(toolCalls) {
  return toolCalls?.map(({ type: call_type, id, function: fn }) => ({
    id,
    call_type,
    function: fn ? { name: fn.name, arguments: fn.arguments } : undefined,
  }));
}

/**
 * Normalizes a ChatMessage content object for recording as a Glean object
 * metric. Only schema-declared fields are included to avoid schema-mismatch
 * errors if ChatMessage gains undeclared fields in the future (undeclared
 * fields cause Glean to drop the entire object). `type` fields are also
 * renamed to avoid reserved-keyword conflicts in Glean's code generation
 * (Bug 1945220).
 *
 * @param {object} content - The raw ChatMessage content object.
 * @returns {object|undefined} Normalized content with only schema-declared fields.
 */
function normalizeContent(content) {
  if (!content) {
    return undefined;
  }
  const { type: content_type, body, contextMentions } = content;

  let normalizedBody;
  if (typeof body === "string") {
    normalizedBody = { text: body };
  } else if (Array.isArray(body)) {
    normalizedBody = {
      result: body.map(item =>
        typeof item === "string" ? item : JSON.stringify(item)
      ),
    };
  } else if (body?.tool_calls) {
    normalizedBody = { tool_calls: normalizeToolCalls(body.tool_calls) };
  }

  return {
    content_type,
    tool_call_id: content.tool_call_id,
    body: normalizedBody,
    name: content.name,
    userContext: content.userContext
      ? { realTimeContext: content.userContext.realTimeContext }
      : undefined,
    contextMentions: contextMentions?.map(
      ({ type: mention_type, url, label, iconSrc }) => ({
        mention_type,
        url,
        label,
        iconSrc,
      })
    ),
    contextPageUrl: content.contextPageUrl,
  };
}

/**
 * Prepares a chat log for the feedback preview and for recording as a Glean
 * object metric. Explicitly picks only schema-declared fields from each
 * ChatMessage to avoid schema-mismatch errors from undeclared fields, and
 * normalizes content.body to always be an object (Glean object metrics
 * require a single declared type):
 *   - string bodies (text messages) become { text: body }
 *   - array bodies (tool result messages) become { result: string[] },
 *     stringifying any non-string items to satisfy the schema
 *   - object bodies (function messages) have tool_calls[].type renamed to
 *     call_type (Bug 1945220)
 *
 * convId is intentionally omitted: it's the same identifier recorded as
 * chat_id on regular interaction telemetry, which retains client_id, so
 * including it here would let this anonymized submission be correlated
 * back to identified telemetry.
 *
 * @param {object} chat - The raw chat log, e.g. { log: ChatMessage[] }.
 * @returns {object|null} Normalized chat log with only schema-declared fields.
 */
export function normalizeChatLog(chat) {
  if (!Array.isArray(chat?.log)) {
    return null;
  }

  return {
    log: chat.log.map(msg => ({
      id: msg.id,
      createdDate: msg.createdDate,
      parentMessageId: msg.parentMessageId,
      revisionRootMessageId: msg.revisionRootMessageId,
      ordinal: msg.ordinal,
      isActiveBranch: msg.isActiveBranch,
      role: msg.role,
      modelId: msg.modelId,
      content: normalizeContent(msg.content),
      pageUrl: msg.pageUrl,
      turnIndex: msg.turnIndex,
      memoriesEnabled: msg.memoriesEnabled,
      memoriesFlagSource: msg.memoriesFlagSource,
      memoriesApplied: msg.memoriesApplied,
      webSearchQueries: msg.webSearchQueries,
      followUpSuggestions: msg.followUpSuggestions,
      pageHistoryDeleted: msg.pageHistoryDeleted,
      tokens: normalizeTokens(msg.tokens),
      toolUIData: normalizeToolUIData(msg.toolUIData),
    })),
  };
}
