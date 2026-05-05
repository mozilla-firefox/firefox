/*
 This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

import {
  MODEL_FEATURES,
  renderPrompt,
} from "moz-src:///browser/components/aiwindow/models/Utils.sys.mjs";

import {
  constructRelevantMemoriesContextMessage,
  constructRealTimeInfoInjectionMessage,
  sanitizeUntrustedContent,
  stripUnresolvedUrlTokens,
} from "moz-src:///browser/components/aiwindow/models/ChatUtils.sys.mjs";

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
import {
  consumeStreamChunk,
  createParserState,
  flushTokenRemainder,
} from "chrome://browser/content/aiwindow/modules/TokenStreamParser.mjs";
import { SecurityProperties } from "moz-src:///browser/components/aiwindow/models/SecurityProperties.sys.mjs";

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  ChatStore:
    "moz-src:///browser/components/aiwindow/ui/modules/ChatStore.sys.mjs",
  MemoriesManager:
    "moz-src:///browser/components/aiwindow/models/memories/MemoriesManager.sys.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "console", function () {
  return console.createInstance({
    prefix: "ChatConversation",
  });
});

const CHAT_ROLES = [MESSAGE_ROLE.USER, MESSAGE_ROLE.ASSISTANT];
const TABLES_PREF = "browser.smartwindow.allowTables";

/**
 * A conversation containing messages.
 */
export class ChatConversation extends EventEmitter {
  id;
  title;
  description;
  pageUrl;
  pageMeta;
  createdDate;
  updatedDate;
  status;
  securityProperties;
  /** @type {ChatMessage[]} */
  #messages;
  #minNextOrdinal = 0;
  activeBranchTipMessageId;

  /**
   * A mapping of a URL to its unique URL token. URL tokens are used as shortened
   * versions of URLs to help the model deal with very long URLs. Very long URLs are
   * problematic since they are hard for a model to repeat back without making mistakes
   * or hallucinating details about the URL. There is also additional cost for every
   * additional token in the context. Long URLs can also contain prompt injections since
   * they can be of an arbitrary size. URL Tokens help solve all of these issues.
   *
   * URL tokens are only generated while a message is "in flight" to and from the language
   * model. When tool calls are handled, messages rendered, and messages stored they are
   * all done with the URL tokens expanded into full URLs.
   *
   * There are no guarantees that a URL in this list isn't just hallucinated by the model.
   * Any URL the language model invents can be present in this list. The only guarantee
   * is that a token maps to some kind of arbitrary URL.
   *
   * Example mapping:
   * https://github.com/mozilla/ -> GITHUB_COM_MOZILLA_1
   *
   * @type {Map<string, string>}
   */
  urlToToken = new Map();

  /**
   * The reverse mapping for a token back to its original URL.
   *
   * e.g. GITHUB_COM_MOZILLA_1 -> https://github.com/mozilla/
   *
   * @type {Map<string, string>}
   */
  tokenToUrl = new Map();

  /**
   * A mapping of the base URL token to how many counts there are for it. It's
   * used to generate the final number on URL tokens.
   *
   * e.g.
   *
   * https://github.com/mozilla/                  -> GITHUB_COM_MOZILLA_1
   * https://github.com/mozilla#not-part-of-token -> GITHUB_COM_MOZILLA_2
   *
   * @type {Map<string, number>}
   */
  #baseTokenCounts = new Map();

