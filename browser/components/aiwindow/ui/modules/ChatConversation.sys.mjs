/*
 This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

import { MODEL_FEATURES } from "moz-src:///browser/components/aiwindow/models/Utils.sys.mjs";

import {
  constructRelevantMemoriesContextMessage,
  replaceUrlsWithTokens,
  resolveMentionUrls,
} from "moz-src:///browser/components/aiwindow/models/ChatUtils.sys.mjs";

import { DEFAULT_RELEVANT_MEMORIES_MESSAGE_COUNT } from "moz-src:///browser/components/aiwindow/models/memories/MemoriesConstants.sys.mjs";

import { getRoleLabel } from "./ChatUtils.sys.mjs";
import {
  CONVERSATION_STATUS,
  MESSAGE_ROLE,
  SYSTEM_PROMPT_TYPE,
} from "./AIWindowConstants.sys.mjs";
import {
  AssistantRoleOpts,
  ChatMessage,
  ToolRoleOpts,
  UserRoleOpts,
} from "./ChatMessage.sys.mjs";

import { EventEmitter } from "resource://gre/modules/EventEmitter.sys.mjs";
import { Conversation } from "moz-src:///browser/components/aiwindow/models/Conversation.sys.mjs";
import { consumeStreamChunk } from "moz-src:///browser/components/aiwindow/models/TokenStreamParser.sys.mjs";
import {
  stripUnresolvedUrlTokens,
  UrlTokenizer,
} from "moz-src:///browser/components/aiwindow/ui/modules/UrlTokenizer.sys.mjs";

/** @typedef {import("moz-src:///browser/components/aiwindow/models/SearchBrowsingHistory.sys.mjs").HistoryRow} HistoryRow */

/**
 * A pooled history result: the subset of `HistoryRow` fields the
 * `search_browsing_history` tool projects into the pool (Tools.sys.mjs) minus
 * `relevanceScore`, plus a localized `timestamp` and the resolved `image` and
 * `hasFavicon` asset fields.
 *
 * @typedef {Omit<HistoryRow, "relevanceScore"> & { timestamp?: string, image?: (string|null), hasFavicon?: boolean }} PooledHistoryResult
 */

/**
 * A web-search source rendered as a citation chip.
 *
 * @typedef {object} Citation
 * @property {string} url - The source URL
 * @property {string} [title] - The page title
 * @property {boolean} [hasFavicon] - Whether Places has a stored favicon
 */

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  convertTimestamp: "chrome://browser/content/firefoxview/helpers.mjs",
  ChatStore:
    "moz-src:///browser/components/aiwindow/ui/modules/ChatStore.sys.mjs",
  MemoriesManager:
    "moz-src:///browser/components/aiwindow/models/memories/MemoriesManager.sys.mjs",
  buildBrowserContextPrompt:
    "moz-src:///browser/components/aiwindow/models/PromptLoader.sys.mjs",
  loadPrompt:
    "moz-src:///browser/components/aiwindow/models/PromptLoader.sys.mjs",
  ToolUI: "moz-src:///browser/components/aiwindow/ui/modules/ToolUI.sys.mjs",
  CONFIRMATION_UI_TYPES:
    "moz-src:///browser/components/aiwindow/ui/modules/ToolUI.sys.mjs",
  isTabGroupMember: "chrome://browser/content/urlbar/SmartbarMentionUtils.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "fluentStrings", () => {
  return new Localization(["browser/firefoxView.ftl"], true);
});

ChromeUtils.defineLazyGetter(lazy, "console", function () {
  return console.createInstance({
    prefix: "ChatConversation",
  });
});

const CHAT_ROLES = [MESSAGE_ROLE.USER, MESSAGE_ROLE.ASSISTANT];
const RESTORABLE_ROLES = [...CHAT_ROLES, MESSAGE_ROLE.TOOL];

let _savedLoadPromptDescriptor = null;
export function _setLoadPromptForTesting(fn) {
  if (fn !== null) {
    _savedLoadPromptDescriptor = Object.getOwnPropertyDescriptor(
      lazy,
      "loadPrompt"
    );
    lazy.loadPrompt = async (...args) => {
      const result = await fn(...args);
      return typeof result === "string"
        ? { prompt: result, version: "" }
        : result;
    };
  } else if (_savedLoadPromptDescriptor) {
    // eslint-disable-next-line mozilla/valid-lazy
    Object.defineProperty(lazy, "loadPrompt", _savedLoadPromptDescriptor);
    _savedLoadPromptDescriptor = null;
  }
}

/**
 * A chat conversation. Adds chat orchestration (security tracking, URL
 * tokens, branching, realtime + memory injection, system prompt loading)
 * and chat UI (event emits, render filtering, tool UI state) on top of the
 * generic Conversation base.
 */
export class ChatConversation extends Conversation {
  title;
  description;
  pageUrl;
  pageMeta;
  status;
  activeBranchTipMessageId;

  #emitter = new EventEmitter();

  /**
   * Transient (not persisted): the submit_type of the most recent user
   * submission, used to send telemetry to later tool-result events.
   *
   * @type {?string}
   */
  lastSubmitType = null;

  /**
   * Transient (not persisted): where the most recent user submission asked
   * for an AI Tab page from (tab_list, tab_group or fxms), or null when it
   * was typed in the chat. Reported by AI Tab telemetry.
   *
   * @type {?string}
   */
  aitabSource = null;

