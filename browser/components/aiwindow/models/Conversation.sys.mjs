/**
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

import { Message } from "moz-src:///browser/components/aiwindow/models/Message.sys.mjs";
import { compactMessages } from "moz-src:///browser/components/aiwindow/models/PromptOptimizer.sys.mjs";
import { SecurityProperties } from "moz-src:///browser/components/aiwindow/models/SecurityProperties.sys.mjs";
import {
  consumeStreamChunk,
  createParserState,
  flushTokenRemainder,
} from "moz-src:///browser/components/aiwindow/models/TokenStreamParser.sys.mjs";

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  ConversationStore:
    "moz-src:///browser/components/aiwindow/ui/modules/ConversationStore.sys.mjs",
});

/**
 * @typedef {import("moz-src:///browser/components/aiwindow/models/Utils.sys.mjs").InferenceParams} InferenceParams
 */

/**
 * @typedef {0 | 1 | 2 | 3} MessageRole
 */

/**
 * @enum {MessageRole}
 */
export const MESSAGE_ROLE = Object.freeze({
  USER: 0,
  ASSISTANT: 1,
  SYSTEM: 2,
  TOOL: 3,
});

export const ROLE_LABEL = {
  [MESSAGE_ROLE.SYSTEM]: "system",
  [MESSAGE_ROLE.USER]: "user",
  [MESSAGE_ROLE.ASSISTANT]: "assistant",
  [MESSAGE_ROLE.TOOL]: "tool",
};

/**
 * Base conversation for any LLM-driven flow. Owns a `Message` list plus the
 * engine + parameters needed to call the model, and exposes `run()` /
 * `runWithGenerator()` against its own messages.
 *
 * The system prompt's RS-record version is stored on
 * `messages[0].content.version` at the moment `setSystemMessage()` is called
 * with a `{type, body, version}` object, and exposed via `systemPromptVersion`.
 */
export class Conversation {
  id;
  createdDate;
  updatedDate;
  feature;
  engine;
  parameters;
  /** @type {SecurityProperties} */
  securityProperties;

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
   * URLs found in SERP contents from run_search that we are willing to
   * fetch via a anonymous request even when the conversation has
   * been exposed to both private and untrusted content.
   *
   * Initialized from the constructor params (restored from DB) or as an empty Set.
   *
   * @type {Set<string>}
   */
  serpUrlsForAnonymousFetch;

  /** @type {Message[]} */
  #messages = [];
  // Floor for the next assigned ordinal. Bumped on retry to ensure removed
  // ordinals are never reused. Underscore-prefixed (not `#`-private) so
  // subclasses can read it from their own overrides.
  _minNextOrdinal = 0;

  /**
   * @param {object} [params]
   * @param {string} [params.id]
   * @param {number} [params.createdDate]
   * @param {number} [params.updatedDate]
   * @param {Message[]} [params.messages]
   * @param {string[]} [params.seenUrls]
   * @param {string[]} [params.serpUrlsForAnonymousFetch]
   * @param {string} [params.feature]
   * @param {object} [params.engine]
   * @param {InferenceParams} [params.parameters]
   * @param {SecurityProperties|object|null} [params.securityProperties]
   */
  constructor({
    id = crypto.randomUUID(),
    createdDate = Date.now(),
    updatedDate = Date.now(),
    messages = [],
    seenUrls,
    serpUrlsForAnonymousFetch,
    feature = null,
    engine = null,
    parameters,
    securityProperties,
  } = {}) {
    this.id = id;
    this.createdDate = createdDate;
    this.updatedDate = updatedDate;
    this.#messages = messages;
    this.seenUrls = seenUrls ? new Set(seenUrls) : new Set();
    this.serpUrlsForAnonymousFetch = serpUrlsForAnonymousFetch
      ? new Set(serpUrlsForAnonymousFetch)
      : new Set();
    this.feature = feature;
    this.engine = engine;
    this.parameters = parameters ?? {};

    if (securityProperties instanceof SecurityProperties) {
      this.securityProperties = securityProperties;
    } else if (securityProperties != null) {
      this.securityProperties = SecurityProperties.fromJSON(securityProperties);
    } else {
      this.securityProperties = new SecurityProperties();
    }
  }

  set messages(value) {
    this.#messages = value;
  }

  get messages() {
    return this.#messages;
  }

  get messageCount() {
    return this.#messages.length;
  }

  /**
   * Persists this conversation and its messages to the ConversationStore.
   * Only the base `Conversation` persists here; subclasses with their own
   * persistence (e.g. ChatConversation, which uses ChatStore) must not use
   * this, or their subclass-specific data would be silently dropped.
   *
   * @returns {Promise<void>}
   */
  async save() {
    if (this.constructor !== Conversation) {
      throw new Error(
        `${this.constructor.name} must not call Conversation.save(): it would ` +
          `persist only the base conversation slice. Use its own store instead.`
      );
    }
    await lazy.ConversationStore.updateConversation(this);
  }