  /**
   * Language models can generate arbitrary URLs. If a conversation has been exposed
   * to untrusted content (such as from summarizing a webpage) then it can be prompt
   * injected to display arbitrary URLs. Language models can also invent plausible URLs
   * for a conversation that do not exist.
   *
   * To mitigate these issues we collect all URLs that have been seen in a conversation
   * so that we can decide how to show them to users in a safe way. If a URL has not
   * been seen before, then it's untrusted in different circumstances.
   *
   * Initialized from the constructor params (restored from DB) or as an empty Set.
   *
   * @type {Set<string>}
   */
  seenUrls;

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
      memoriesToggled = null,
    } = params;

    super();

    this.id = id;
    this.title = title;
    this.description = description;
    this.pageUrl = pageUrl;
    this.pageMeta = pageMeta;
    this.createdDate = createdDate;
    this.updatedDate = updatedDate;
    this.#messages = messages;
    this.seenUrls = seenUrls ? new Set(seenUrls) : new Set();
    this.memoriesToggled = memoriesToggled;
    this._telemetryUniformSample = false;
    this._checkedTelemetryTriggers = new Set();

    // NOTE: Destructuring params.status causes a linter error
    this.status = params.status || CONVERSATION_STATUS.ACTIVE;
    if (params.securityProperties instanceof SecurityProperties) {
      this.securityProperties = params.securityProperties;
    } else if (params.securityProperties != null) {
      this.securityProperties = SecurityProperties.fromJSON(
        params.securityProperties
      );
    } else {
      this.securityProperties = new SecurityProperties();
    }
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
    const seenToken = this.urlToToken.get(url);
    if (seenToken) {
      return seenToken;
    }

    let baseToken = "";

    // Attempt to convert the URL into a base token.
    const parsedUrl = URL.parse(url);
    if (parsedUrl) {
      if (parsedUrl.protocol !== "http:" && parsedUrl.protocol !== "https:") {
        // Go ahead and handle URL tokens for more complicated URLs that
        // aren't probably supported in the chat interface, but would be useful
        // to disambiguate from the HTTP(s) varieties.
        baseToken +=
          // e.g. "ftp:" -> "FTP"
          parsedUrl.protocol.toUpperCase().replace(":", "");
      }

      // Convert the hostname into a token.
      const hostToken = parsedUrl.hostname
        .replace(/^www\./, "")
        .toUpperCase()
        .replace(/[.\-]/g, "_")
        .substring(0, 100);

      if (hostToken) {
        baseToken = baseToken ? `${baseToken}_${hostToken}` : hostToken;
      }

      // Add on the parts of the URL to the token.
      for (let part of parsedUrl.pathname.split("/")) {
        if (!part) {
          continue;
        }
        const partToken = part.toUpperCase().replace(/[^A-Z0-9]/g, "_");

        const nextToken = `${baseToken}_${partToken}`;
        if (nextToken.length > 100) {
          break;
        }
        baseToken = nextToken;
      }
    } else {
      baseToken = "INVALID_URL";
    }

    let count = this.#baseTokenCounts.get(baseToken) ?? 0;
    count += 1;
    this.#baseTokenCounts.set(baseToken, count);

    const tokenFinal = `${baseToken}_${count}`;

    this.urlToToken.set(url, tokenFinal);
    this.tokenToUrl.set(tokenFinal, url);

    return tokenFinal;
  }

  /**
   * @param {any} chunk
   * @param {any} currentMessage
   * @param {{
   *     inToken: boolean,
   *     tokenBuffer: string,
   *     tokenCandidate: boolean,
   *     pendingOpen: boolean
   * }} parserState
   */
  handleChunk(chunk, currentMessage, parserState) {
    let update = false;

    const { plainText, tokens } = consumeStreamChunk(
      chunk,
      parserState,
      this.tokenToUrl
    );

    if (plainText) {
      currentMessage.content.body += plainText;
      update = true;
    }

    if (tokens) {
      currentMessage.addTokens(tokens);
      update = true;
    }

    if (update) {
      this.emit("chat-conversation:message-update", currentMessage);

      lazy.ChatStore.updateConversation(this);
    }
  }

  async receiveResponse(stream) {
    const parserState = createParserState();
    const currentMessage = this.#getCurrentAssistantResponse();

    if (currentMessage?.content?.body) {
      currentMessage.content.body += "\n\n";
    }

    let pendingToolCalls = null;
    let fullResponseText = "";
    let usage = null;

    for await (const chunk of stream) {
      usage = chunk?.usage;
      if (chunk.text) {
        fullResponseText += chunk.text;
        this.handleChunk(chunk.text, currentMessage, parserState);
      }

      if (chunk?.toolCalls?.length) {
        pendingToolCalls = chunk.toolCalls;
      }
    }

    const remainder = flushTokenRemainder(parserState);
    if (remainder) {
      currentMessage.content.body += remainder;
      this.emit("chat-conversation:message-update", currentMessage);
    }

    if (currentMessage._pendingMemoryIds?.length) {
      currentMessage.memoriesApplied =
        await lazy.MemoriesManager.getMemoriesByID(
          new Set(currentMessage._pendingMemoryIds)
        );

      delete currentMessage._pendingMemoryIds;

      this.emit("chat-conversation:message-update", currentMessage);
    }

    // Post-process the response text by expanding the URL tokens and removing
    // any hallucinated URL tokens.
    if (this.urlToToken.size && currentMessage?.content?.body) {
      let body = stripUnresolvedUrlTokens(currentMessage.content.body);
      if (body !== currentMessage.content.body) {
        currentMessage.content.body = body;
        this.emit("chat-conversation:message-update", currentMessage);
      }
    }

    await lazy.ChatStore.updateConversation(this);
    this.emit("chat-conversation:message-complete", currentMessage);

    return { pendingToolCalls, fullResponseText, usage };
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
    return this.#messages.filter(message => {
      const { role, content } = message;
      if (!CHAT_ROLES.includes(role)) {
        return false;
      }
      if (role !== MESSAGE_ROLE.ASSISTANT) {
        return true;
      }
      const { type, body } = content ?? {};
      if (type === "function") {
        return false;
      }
      if (type === "text" && !body) {
        return false;
      }
      return true;
    });
  }

  /**
   * Returns the current turn index for the conversation
   *
   * @returns {number}
   */
  currentTurnIndex() {
    return this.#messages.reduce((turnIndex, message) => {
      return Math.max(turnIndex, message.turnIndex);
    }, 0);
  }

  /**
   * Adds a message to the conversation
   *
   * @param {ConversationRole} role - The type of conversation message
   * @param {object} content - The conversation message contents
   * @param {URL} pageUrl - The current page url when message was submitted
   * @param {number} turnIndex - The current conversation turn/cycle
   * @param {AssistantRoleOpts|ToolRoleOpts|UserRoleOpts} opts - Additional opts for the message
   */
  addMessage(role, content, pageUrl, turnIndex, opts = {}) {
    if (role < 0 || role > MESSAGE_ROLE.TOOL) {
      return;
    }

    if (turnIndex < 0) {
      turnIndex = 0;
    }

    let parentMessageId = null;
    if (this?.messages?.length) {
      const lastMessageIndex = this.messages.length - 1;
      parentMessageId = this.messages[lastMessageIndex].id;
    }

    const convId = this.id;
    const currentMessages = this?.messages || [];
    const maxOrdinal = Math.max(
      this.#minNextOrdinal,
      ...currentMessages.map(m => m.ordinal ?? 0)
    );
    const ordinal = maxOrdinal + 1;

    const messageData = {
      parentMessageId,
      content,
      ordinal,
      pageUrl,
      turnIndex,
      role,
      convId,
      ...opts,
    };

    const newMessage = new ChatMessage(messageData);

    this.messages.push(newMessage);
  }

  /**
   * Gets any URL mentioned in the conversation. These URLs have heightened security
   * permissions as they have been explicitly added to the conversation by the user.
   *
   * @returns {Set<string>}
   */
  getAllMentionURLs() {
    /** @type {Set<string>} */
    const mentionUrls = new Set();
    for (const message of this.#messages) {
      const { contextMentions } = message.content;
      if (contextMentions) {
        for (const { url } of contextMentions) {
          mentionUrls.add(url);
        }
      }
    }
    return mentionUrls;
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
      this.#messages.length === 1 ? currentTurn : currentTurn + 1;

    this.addMessage(
      MESSAGE_ROLE.USER,
      content,
      pageUrl,
      newTurnIndex,
      userOpts
    );
  }

  /**
   * Add an assistant message to the conversation
   *
   * @param {string} type - The assistant message type: text|function
   * @param {string} contentBody - The assistant message content
   * @param {AssistantRoleOpts} [assistantOpts=new AssistantRoleOpts()] - ChatMessage options specific to assistant messages
   */
  addAssistantMessage(
    type,
    contentBody,
    assistantOpts = new AssistantRoleOpts()
  ) {
    const content = {
      type,
      body: contentBody,
    };

    this.addMessage(
      MESSAGE_ROLE.ASSISTANT,
      content,
      null,
      this.currentTurnIndex(),
      assistantOpts
    );
  }

  /**
   * Add a tool call message to the conversation
   *
   * @param {object} content - The tool call object to be saved as JSON
   * @param {ToolRoleOpts} [toolOpts=new ToolRoleOpts()] - Message opts for a tool role message
   */
  addToolCallMessage(content, toolOpts = new ToolRoleOpts()) {
    this.addMessage(
      MESSAGE_ROLE.TOOL,
      content,
      null,
      this.currentTurnIndex(),
      toolOpts
    );
  }

  /**
   * Add a system message to the conversation
   *
   * @param {string} type - The assistant message type: text|injected_memories|injected_real_time_info
   * @param {string} contentBody - The system message object to be saved as JSON
   */
  addSystemMessage(type, contentBody) {
    const content = { type, body: contentBody };

    this.addMessage(
      MESSAGE_ROLE.SYSTEM,
      content,
      null,
      this.currentTurnIndex()
    );
  }

  /**
   * Takes a new prompt and generates LLM context messages before
   * adding new user prompt to messages.
   *
   * @param {string} prompt - new user prompt
   * @param {?URL} pageUrl - The URL of the page when prompt was submitted
   * @param {openAIEngine} engineInstance
   * @param {UserRoleOpts} [userOpts]
   * @param {boolean} [skipUserDispatch=false] - If true, do not emit the
   *   message-update event after adding the user message (used for retries
   *   to avoid duplicate user messages in the child process).
   */
  async generatePrompt(
    prompt,
    pageUrl,
    engineInstance,
    userOpts = undefined,
    skipUserDispatch = false
  ) {
    // Remove stale ephemeral messages before adding new user message
    this.removeSystemTimeMemoriesMessages();

    if (!this.messages.length) {
      const _systemPrompt = await engineInstance.loadPrompt(
        MODEL_FEATURES.CHAT
      );
      let tableInstructions;
      if (Services.prefs.getBoolPref(TABLES_PREF, false)) {
        tableInstructions = await engineInstance.loadPrompt(
          MODEL_FEATURES.ENABLE_TABLE_INSTRUCTIONS
        );
      } else {
        tableInstructions = await engineInstance.loadPrompt(
          MODEL_FEATURES.DISABLE_TABLE_INSTRUCTIONS
        );
      }
      const systemPrompt = renderPrompt(_systemPrompt, { tableInstructions });
      this.addSystemMessage(SYSTEM_PROMPT_TYPE.TEXT, systemPrompt);
    }

    // userContext starts empty so the user message can be added and dispatched
    // immediately for better perceived performance. The realTimeContext and
    // memoriesContext properties are set on it by reference below before this
    // method returns, so the full context is available to getMessagesInOpenAiFormat()
    // when the LLM call is made.
    let userContext = {};
    this.addUserMessage(prompt, pageUrl, userOpts, userContext);
    if (!skipUserDispatch) {
      this.emit("chat-conversation:message-update", this.messages.at(-1));
    }

    const realTimeContext = await ChatConversation.getRealTimeInfo(
      engineInstance,
      {
        contextMentions: userOpts?.contextMentions,
        securityProperties: this.securityProperties,
      }
    );
    if (realTimeContext) {
      userContext.realTimeContext = realTimeContext;
    }

    if (userOpts?.memoriesEnabled) {
      try {
        const memoriesContext = await this.getMemoriesContext(
          prompt,
          engineInstance,
          undefined,
          this.securityProperties
        );
        if (memoriesContext) {
          userContext.memoriesContext = memoriesContext;
        }
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
      throw new Error("Not a user message");
    }

    // Capture ephemeral system messages before removal so we can return them.
    const ephemeralMessages = this.#messages.filter(
      m =>
        m.role === MESSAGE_ROLE.SYSTEM &&
        (m.content?.type === SYSTEM_PROMPT_TYPE.REAL_TIME ||
          m.content?.type === SYSTEM_PROMPT_TYPE.MEMORIES)
    );

    // Remove ephemeral system messages (they'll be re-added by generatePrompt).
    this.removeSystemTimeMemoriesMessages();

    // Preserve the highest ordinal so addMessage() never reuses one.
    this.#minNextOrdinal = Math.max(
      this.#minNextOrdinal,
      ...this.#messages.map(m => m.ordinal ?? 0)
    );

    const retryMessageIndex = this.#messages.findIndex(
      chatMessage => message.id === chatMessage.id
    );

    if (retryMessageIndex === -1) {
      throw new Error("Unrelated message");
    }

    const toDeleteMessages = this.#messages.splice(retryMessageIndex);
    return [...ephemeralMessages, ...toDeleteMessages];
  }

  /**
   * Removes context system messages (real-time context, memories) that should be
   * regenerated on each turn. These messages contain time-sensitive data that becomes
   * stale between conversation turns.
   */
  removeSystemTimeMemoriesMessages() {
    this.messages = this.messages.filter(message => {
      const isRealTimeInjection =
        message.role === MESSAGE_ROLE.SYSTEM &&
        message.content?.type === SYSTEM_PROMPT_TYPE.REAL_TIME;

      const isMemoriesInjection =
        message.role === MESSAGE_ROLE.SYSTEM &&
        message.content?.type === SYSTEM_PROMPT_TYPE.MEMORIES;

      return !isRealTimeInjection && !isMemoriesInjection;
    });
  }

  /**
   * Gets the real time brower tab data for a new chat message and
   * adds a system message if the real time data API function
   * returns content.
   *
   * @typedef {
   *   (contextMentions: Array<ContextWebsite>) => Promise<{url, title, description, locale, timezone, isoTimestamp, todayDate, hasTabInfo}>
   * } RealTimeApiFunction
   *
   * @param {openAIEngine} engineInstance - The initialized engine instance
   * @param {object} [options]
   * @param {RealTimeApiFunction} [options.getRealTimeMapping=constructRealTimeInfoInjectionMessage]
   * @param {ContextWebsite[]} [options.contextMentions]
   *   URLs provided by the user as additional context
   * @param {SecurityProperties} [options.securityProperties]
   *
   * @returns {Promise<string|null>} - Promise that resolves with real time info or null
   */
  static async getRealTimeInfo(
    engineInstance,
    {
      getRealTimeMapping = constructRealTimeInfoInjectionMessage,
      contextMentions,
      securityProperties,
    } = {}
  ) {
    const realTimeInfoMapping = await getRealTimeMapping(contextMentions);
    if (realTimeInfoMapping) {
      let realTimePromptRaw = await engineInstance.loadPrompt(
        MODEL_FEATURES.REAL_TIME_CONTEXT_DATE
      );
      if (realTimeInfoMapping.hasTabInfo) {
        securityProperties.setPrivateData();
        const realTimeTabPromptRaw = await engineInstance.loadPrompt(
          MODEL_FEATURES.REAL_TIME_CONTEXT_TAB
        );
        realTimePromptRaw += realTimeTabPromptRaw;
      } else {
        delete realTimeInfoMapping.url;
        delete realTimeInfoMapping.title;
        delete realTimeInfoMapping.description;
      }
      delete realTimeInfoMapping.hasTabInfo;

      if (contextMentions?.length) {
        const contextUrls = contextMentions
          .map(
            mention =>
              `- URL: ${mention.url}\n  Title: ${sanitizeUntrustedContent(mention.label)}`
          )
          .join("\n");
        realTimeInfoMapping.contextUrls = contextUrls;
        const contextMentionsPrompt = await engineInstance.loadPrompt(
          MODEL_FEATURES.REAL_TIME_CONTEXT_MENTIONS
        );
        realTimePromptRaw += contextMentionsPrompt;
      }

      const realTimePrompt = renderPrompt(
        realTimePromptRaw,
        realTimeInfoMapping
      );
      return realTimePrompt ?? null;
    }
    return null;
  }

  /**
   * Gets the memories for a new chat message and adds
   * a system message if the memories API function returns
   * content.
   *
   * @todo Bug2009434
   * Rename type and change enum to renamed values
   *
   * @typedef {{
   *    role: string;
   *    tool_call_id: string;
   *    content: string;
   *  }} MemoryApiFunctionReturn
   *
   *  @typedef {
   *    (message: string) => Promise<null | MemoryApiFunctionReturn>
   *  } MemoriesApiFunction
   *
   * @param {message} message
   * @param {openAIEngine} engineInstance
   * @param {MemoriesApiFunction} [constructMemories=constructRelevantMemoriesContextMessage]
   * @param {SecurityProperties} [securityProperties]
   *
   * @returns {Promise<string|null>} - Promise that resolves with relevant memories or null
   */
  async getMemoriesContext(
    message,
    engineInstance,
    constructMemories = constructRelevantMemoriesContextMessage,
    securityProperties
  ) {
    const memoriesContext = await constructMemories(message, engineInstance);
    if (memoriesContext != null) {
      securityProperties.setPrivateData();
      return memoriesContext.content;
    }
    return null;
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
    const sites = this.getSitesList();

    return sites.length ? sites.pop() : null;
  }

  /**
   * Converts the persisted message data to OpenAI API format
   *
   * @returns {Array<{ role: string, content: string }>}
   */
  getMessagesInOpenAiFormat() {
    const filteredMsgs = this.#messages.filter(message => {
      return !(
        message.role === MESSAGE_ROLE.ASSISTANT && !message?.content?.body
      );
    });
    const msgsForAPI = filteredMsgs.map(message => {
      const msg = {
        role: getRoleLabel(message.role).toLowerCase(),
        content: message.content?.body ?? message.content,
      };

      if (msg.content.tool_calls) {
        msg.tool_calls = msg.content.tool_calls;
        msg.content = "";
      }

      if (msg.role === "tool") {
        msg.tool_call_id = message.content.tool_call_id;
        msg.name = message.content.name;
        msg.content = JSON.stringify(message.content.body);
      }

      return msg;
    });

    // Inject contextual messages immediately before the last user message, like real time info and relevant memories as USER role messages
    const lastUserMsgIdx = filteredMsgs.findLastIndex(
      msg => msg.role == MESSAGE_ROLE.USER
    );

    if (lastUserMsgIdx > -1) {
      const contextMsgs = Object.values(
        filteredMsgs[lastUserMsgIdx].content.userContext
      ).map(contextMsg => ({
        role: getRoleLabel(MESSAGE_ROLE.USER).toLowerCase(),
        content: contextMsg,
      }));
      msgsForAPI.splice(lastUserMsgIdx, 0, ...contextMsgs);
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
    this.#messages = value;
    this.#updateActiveBranchTipMessageId();
  }

  get messages() {
    return this.#messages;
  }

  get messageCount() {
    return this.#messages.filter(m => CHAT_ROLES.includes(m.role)).length;
  }

  /**
   * Efficiently add an iterable of URLs to the seen urls.
   *
   * @param {Iterable<string>} urls
   */
  addSeenUrls(urls) {
    for (const url of urls) {
      this.seenUrls.add(url);
    }
    this.emit("chat-conversation:seen-urls-updated", this.seenUrls);
  }
}