  /**
   * Transient (not persisted): browser_action_submit telemetry context for
   * manage_tabs confirmation, keyed by toolCallId. Stashed when a tab action
   * is deferred for user confirmation and consumed when it resolves.
   *
   * @type {Map<string, object>} toolCallId -> browser_action_submit context
   */
  #pendingBrowserActionTelemetry = new Map();

  /**
   * Uncited memories embedded in the current prompt. Cleared after completion.
   *
   * @type {Array<object>}
   */
  promptEmbeddedMemories = [];

  /**
   * URL Tokenizer instance for sending URLs to LLM.
   * The tokens created by UrlTokenizer are cached to the conversation while
   * the conversation is loaded in memory. These tokens are in-memory only and not
   * serialized to storage.
   *
   * @type {UrlTokenizer}
   */
  urlTokenizer = null;

  /**
   * Conversation-level pool of history results keyed by URL, accumulated across
   * every `search_browsing_history` invocation in this conversation. A message
   * snapshots this pool when it completes (see `receiveResponse`), so any
   * assistant message that lists previously-searched URLs renders a history
   * grid — even when the model answered a follow-up from prior results without
   * re-invoking the tool. The pool itself is not persisted; instead each
   * message persists its own snapshot, and the pool is rehydrated from those
   * snapshots when the conversation is constructed from the database.
   *
   * @type {Map<string, object>}
   */
  #historyResultsPool = new Map();

  /**
   * Conversation-level pool of web-search citations keyed by URL, accumulated
   * across every `search_the_web` invocation in this conversation.
   *
   * @type {Map<string, Citation>}
   */
  #citationsPool = new Map();

  /**
   * URLs read by `search_the_web` during the current turn.
   *
   * @type {Set<string>}
   */
  #pendingCitationUrls = new Set();

  /**
   * Last browser-context string written; injectRealTimeContext skips
   * rewriting an identical one so the prompt-cache prefix stays stable.
   *
   * @type {string|null}
   */
  #lastBrowserContext = null;

  /**
   * @param {object} params
   * @param {string} [params.id]
   * @param {string} params.title
   * @param {string} params.description
   * @param {URL} params.pageUrl
   * @param {object} params.pageMeta
   * @param {number} [params.createdDate]
   * @param {number} [params.updatedDate]
   * @param {CONVERSATION_STATUS} [params.status]
   * @param {Array<ChatMessage>} [params.messages]
   * @param {boolean|null} [params.memoriesToggled]
   */
  constructor(params) {
    const {
      id = crypto.randomUUID(),
      title,
      description,
      pageUrl,
      pageMeta,
      createdDate = Date.now(),
      updatedDate = Date.now(),
      messages = [],
      seenUrls,
      serpUrlsForAnonymousFetch,
      memoriesToggled = null,
      securityProperties = null,
    } = params;

    super({
      id,
      createdDate,
      updatedDate,
      messages,
      seenUrls,
      serpUrlsForAnonymousFetch,
      feature: "chat",
      securityProperties,
    });

    this.urlTokenizer = new UrlTokenizer();
    this.title = title;
    this.description = description;
    this.pageUrl = pageUrl;
    this.pageMeta = pageMeta;
    this.rehydrateHistoryResultsPool();
    this.rehydrateCitationsPool();
    this.memoriesToggled = memoriesToggled;

    // transient: tracks the URL the current starter prompts were generated
    // for. Not persisted, only used while conversation is empty.
    this.transientStarterUrl = null;

    // transient: caches the last set of starter prompts generated for this
    // conversation so a tab switch-back can restore without re-fetching.
    // Not persisted, only meaningful while the conversation is empty.
    this.transientStarters = null;

    // transient: stores information about a cancelled confirmation dialog
    // that can be retried. Set when a website confirmation is auto-cancelled
    // due to a new user prompt. Not persisted.
    this.pendingRetry = null;

    // NOTE: Destructuring params.status causes a linter error
    this.status = params.status || CONVERSATION_STATUS.ACTIVE;

    // Seed the branch-tip cache so restored-from-DB conversations have it
    // set before the first turn.
    if (messages.length) {
      this.#updateActiveBranchTipMessageId();
    }
  }

  on(...args) {
    return this.#emitter.on(...args);
  }
  off(...args) {
    return this.#emitter.off(...args);
  }
  emit(...args) {
    return this.#emitter.emit(...args);
  }

  /**
   * Stash browser_action_submit telemetry context for a deferred tab action,
   * to be consumed when its confirmation resolves.
   *
   * @param {string} toolCallId
   * @param {object} telemetryInfo - browser_action_submit context
   */
  stashPendingBrowserActionTelemetry(toolCallId, telemetryInfo) {
    this.#pendingBrowserActionTelemetry.set(toolCallId, telemetryInfo);
  }

  /**
   * Retrieve and remove the stashed browser_action_submit context for a tool
   * call, if any.
   *
   * @param {string} toolCallId
   * @returns {object | undefined} The stashed context, or undefined if none.
   */
  takePendingBrowserActionTelemetry(toolCallId) {
    const telemetryInfo = this.#pendingBrowserActionTelemetry.get(toolCallId);
    this.#pendingBrowserActionTelemetry.delete(toolCallId);
    return telemetryInfo;
  }

  /**
   * Number of stashed browser_action_submit contexts awaiting a confirmation.
   * Exposed for tests.
   *
   * @type {number}
   */
  get pendingBrowserActionTelemetryCount() {
    return this.#pendingBrowserActionTelemetry.size;
  }