  /** True when the underlying engineInstance is initialized and ready to serve requests. */
  get isReady() {
    return ["ready", "suspended"].includes(
      this.engine?.engineInstance?.engineStatus
    );
  }

  /**
   * Highest turnIndex across messages. Subclasses use this to assign
   * monotonically increasing turnIndices to new messages.
   */
  currentTurnIndex() {
    return this.#messages.reduce(
      (turnIndex, message) => Math.max(turnIndex, message.turnIndex ?? 0),
      0
    );
  }

  /**
   * Reads the prompt version off the current system message — written there
   * by `setSystemMessage({type, body, version})`. Returns empty string if no
   * system message or no version field.
   */
  get systemPromptVersion() {
    const sysMsg = this.#messages.find(m => m.role === MESSAGE_ROLE.SYSTEM);
    return sysMsg?.content?.version ?? "";
  }

  /**
   * Appends a Message at the next ordinal.
   *
   * @param {number} role - MESSAGE_ROLE.*
   * @param {*} content
   * @param {number} turnIndex
   * @param {object} [opts]
   * @returns {Message}
   */
  addMessage(role, content, turnIndex, opts = {}) {
    let parentMessageId = null;
    if (this.#messages.length) {
      parentMessageId = this.#messages[this.#messages.length - 1].id;
    }

    const maxOrdinal = Math.max(
      this._minNextOrdinal,
      ...this.#messages.map(m => m.ordinal ?? 0)
    );
    const ordinal = maxOrdinal + 1;

    const newMessage = this._createMessage({
      role,
      content,
      ordinal,
      turnIndex,
      parentMessageId,
      ...opts,
    });
    this.#messages.push(newMessage);
    return newMessage;
  }

  _createMessage(args) {
    return new Message(args);
  }

  addUserMessage(content) {
    return this.addMessage(
      MESSAGE_ROLE.USER,
      content,
      this.currentTurnIndex() + 1
    );
  }

  addAssistantMessage(content, opts = {}) {
    let storedContent = content;
    if (opts.tool_calls) {
      storedContent =
        typeof content === "string"
          ? { body: content, tool_calls: opts.tool_calls }
          : { ...content, tool_calls: opts.tool_calls };
    }
    return this.addMessage(
      MESSAGE_ROLE.ASSISTANT,
      storedContent,
      this.currentTurnIndex(),
      opts
    );
  }

  /**
   * Idempotent upsert of the system message at index 0. Pass either a string
   * (will be wrapped as `{body: content}`) or a content object. The version is
   * captured on the message via the content object's `version` field — that's
   * what `systemPromptVersion` reads back.
   *
   * @param {string|object} content
   */
  setSystemMessage(content) {
    const wrapped = typeof content === "string" ? { body: content } : content;
    if (this.#messages[0]?.role === MESSAGE_ROLE.SYSTEM) {
      this.#messages[0].content = wrapped;
      return this.#messages[0];
    }
    const message = this._createMessage({
      role: MESSAGE_ROLE.SYSTEM,
      content: wrapped,
      ordinal: 0,
      turnIndex: 0,
      parentMessageId: null,
    });
    this.#messages.unshift(message);
    return message;
  }

  addToolMessage({ tool_call_id, content, name }) {
    return this.addMessage(
      MESSAGE_ROLE.TOOL,
      content,
      this.currentTurnIndex(),
      { toolCallId: tool_call_id, toolName: name }
    );
  }

  removeLastMessage() {
    return this.#messages.pop();
  }

  clearMessages() {
    this.#messages = [];
  }

  replaceMessages(messages) {
    this.#messages = messages;
  }

  /**
   * Generic retry: truncate from `message` to the end. Preserves the
   * next-ordinal floor so future messages never reuse one.
   *
   * @param {Message} message
   * @returns {Message[]} removed messages
   */
  retryMessage(message) {
    // Capture the current max ordinal before splicing so future addMessage
    // calls don't reuse ordinals from removed messages (ordinals must be
    // monotonically increasing across the conversation lifetime).
    this._minNextOrdinal = Math.max(
      this._minNextOrdinal,
      ...this.#messages.map(m => m.ordinal ?? 0)
    );
    const idx = this.#messages.findIndex(m => m.id === message.id);
    if (idx === -1) {
      return [];
    }
    return this.#messages.splice(idx);
  }

  /** Returns a compacted chat-completions wire-format snapshot. */
  compactChatCompletions() {
    return compactMessages(this.getMessagesInChatCompletionsFormat());
  }

  /**
   * Snapshot in the OpenAI chat-completions API shape.
   *
   * @returns {object[]}
   */
  getMessagesInChatCompletionsFormat() {
    return this.#messages.map(message => {
      const role = ROLE_LABEL[message.role] ?? message.role;
      const bodyOrContent = message.content?.body ?? message.content;
      const msg = { role, content: bodyOrContent };

      if (
        bodyOrContent &&
        typeof bodyOrContent === "object" &&
        bodyOrContent.tool_calls
      ) {
        msg.tool_calls = bodyOrContent.tool_calls;
        msg.content = "";
      }

      if (msg.role === "tool") {
        msg.tool_call_id =
          message.toolCallId ?? message.content?.tool_call_id ?? null;
        if (message.toolName ?? message.content?.name) {
          msg.name = message.toolName ?? message.content?.name;
        }
        msg.content = JSON.stringify(message.content?.body ?? message.content);
      }
      return msg;
    });
  }

  /**
   * Process one chunk of streaming output: parse, append plain text to the
   * current message body, hand off any tokens to the message's `addTokens`
   * method when present.
   *
   * @param {string} chunk - Raw text chunk from the model stream.
   * @param {Message} currentMessage - Message receiving the body / tokens.
   * @param {object} parserState - State returned by `createParserState()`, threaded across chunks.
   * @returns {boolean} True if anything was extracted (text or tokens).
   */
  handleChunk(chunk, currentMessage, parserState) {
    const { plainText, tokens } = consumeStreamChunk(chunk, parserState);

    if (plainText && currentMessage?.content) {
      currentMessage.content.body =
        (currentMessage.content.body ?? "") + plainText;
    }

    if (tokens) {
      currentMessage?.addTokens(tokens);
    }

    return Boolean(plainText) || Boolean(tokens);
  }

  /**
   * Drain the stream: loop chunks, flush remainder.
   *
   * @param {AsyncIterable} stream
   * @param {Message} currentMessage
   * @returns {Promise<{pendingToolCalls, fullResponseText, usage, currentMessage}>}
   */
  async receiveResponse(stream, currentMessage) {
    const parserState = createParserState();
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
    if (remainder && currentMessage?.content) {
      currentMessage.content.body =
        (currentMessage.content.body ?? "") + remainder;
    }

    return { pendingToolCalls, fullResponseText, usage, currentMessage };
  }

  /**
   * Execute one LLM call against this conversation's messages + parameters.
   *
   * This and `runWithGenerator()` are the only ways a turn reaches the engine,
   * so they are where staged security flags are committed. Committing here
   * rather than at each call site covers turns driven from anywhere, including
   * a tool that re-enters the chat loop, and keeps flags staged for the whole
   * of a tool-call batch, which runs no LLM call of its own.
   *
   * @param {object} opts - { token, signal?, ... }
   * @param {InferenceParams} [opts.inferenceParams]
   * @returns {Promise<object>}
   */
  async run(opts = {}) {
    this.securityProperties.commit();
    return this.engine.run({
      args: this.getMessagesInChatCompletionsFormat(),
      ...opts,
      inferenceParams: { ...this.parameters, ...opts.inferenceParams },
    });
  }

  /**
   * Streaming variant — returns an AsyncGenerator. Commits security flags for
   * the same reason `run()` does, eagerly, before the generator is returned.
   *
   * @param {object} opts - { token, signal?, chatId?, tools?, tool_choice?, streamOptions?, args? }
   * @param {InferenceParams} [opts.inferenceParams]
   * @returns {AsyncGenerator}
   */
  runWithGenerator(opts = {}) {
    this.securityProperties.commit();
    return this.engine.runWithGenerator({
      ...opts,
      // Lazy so the projection is skipped when the caller supplies its own
      // snapshot, as fetchWithHistory does with the compacted history.
      args: opts.args ?? this.getMessagesInChatCompletionsFormat(),
      inferenceParams: { ...this.parameters, ...opts.inferenceParams },
    });
  }

  toJSON() {
    return {
      id: this.id,
      createdDate: this.createdDate,
      updatedDate: this.updatedDate,
      feature: this.feature,
      messages: this.#messages,
    };
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
  }

  /**
   * Add an iterable of URLs to the serpUrlsForAnonymousFetch ledger
   *
   * @param {Iterable<string>} urls
   */
  addSerpUrlsForAnonymousFetch(urls) {
    for (const url of urls) {
      this.serpUrlsForAnonymousFetch.add(url);
    }
  }

  /**
   * Gets any URL mentioned in the conversation. These URLs have heightened security
   * permissions as they have been explicitly added to the conversation by the user.
   * Tabs mentioned as part of a tab group were not picked individually, so they
   * are left out.
   *
   * @returns {Set<string>}
   */
  getAllMentionURLs() {
    /** @type {Set<string>} */
    const mentionUrls = new Set();
    for (const message of this.messages) {
      const { contextMentions } = message.content;
      if (contextMentions) {
        for (const { url, groupId } of contextMentions) {
          if (url && !groupId) {
            mentionUrls.add(url);
          }
        }
      }
    }
    return mentionUrls;
  }
}