  /**
   * Converts a URL into a token. It first computes a base token from
   * the hostname and path parts, then appends a monotonically increasing number on the
   * end to make it unique. This token is cached to the conversation while
   * the conversation is loaded in memory. This token is in-memory only and not
   * serialized to storage.
   *
   * URL token analysis:
   * https://docs.google.com/document/d/1kwf2PH1APyUR4wrvv6lJIhA12bkQoNVV5KFwAPtWubw/edit?tab=t.0#heading=h.yhx5pggnwgne
   *
   * @param {string} url - The full URL to register
   * @returns {string} The short token for the URL (e.g. "GITHUB_COM_1")
   */
  convertUrlToToken(url) {
    return this.urlTokenizer.encodeToken(url);
  }

  /**
   * @param {string} chunk - Raw text chunk from the model stream.
   * @param {ChatMessage} currentMessage - Message receiving the body / tokens.
   * @param {object} parserState - State returned by `createParserState()`, threaded across chunks.
   */
  handleChunk(chunk, currentMessage, parserState) {
    const { plainText, tokens } = consumeStreamChunk(
      chunk,
      parserState,
      this.urlTokenizer.tokenToUrl
    );

    if (plainText && currentMessage?.content) {
      currentMessage.content.body =
        (currentMessage.content.body ?? "") + plainText;
    }
    if (tokens) {
      currentMessage?.addTokens(tokens);
    }
    if (plainText || tokens) {
      this.emit("chat-conversation:message-update", currentMessage);
      lazy.ChatStore.persistStreamingMessage(this, currentMessage);
    }
  }

  /**
   * @param {AsyncIterable} stream - Model response stream from `runWithGenerator`.
   */
  async receiveResponse(stream) {
    const currentMessage = this.#getCurrentAssistantResponse();
    if (!currentMessage) {
      return {
        pendingToolCalls: [],
        fullResponseText: "",
        usage: null,
      };
    }

    if (currentMessage.content?.body) {
      currentMessage.content.body += "\n\n";
    }

    // Set the browsing history snapshot on the message before streaming so its
    // list is recognized while streaming and swapped to a grid on completion.
    if (this.#historyResultsPool.size) {
      currentMessage.historyResults = this.getHistoryResultsSnapshot();
    }

    // Snapshot the web-search citations onto the message so the reply can show
    // its source chips underneath.
    if (this.#pendingCitationUrls.size) {
      currentMessage.citations = this.getCitationsSnapshot();
    }

    let result;
    try {
      result = await super.receiveResponse(stream, currentMessage);
    } catch (e) {
      // An aborted or failed stream skips the persist below, so land what
      // streamed before it stopped rather than leaving it only in memory.
      await lazy.ChatStore.endStreamingWrites(this.id);
      throw e;
    }

    if (result.currentMessage?.content?.body) {
      // Expand URL tokens and remove any hallucinated ones.
      if (this.urlTokenizer.urlToToken.size) {
        result.currentMessage.content.body = stripUnresolvedUrlTokens(
          result.currentMessage.content.body
        );
      }

      this.emit("chat-conversation:message-update", currentMessage);
    }

    // Only resolve used memories once the entire assistant turn is complete,
    // including all tool calls
    if (!result.pendingToolCalls?.length) {
      const citedMemoryIds = currentMessage.tokens?.existing_memory ?? [];
      const promptEmbeddedMemoryIds = this.promptEmbeddedMemories.map(
        memory => memory.id
      );
      this.promptEmbeddedMemories = [];

      const memoryIds = [...citedMemoryIds, ...promptEmbeddedMemoryIds];
      const memoriesApplied = memoryIds.length
        ? await lazy.MemoriesManager.resolveUsedMemories(memoryIds)
        : [];

      if (memoriesApplied.length) {
        currentMessage.memoriesApplied = memoriesApplied;

        this.emit("chat-conversation:message-update", currentMessage);
      }
    }

    // Drop the pending chunk write rather than flushing it: the full write
    // below covers the same message, plus the token remainder the stream loop
    // appends after the last chunk.
    await lazy.ChatStore.endStreamingWrites(this.id, false);
    await lazy.ChatStore.updateConversation(this);

    // Only finalize the message when the turn is actually done. When the model
    // requested tool calls, the same assistant message keeps streaming its
    // answer after the tools run (see the loop in Chat.sys.mjs), so emitting
    // completion here would mark a still-streaming message complete mid-turn —
    // e.g. converting a streamed history list into a grid before the answer
    // finishes.
    if (!result.pendingToolCalls?.length) {
      this.emit("chat-conversation:message-complete", currentMessage);
    }

    return {
      pendingToolCalls: result.pendingToolCalls,
      fullResponseText: result.fullResponseText,
      usage: result.usage,
    };
  }

  #getCurrentAssistantResponse() {
    return this.messages
      .filter(
        message =>
          message.role === MESSAGE_ROLE.ASSISTANT &&
          message?.content?.type === "text"
      )
      .at(-1);
  }

  /**
   * Returns a filtered messages array consisting only of the messages
   * that are meant to be rendered as the chat conversation.
   *
   * @returns {Array<ChatMessage>}
   */
  renderState() {
    return this.messages.filter(message => {
      const { role, content } = message;
      if (!RESTORABLE_ROLES.includes(role)) {
        return false;
      }
      const { type, body } = content ?? {};
      if (type === "function") {
        return false;
      }
      // Keep localized messages (rendered from l10n id)
      if (type === "text" && !body && !content?.l10nId && !message.toolUIData) {
        return false;
      }
      return true;
    });
  }

  _createMessage(args) {
    return new ChatMessage({ ...args, convId: this.id });
  }

  /**
   * Add a user message to the conversation
   *
   * @todo Bug 2005424
   * Limit/filter out data uris from message data
   *
   * @param {string} contentBody - The user message content
   * @param {URL?} [pageUrl=null] - The current page url when message was submitted
   * @param {UserRoleOpts} [userOpts=new UserRoleOpts()] - User message options
   * @param {object} [userContext={}] - Contextual information for the user message, such as real time info and relevant memories
   * @returns {ChatMessage} The newly created user message
   */
  addUserMessage(
    contentBody,
    pageUrl = null,
    userOpts = new UserRoleOpts(),
    userContext = {}
  ) {
    const content = {
      type: "text",
      body: contentBody,
      userContext,
    };

    if (userOpts.contextMentions?.length) {
      content.contextMentions = userOpts.contextMentions;
    }

    if (pageUrl) {
      content.contextPageUrl = pageUrl.href;
    }

    let currentTurn = this.currentTurnIndex();
    const newTurnIndex =
      this.messages.length === 1 ? currentTurn : currentTurn + 1;

    this.#pendingCitationUrls.clear();
    this.#dismissPendingUndos();

    return this.addMessage(MESSAGE_ROLE.USER, content, newTurnIndex, {
      pageUrl,
      ...userOpts,
    });
  }

  /**
   * Finds a pending confirmation in the trailing batch of tool messages.
   *
   * @param {string} toolCallId - The tool call to look up.
   * @returns {ChatMessage|undefined} The pending confirmation, if found.
   */
  getPendingToolConfirmation(toolCallId) {
    for (let i = this.messages.length - 1; i >= 0; i--) {
      const message = this.messages[i];
      if (message.role !== MESSAGE_ROLE.TOOL) {
        break;
      }
      if (
        message.content?.tool_call_id === toolCallId &&
        message.content?.body?.pending
      ) {
        return message;
      }
    }
    return undefined;
  }

  /**
   * Resolves the pending tool-confirmation message for UI actions.
   * Called by ToolUI when the user confirms or cancels via the UI.
   *
   * @param {object} outcomeBody - The new body for the tool message.
   * @param {string} toolCallId - Only resolve when the message's tool_call_id matches.
   * @returns {boolean} True if a pending message was resolved.
   */
  resolvePendingToolConfirmation(outcomeBody, toolCallId) {
    const message = this.getPendingToolConfirmation(toolCallId);

    if (!message) {
      return false;
    }

    message.content = { ...message.content, body: outcomeBody };
    this.emit("chat-conversation:message-update", message);
    lazy.ChatStore.updateConversation(this).catch(e => {
      lazy.console.error("Failed to persist resolved tool confirmation", e);
    });
    return true;
  }

  /**
   * Mark the most recent ai-action-result toolUIData with
   * properties.undoDismissed: true. Called when a user message
   * is added, signalling the previous action is no longer available.
   *
   * At most one card is non-dismissed at any time, so walk back
   * and stop on first hit.
   *
   * Persistence: emit triggers re-render. The toolUIData mutation
   * is persisted on the next ChatStore.updateConversation call
   * which fires when the assistant turn that follows completes.
   */
  #dismissPendingUndos() {
    for (let i = this.messages.length - 1; i >= 0; i--) {
      const m = this.messages[i];
      const td = m.toolUIData;
      if (
        !td ||
        td.uiType !== "ai-action-result" ||
        td.properties?.undoDismissed
      ) {
        continue;
      }

      const confirmedData = td.properties?.confirmedData;
      if (!confirmedData?.operationIds?.length) {
        continue;
      }

      m.toolUIData = {
        ...td,
        properties: { ...td.properties, undoDismissed: true },
      };
      this.emit("chat-conversation:message-update", m);
      break;
    }
  }

  /**
   * Add an assistant message to the conversation
   *
   * @param {string} type - The assistant message type: text|function
   * @param {string} contentBody - The assistant message content
   * @param {AssistantRoleOpts} [assistantOpts=new AssistantRoleOpts()] - ChatMessage options specific to assistant messages
   * @returns {ChatMessage} The newly created assistant message
   */
  addAssistantMessage(
    type,
    contentBody,
    assistantOpts = new AssistantRoleOpts()
  ) {
    if (assistantOpts.modelId == null) {
      assistantOpts.modelId = this.engine?.model ?? null;
    }
    const content = {
      type,
      body: contentBody,
    };

    return this.addMessage(
      MESSAGE_ROLE.ASSISTANT,
      content,
      this.currentTurnIndex(),
      assistantOpts
    );
  }

  /**
   * Add a localized assistant message that renders from a Fluent id, optionally
   * embedding a link via '<a data-l10n-name>' element in the message
   *
   * @param {string} l10nId - Fluent id for the message
   * @param {object} [l10nArgs] - Fluent variables for the message
   * @param {{ l10nName: string, href: string }} [link] - Link to fill the
   *   matching '<a data-l10n-name>' element in the message
   * @param {AssistantRoleOpts} [assistantOpts=new AssistantRoleOpts()]
   * @returns {ChatMessage} The newly created assistant message
   */
  addAssistantWithL10nMessage(
    l10nId,
    l10nArgs = null,
    link = null,
    assistantOpts = new AssistantRoleOpts()
  ) {
    if (assistantOpts.modelId == null) {
      assistantOpts.modelId = this.engine?.model ?? null;
    }
    const content = { type: "text", body: "", l10nId, l10nArgs, link };
    const message = this.addMessage(
      MESSAGE_ROLE.ASSISTANT,
      content,
      this.currentTurnIndex(),
      assistantOpts
    );

    if (message) {
      this.emit("chat-conversation:message-update", message);
      this.emit("chat-conversation:message-complete", message);
    }
    return message;
  }

  /**
   * Add a tool call message to the conversation
   *
   * @param {object} content - The tool call object to be saved as JSON
   * @param {ToolRoleOpts} [toolOpts=new ToolRoleOpts()] - Message opts for a tool role message
   * @returns {ChatMessage} The newly created tool message
   */
  addToolCallMessage(content, toolOpts = new ToolRoleOpts()) {
    if (toolOpts.modelId == null) {
      toolOpts.modelId = this.engine?.model ?? null;
    }
    const message = this.addMessage(
      MESSAGE_ROLE.TOOL,
      content,
      this.currentTurnIndex(),
      toolOpts
    );
    // Emit tool messages so the renderer can display them
    // in the action log
    if (message) {
      this.emit("chat-conversation:message-update", message);
    }
    return message;
  }

  /**
   * Finalize a tool call message added early (with a placeholder body) so the
   * action log could show a pending row while a slow tool ran: replace its
   * content in place and re-emit so the renderer refreshes the same row instead
   * of appending a duplicate. Falls back to adding a fresh message when there
   * is none to update.
   *
   * `content` replaces the existing content entirely (callers pass the complete
   * object). Like addToolCallMessage, this does NOT persist; the caller must
   * call ChatStore.updateConversation afterwards.
   *
   * @param {ChatMessage|null} message - Message returned by a prior
   *   addToolCallMessage, or null to add a fresh one.
   * @param {object} content - The finalized tool call content.
   * @returns {ChatMessage} The updated (or newly added) tool message.
   */
  updateToolCallMessage(message, content) {
    if (!message) {
      return this.addToolCallMessage(content);
    }
    message.content = content;
    this.emit("chat-conversation:message-update", message);
    return message;
  }

  /**
   * Upserts the chat system prompt at index 0, always rewriting body and
   * version so a fresh build (today's timestamp, latest RS content) wins.
   *
   * @param {object} [opts]
   * @param {string} [opts.model] - Model to assemble the prompt for; defaults
   *   to the conversation engine's model.
   */
  async loadSystemPrompt(opts = {}) {
    const { prompt: body, version } = await lazy.loadPrompt(
      MODEL_FEATURES.CHAT,
      { ...opts, model: opts.model ?? this.engine?.model }
    );

    return this.setSystemMessage({
      type: SYSTEM_PROMPT_TYPE.TEXT,
      body,
      ...(version && { version }),
    });
  }

  /**
   * Takes a new prompt and generates LLM context messages before
   * adding new user prompt to messages.
   *
   * SECURITY: each data source added here may carry private or untrusted
   * content and MUST raise the matching SecurityProperties flag before commit(),
   * and add a security test asserting the flags it sets.
   *
   * @param {string} prompt - new user prompt
   * @param {?URL} pageUrl - The URL of the page when prompt was submitted
   * @param {UserRoleOpts} [userOpts]
   * @param {boolean} [skipUserDispatch=false] - If true, do not emit the
   *   message-update event after adding the user message (used for retries
   *   to avoid duplicate user messages in the child process).
   */
  async generatePrompt(
    prompt,
    pageUrl,
    userOpts = undefined,
    skipUserDispatch = false
  ) {
    if (!this.messages.length) {
      await this.loadSystemPrompt();
    }

    // userContext starts empty so the user message can be added and dispatched
    // immediately for better perceived performance. The realTimeContext and
    // memoriesContext properties are set on it by reference below before this
    // method returns, so the full context is available to
    // getMessagesInChatCompletionsFormat() when the LLM call is made.
    let userContext = {};
    const userMessage = this.addUserMessage(
      prompt,
      pageUrl,
      userOpts,
      userContext
    );
    if (!skipUserDispatch) {
      this.emit("chat-conversation:message-update", userMessage);
    }

    await this.injectRealTimeContext(userMessage, {
      contextMentions: userOpts?.contextMentions,
    });

    if (userOpts?.memoriesEnabled) {
      try {
        await this.injectMemoriesContext(userMessage, prompt);
      } catch (memoriesContextError) {
        lazy.console.error(
          `Failed to generate memories context message: ${memoriesContextError}`
        );
      }
    }

    this.securityProperties.commit();
    return this;
  }

  /**
   * Removes the given user message and all messages after it from the
   * in-memory conversation, filtering out ephemeral system messages and
   * preserving the highest ordinal so future messages never reuse one.
   * The caller is responsible for deleting the returned messages from
   * the database and re-generating the response.
   *
   * TODO: Bug 2016249 - Rename to something like truncateFromMessage() and
   * consider moving the chatStore.deleteMessages() call into this method.
   *
   * @param {ChatMessage} message
   *
   * @returns {Array<ChatMessage>} - Array of messages removed from the conversation
   */
  async retryMessage(message) {
    if (message.role !== MESSAGE_ROLE.USER) {
      const err = new Error("Not a user message");
      err.clientReason = "retryInvalidMessage";
      throw err;
    }

    const removed = super.retryMessage(message);
    if (!removed.length) {
      const err = new Error("Unrelated message");
      err.clientReason = "retryInvalidMessage";
      throw err;
    }
    this.#pendingCitationUrls.clear();
    // splice() bypasses our setter; refresh branch-tip manually.
    this.#updateActiveBranchTipMessageId();
    return removed;
  }

  /**
   * Fetch browser/tab + mentions context and write it onto
   * `userMessage.content.userContext.realTimeContext`.
   *
   * SECURITY: current-tab info is private, so it raises setPrivateData() when
   * hasTabInfo is true. Context mentions inject only a URL and sanitized
   * label, not page content, so they raise no flag.
   *
   * @param {ChatMessage} userMessage
   * @param {object} [opts]
   * @param {ContextWebsite[]} [opts.contextMentions]
   * @param {Function} [opts.getRealTimeMapping]
   */
  async injectRealTimeContext(userMessage, opts = {}) {
    const { contextMentions, getRealTimeMapping } = opts;
    const realTimePrompt = await lazy.buildBrowserContextPrompt(
      this.engine?.model,
      {
        ...(getRealTimeMapping && { getRealTimeMapping }),
        contextMentions,
        securityProperties: this.securityProperties,
      }
    );
    if (!realTimePrompt || !userMessage?.content) {
      return;
    }
    if (realTimePrompt === this.#lastBrowserContext) {
      return;
    }
    userMessage.content.userContext ??= {};
    userMessage.content.userContext.realTimeContext = realTimePrompt;
    this.#lastBrowserContext = realTimePrompt;
  }

  /**
   * Fetch relevant memories, mutate
   * `userMessage.content.userContext.memoriesContext` in place.
   *
   * Retrieval is keyed on `prompt` alone, but the injected context also lists
   * the memories retrieved for the preceding user messages, so a semantically
   * isolated message like "what about the other one?" still has memories relevant
   * to its immediate context. The memories retrieved for this message are recorded on
   * `userMessage.content.relevantMemories` to feed the next messages' window.
   *
   * SECURITY: retrieved memories are private user data, so this raises
   * setPrivateData() whenever memories are returned.
   *
   * @todo Bug2009434 Rename type and change enum to renamed values
   * @param {ChatMessage} userMessage
   * @param {string} prompt
   * @param {Function} [constructMemories]
   * @param {number} [messageCount] - How many recent user messages, including
   *   this one, contribute their memories to the injected context
   */
  async injectMemoriesContext(
    userMessage,
    prompt,
    constructMemories = constructRelevantMemoriesContextMessage,
    messageCount = DEFAULT_RELEVANT_MEMORIES_MESSAGE_COUNT
  ) {
    const memoriesContext = await constructMemories(
      prompt,
      this.#getPreviousRelevantMemories(messageCount),
      this.engine?.model
    );
    if (memoriesContext == null) {
      return;
    }
    this.securityProperties.setPrivateData();
    if (userMessage?.content) {
      userMessage.content.userContext ??= {};
      userMessage.content.userContext.memoriesContext =
        memoriesContext.message.content;
      userMessage.content.relevantMemories = memoriesContext.relevantMemories;
    }
  }

  /**
   * Retrieve memories for the user messages before the one being sent,
   * ordered newest message first.
   *
   * @param {number} [messageCount] - Size of the window the message being sent
   * @returns {Array<{id: string, memory_summary: string}>}
   */
  #getPreviousRelevantMemories(
    messageCount = DEFAULT_RELEVANT_MEMORIES_MESSAGE_COUNT
  ) {
    const userMessages = [];

    for (
      let i = this.messages.length - 1;
      i >= 0 && userMessages.length < messageCount;
      i--
    ) {
      if (this.messages[i].role === MESSAGE_ROLE.USER) {
        userMessages.push(this.messages[i]);
      }
    }

    return userMessages
      .slice(1)
      .flatMap(message => message.content?.relevantMemories ?? []);
  }

  /**
   * Retrieves the list of visited sites during a conversation in visited order.
   * Primarily used to retrieve external URLs that the user had a conversation
   * around to display in Chat History view.
   *
   * @param {boolean} [includeInternal=false] - Whether to include internal Firefox URLs
   *
   * @returns {Array<URL>} - Ordered list of visited page URLs for this conversation
   */
  getSitesList(includeInternal = false) {
    const seen = new Set();
    const deduped = [];

    this.messages.forEach(message => {
      if (!message.pageUrl) {
        return;
      }

      if (!includeInternal && !message.pageUrl.protocol.startsWith("http")) {
        return;
      }

      if (!seen.has(message.pageUrl.href)) {
        seen.add(message.pageUrl.href);
        deduped.push(message.pageUrl);
      }
    });

    return deduped;
  }

  /**
   * Returns the most recently visited external sites during this conversation, or null
   * if no external sites have been visited.
   *
   * @returns {URL|null}
   */
  getMostRecentPageVisited() {
    const message = this.messages.findLast(m =>
      m.pageUrl?.protocol.startsWith("http")
    );

    return message?.pageUrl ?? null;
  }

  /**
   * Chat wire-format snapshot: filters out empty-body assistant messages,
   * walks message → wire shape, then injects realTime/memories `userContext`
   * as USER messages just before the last user message.
   *
   * @param {object} [opts]
   * @param {boolean} [opts.applyUrlTokens=true]
   * @returns {object[]}
   */
  getMessagesInChatCompletionsFormat({ applyUrlTokens = true } = {}) {
    const isWireFiltered = m =>
      // Empty-body assistant placeholders, added pre-stream to receive the
      // response body. Drop until the stream fills them in.
      (m.role === MESSAGE_ROLE.ASSISTANT && !m?.content?.body) ||
      // Legacy ephemeral SYSTEM-role realtime/memories messages persisted from
      // pre-refactor conversations. Post-refactor injects these via the new
      // userContext field on the user message; drop the old SYSTEM rows so the
      // LLM doesn't see them twice.
      (m.role === MESSAGE_ROLE.SYSTEM &&
        (m?.content?.type === SYSTEM_PROMPT_TYPE.REAL_TIME ||
          m?.content?.type === SYSTEM_PROMPT_TYPE.MEMORIES));

    // Base wire-format snapshot, then drop filtered messages. Mirror the same
    // filter on `this.messages` so indices stay aligned for userContext injection.
    const baseWire = super.getMessagesInChatCompletionsFormat();
    const filteredSrc = this.messages.filter(m => !isWireFiltered(m));
    const msgsForAPI = baseWire.filter(
      (_, i) => !isWireFiltered(this.messages[i])
    );

    // Resolve inline `@mention`s on USER messages so the model receives a
    // fetchable URL.
    for (const msg of msgsForAPI) {
      if (msg.role === getRoleLabel(MESSAGE_ROLE.USER).toLowerCase()) {
        msg.content = resolveMentionUrls(msg.content);
      }
    }

    // Inject contextual user-context messages just before the last user
    // message, as USER-role messages.
    const lastUserMsgIdx = filteredSrc.findLastIndex(
      msg => msg.role == MESSAGE_ROLE.USER
    );
    if (lastUserMsgIdx > -1) {
      const userContext = filteredSrc[lastUserMsgIdx].content.userContext;
      if (userContext) {
        const contextMsgs = Object.values(userContext).map(contextMsg => ({
          role: getRoleLabel(MESSAGE_ROLE.USER).toLowerCase(),
          content: contextMsg,
        }));
        msgsForAPI.splice(lastUserMsgIdx, 0, ...contextMsgs);
      }
    }

    if (applyUrlTokens) {
      replaceUrlsWithTokens(this, msgsForAPI);
    }

    return msgsForAPI;
  }

  #updateActiveBranchTipMessageId() {
    this.activeBranchTipMessageId = this.messages
      .filter(m => m.isActiveBranch)
      .sort((a, b) => b.ordinal - a.ordinal)
      .shift()?.id;
  }

  set messages(value) {
    // super to avoid recursion into our own setter; the base setter writes #messages.
    super.messages = value;
    this.#updateActiveBranchTipMessageId();
  }

  get messages() {
    return super.messages;
  }

  get messageCount() {
    return this.messages.filter(m => CHAT_ROLES.includes(m.role)).length;
  }

  get tokenToUrl() {
    return this.urlTokenizer.tokenToUrl;
  }

  get urlToToken() {
    return this.urlTokenizer.urlToToken;
  }

  /**
   * Returns the contextMentions count from the most recent user message in
   * the conversation, or 0 if none.
   *
   * @returns {number}
   */
  getLatestUserMentionCount() {
    const lastUserMsg = this.messages.findLast(
      m => m?.role === MESSAGE_ROLE.USER
    );
    return (
      lastUserMsg?.content?.contextMentions?.filter(
        m => !lazy.isTabGroupMember(m)
      ).length ?? 0
    );
  }

  /**
   * Efficiently add an iterable of URLs to the seen urls.
   *
   * @param {Iterable<string>} urls
   */
  addSeenUrls(urls) {
    super.addSeenUrls(urls);
    this.emit("chat-conversation:seen-urls-updated", this.seenUrls);
  }

  /**
   * Clears the tool UI data for a message
   *
   * @param {ChatMessage} message - The message to clear tool UI data from
   * @private
   */
  #clearToolUI(message) {
    message.toolUIData = null;
    this.emit("chat-conversation:message-update", message);
  }

  /**
   * Updates the tool UI data for a message with a new UI state
   *
   * @param {ChatMessage} message - The message to update
   * @param {object} data - The update data containing properties or updateData
   * @param {string|null} nextUI - The next UI state to transition to, or null to clear
   */
  async updateToolUI(message, data, nextUI) {
    // If nextUI is null, clear the toolUIData and return early
    if (nextUI === null) {
      this.#clearToolUI(message);
      return;
    }

    message.toolUIData = {
      ...message.toolUIData,
      uiType: nextUI,
      properties: {
        ...message.toolUIData.properties,
        ...data?.properties,
      },
    };

    // Add specific data based on the UI type
    if (nextUI === "ai-action-result") {
      message.toolUIData.properties.confirmedData = data.updateData;
    }

    // Emit event to trigger re-render
    this.emit("chat-conversation:message-update", message);
  }

  /**
   * Adds UI tool data to the current assistant message.
   * Handles both initial addition and progressive updates.
   *
   * @param {string} toolCallId - The ID of the tool call
   * @param {object} uiData - The UI data to attach to the message
   * @param {object} [options]
   * @param {boolean} [options.emitComplete=true] - Whether to mark the message complete
   * @returns {object} Result object with success status and message
   */
  addUIToolToCurrentMessage(toolCallId, uiData, { emitComplete = true } = {}) {
    const enrichedUIData = { ...uiData };

    // Get the last assistant text message to attach UI to
    let currentMessage = this.messages
      .filter(
        m => m.role === MESSAGE_ROLE.ASSISTANT && m.content?.type === "text"
      )
      .at(-1);

    if (!currentMessage) {
      // Create a synthetic text message if the model only returned a tool call
      // This ensures the UI has something to attach to
      currentMessage = this.addAssistantMessage("text", "");

      if (!currentMessage) {
        return {
          success: false,
          message: "Failed to create assistant message for UI attachment",
          dataAdded: null,
        };
      }
    }

    // For certain UI types, add the original user prompt
    if (lazy.CONFIRMATION_UI_TYPES.includes(uiData.uiType)) {
      const originalUserPrompt = lazy.ToolUI.findOriginalUserPrompt(
        this.messages,
        currentMessage
      );

      if (originalUserPrompt) {
        enrichedUIData.properties = {
          ...enrichedUIData.properties,
          originalUserPrompt,
        };
      }
    }

    // Check if this is an update to existing toolUIData
    const isUpdate =
      currentMessage.toolUIData &&
      currentMessage.toolUIData.toolCallId === toolCallId;

    if (isUpdate) {
      // Merge the new data with existing data for progressive updates
      currentMessage.toolUIData = {
        ...currentMessage.toolUIData,
        ...enrichedUIData,
        // Deep merge properties if they exist in both
        properties: {
          ...currentMessage.toolUIData.properties,
          ...enrichedUIData.properties,
        },
        updateCount: (currentMessage.toolUIData.updateCount || 0) + 1,
        lastUpdated: new Date().toISOString(),
      };
    } else {
      // Set toolUIData as a new object (first call)
      currentMessage.toolUIData = {
        toolCallId,
        timestamp: new Date().toISOString(),
        updateCount: 0,
        ...enrichedUIData,
      };
    }

    // Emit update event so UI components react to the change
    this.emit("chat-conversation:message-update", currentMessage);

    // Re-emit complete event since the message was already marked complete before tool execution
    // This forces the UI to re-render with the new toolUIData
    if (emitComplete) {
      this.emit("chat-conversation:message-complete", currentMessage);
    }

    return {
      success: true,
      message: isUpdate
        ? "Tool UI data updated"
        : "Tool UI data added to existing assistant message",
      dataAdded: enrichedUIData,
      isUpdate,
    };
  }

  /**
   * Merge records returned by a `search_browsing_history` tool call into the
   * conversation-level history results pool, keyed by URL. Accumulates across
   * every search in the conversation. The pool is snapshotted onto an assistant
   * message when it completes in `receiveResponse()` so the message that
   * triggered the search and any later message reusing those results
   * can both render a grid.
   *
   * @param {Iterable<PooledHistoryResult>} records - Per-URL records from search_browsing_history.
   */
  addHistoryResults(records) {
    for (const record of records) {
      record.timestamp = lazy.convertTimestamp(
        record.visitDate,
        lazy.fluentStrings
      );
      this.#historyResultsPool.set(record.url, record);
    }
  }

  /**
   * A snapshot of the accumulated history results pool, as a records array.
   * Set on the active assistant message in `receiveResponse` so the content
   * page can render the history grid.
   *
   * @returns {PooledHistoryResult[]}
   */
  getHistoryResultsSnapshot() {
    return [...this.#historyResultsPool.values()];
  }

  /**
   * Apply resolved page assets (thumbnail image URI and favicon availability)
   * onto the pooled history records by URL, so snapshots dispatched afterward
   * already include them.
   *
   * @param {Array<{url: string, image: ?string, requestedThumbnail?: boolean, hasFavicon: boolean}>} assets
   */
  applyHistoryAssets(assets) {
    for (const { url, image, requestedThumbnail, hasFavicon } of assets) {
      const record = this.#historyResultsPool.get(url);
      if (record) {
        // A citation-only request has no thumbnail
        if (requestedThumbnail !== false) {
          record.image = image;
        }
        record.hasFavicon = hasFavicon;
      }
      const citation = this.#citationsPool.get(url);
      if (citation) {
        citation.hasFavicon = hasFavicon;
      }
    }
  }

  /**
   * Rehydrate the history results pool from each message's persisted snapshot so
   * follow-ups that reference prior URLs still render a grid after reload. Safe
   * to call after messages are attached post-construction (e.g. DB load).
   */
  rehydrateHistoryResultsPool() {
    for (const message of this.messages) {
      for (const record of message.historyResults) {
        this.#historyResultsPool.set(record.url, record);
      }
    }
  }

  /**
   * Merge web-search citation records into the conversation-level citations.
   *
   * @param {Iterable<{url: string, title?: string}>} records
   */
  addCitations(records) {
    for (const record of records) {
      // Keep any favicon availability already resolved for this URL.
      const existing = this.#citationsPool.get(record.url);
      this.#citationsPool.set(record.url, { ...existing, ...record });
      this.#pendingCitationUrls.add(record.url);
    }
  }

  /**
   * A snapshot of the current turn’s citations.
   *
   * @returns {Citation[]}
   */
  getCitationsSnapshot() {
    return [...this.#pendingCitationUrls]
      .map(url => this.#citationsPool.get(url))
      .filter(Boolean);
  }

  /**
   * Rehydrate the citations pool from the message snapshots.
   */
  rehydrateCitationsPool() {
    for (const message of this.messages) {
      for (const record of message.citations) {
        this.#citationsPool.set(record.url, record);
      }
    }
  }
}
