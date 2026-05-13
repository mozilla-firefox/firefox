/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  ChatConversation:
    "moz-src:///browser/components/aiwindow/ui/modules/ChatConversation.sys.mjs",
  openAIEngine: "moz-src:///browser/components/aiwindow/models/Utils.sys.mjs",
  MODEL_FEATURES: "moz-src:///browser/components/aiwindow/models/Utils.sys.mjs",
  toolFns: "moz-src:///browser/components/aiwindow/models/Tools.sys.mjs",
  toolsConfig: "moz-src:///browser/components/aiwindow/models/Tools.sys.mjs",
  GetPageContent: "moz-src:///browser/components/aiwindow/models/Tools.sys.mjs",
  RunSearch: "moz-src:///browser/components/aiwindow/models/Tools.sys.mjs",
  GET_OPEN_TABS: "moz-src:///browser/components/aiwindow/models/Tools.sys.mjs",
  SEARCH_BROWSING_HISTORY:
    "moz-src:///browser/components/aiwindow/models/Tools.sys.mjs",
  GET_PAGE_CONTENT:
    "moz-src:///browser/components/aiwindow/models/Tools.sys.mjs",
  RUN_SEARCH: "moz-src:///browser/components/aiwindow/models/Tools.sys.mjs",
  GET_USER_MEMORIES:
    "moz-src:///browser/components/aiwindow/models/Tools.sys.mjs",
  PageExtractorParent: "resource://gre/actors/PageExtractorParent.sys.mjs",
});

// Per-process state. In the previous Marionette-script form, this lived on
// the chrome window as `__realTimeInfo` / `__conversationState`. In a
// module-singleton this is per-process scope — fine for ml_driver, which
// drives one window per Firefox process. See the commit message for the
// contract change.
let _realTimeInfo = null;
let _conversation = null;

const MAX_TOOL_ROUNDS = 5;

/**
 * DuckDuckGo search helper — replaces prod run_search for evals/headless
 * testing. The production RunSearch requires a browsingContext for
 * search-handoff which doesn't exist in headless mode.
 *
 * @param {string} searchQuery
 * @param {string|null} [cachedContent]
 * @returns {Promise<string>}
 */
async function runSearchDDG(searchQuery, cachedContent = null) {
  if (!searchQuery || typeof searchQuery !== "string" || !searchQuery.trim()) {
    return "Error: a non-empty search query is required.";
  }
  try {
    if (cachedContent) {
      return `Search results for "${searchQuery.trim()}":\n\n${cachedContent}`;
    }
    const searchUrl = `https://html.duckduckgo.com/html/?q=${encodeURIComponent(
      searchQuery.trim()
    )}`;
    const extraction = await lazy.PageExtractorParent.getHeadlessExtractor(
      searchUrl,
      actor => actor.getText()
    );
    let text = extraction?.text ?? "";
    if (!text) {
      return `No content could be extracted from the search results for "${searchQuery}".`;
    }
    text = text
      .replace(/\s+/g, " ")
      .replace(/\n\s*\n/g, "\n")
      .trim();
    if (text.length > 15000) {
      const truncatePoint = text.lastIndexOf(".", 15000);
      text =
        truncatePoint > 14900
          ? text.substring(0, truncatePoint + 1)
          : text.substring(0, 15000) + "...";
    }
    return `Search results for "${searchQuery.trim()}":\n\n${text}`;
  } catch (e) {
    return `Error performing search for "${searchQuery}": ${e.message || e}`;
  }
}

/**
 * Override real-time-info injected into the chat system prompt on the next
 * `submitTurn` call.
 *
 * @param {object} info - Fields consumed by ChatConversation.getRealTimeInfo:
 *   url, title, description, locale, timezone, isoTimestamp, todayDate,
 *   hasTabInfo.
 * @returns {{ ok: true }}
 */
export async function setRealTimeInfo(info) {
  _realTimeInfo = info;
  return { ok: true };
}

/**
 * Initialize a new conversation for multi-turn use. Must be called before
 * `submitTurn` when doing multi-turn dialogue; for single-turn use, skip —
 * `submitTurn` creates a fresh conversation automatically.
 *
 * @returns {Promise<{ ok: true, conversationId: string }>}
 */
export async function initConversation() {
  _conversation = new lazy.ChatConversation({});
  return { ok: true, conversationId: _conversation.id };
}

/**
 * Submit a turn to the chat.
 *
 * Single-turn: skip initConversation — a fresh conversation is created each
 * call. Multi-turn: call initConversation first — the stored conversation
 * is reused across calls.
 *
 * @param {Array<object>} query - OpenAI-format message history.
 * @param {{ maxRetries?: number, cachedSerpContent?: string|null }} [options]
 * @returns {Promise<{
 *   content: string,
 *   toolCalls: object[],
 *   sentMessages: object[],
 *   newMessages: object[],
 * }>}
 */
export async function submitTurn(query, options = {}) {
  if (!Array.isArray(query) || !query.length) {
    throw new Error("query must be a non-empty array");
  }

  const { cachedSerpContent = null } = options;
  const conversation = _conversation ?? new lazy.ChatConversation({});

  // DuckDuckGo replacement for run_search. Production RunSearch requires
  // a browsingContext for search-handoff which doesn't exist headlessly.
  const ddgRunSearch = async ({ query: searchQuery } = {}) =>
    runSearchDDG(searchQuery, cachedSerpContent);

  try {
    // Engine instances don't survive across executeAsyncScript invocations
    // — rebuild each call. Pass "ai-dev" service type for MLPA proxy auth.
    const engineInstance = await lazy.openAIEngine.build(
      lazy.MODEL_FEATURES.CHAT
    );

    // Always inject the Firefox system prompt first, matching standard
    // aiwindow behavior.
    const systemPrompt = await engineInstance.loadPrompt(
      lazy.MODEL_FEATURES.CHAT
    );
    conversation.addSystemMessage("text", systemPrompt);

    const lastMsg = query.findLast(msg => msg.role === "user");

    const realTimeContext = await lazy.ChatConversation.getRealTimeInfo(
      engineInstance,
      {
        ...(_realTimeInfo && {
          getRealTimeMapping: () => Promise.resolve(_realTimeInfo),
        }),
      }
    );
    let memoriesContext = null;
    try {
      memoriesContext = await conversation.getMemoriesContext(
        lastMsg.content,
        engineInstance,
        undefined,
        conversation.securityProperties
      );
    } catch (e) {
      console.warn(
        "[chat] Failed to get memories context, continuing without:",
        e
      );
    }

    const userContext = {
      ...(realTimeContext && { realTimeContext }),
      ...(memoriesContext && { memoriesContext }),
    };

    // Hydrate conversation from a full message history. Real-time context
    // and memories are only attached to the final user message.
    const lastUserIndex = query.findLastIndex(msg => msg.role === "user");
    let userMsgCount = 0;
    for (let i = 0; i < query.length; i++) {
      const msg = query[i];
      switch (msg.role) {
        case "system":
          conversation.addSystemMessage("text", msg.content);
          break;
        case "user":
          conversation.addUserMessage(
            msg.content,
            null,
            { memoriesEnabled: i === lastUserIndex },
            i === lastUserIndex ? userContext : {}
          );
          userMsgCount++;
          break;
        case "assistant":
          if (msg.tool_calls) {
            conversation.addAssistantMessage("function", {
              tool_calls: msg.tool_calls,
            });
          } else {
            conversation.addAssistantMessage("text", msg.content);
          }
          break;
        case "tool": {
          let toolBody;
          try {
            toolBody = JSON.parse(msg.content);
          } catch {
            toolBody = msg.content;
          }
          conversation.addToolCallMessage({
            tool_call_id: msg.tool_call_id,
            name: msg.name,
            body: toolBody,
          });
          break;
        }
      }
    }

    // Auth and inference params for direct streaming.
    const fxAccountToken = await lazy.openAIEngine.getFxAccountToken();
    const config = engineInstance.getConfig(engineInstance.feature);
    const inferenceParams = config?.parameters || {};

    let fullContent = "";
    let fullToolCalls = [];
    let searchExecuted = false;
    const sentMessages = conversation.getMessagesInOpenAiFormat();
    const msgCountBefore = sentMessages.length;

    let chatToolsConfig = lazy.toolsConfig;
    // After the first user turn, swap in the generated-search-query
    // description so the model produces a refined query.
    if (userMsgCount > 1) {
      chatToolsConfig =
        lazy.RunSearch.setGeneratedSearchQueryDescription(chatToolsConfig);
    }

    for (let round = 0; round < MAX_TOOL_ROUNDS; round++) {
      // Placeholder assistant message for receiveResponse to fill.
      conversation.addAssistantMessage("text", "");

      // Stream directly via engine + receiveResponse, bypassing
      // fetchWithHistory which does a sidebar handoff on run_search that
      // breaks headless evals.
      const stream = engineInstance.runWithGenerator({
        streamOptions: { enabled: true },
        fxAccountToken,
        chatId: conversation.id,
        tool_choice: "auto",
        tools: chatToolsConfig,
        args: conversation.getMessagesInOpenAiFormat(),
        ...inferenceParams,
      });
      const response = await conversation.receiveResponse(stream);
      const pendingToolCalls = response.pendingToolCalls;

      // Match fetchWithHistory: overwrite (not accumulate) each round.
      // Text from tool-call rounds is discarded — only the final text-only
      // round matters.
      fullContent = response.fullResponseText || "";

      if (pendingToolCalls?.length) {
        // Remove the placeholder assistant message before adding the clean
        // tool-call message — matches fetchWithHistory behavior.
        const msgs = conversation.messages;
        if (msgs.length && msgs[msgs.length - 1]?.role === "assistant") {
          msgs.pop();
        }
        // receiveResponse only collects tool calls — it does not add them
        // to the conversation. We must do it ourselves, matching
        // fetchWithHistory.
        const tool_calls = pendingToolCalls.map(tc => ({
          id: tc.id,
          type: "function",
          function: {
            name: tc.function.name,
            arguments: tc.function.arguments || "{}",
          },
        }));
        conversation.addAssistantMessage("function", { tool_calls });

        // Guard: block duplicate run_search (matches fetchWithHistory
        // Bug 2024006).
        const firstPending = pendingToolCalls[0]?.function;
        if (firstPending?.name === lazy.RUN_SEARCH && searchExecuted) {
          conversation.addToolCallMessage({
            tool_call_id: pendingToolCalls[0].id,
            body: "ERROR: run_search tool call error: You may only run one search per user message. Respond to the user with what you have already found.",
            name: lazy.RUN_SEARCH,
          });
          fullToolCalls.push(...tool_calls);
          continue;
        }

        // Dispatch tool calls — mirrors the switch in fetchWithHistory.
        // RunSearch is replaced with DDG for headless.
        for (const tc of pendingToolCalls) {
          const toolName = tc.function?.name || "";
          let toolParams = {};
          try {
            toolParams = tc.function?.arguments
              ? JSON.parse(tc.function.arguments)
              : {};
          } catch {
            /* ignore parse errors */
          }

          let result;
          try {
            switch (toolName) {
              case lazy.RUN_SEARCH:
                result = await ddgRunSearch(toolParams);
                searchExecuted = true;
                break;
              case lazy.GET_PAGE_CONTENT:
                result = await lazy.GetPageContent.getPageContent(
                  toolParams,
                  conversation
                );
                break;
              case lazy.GET_OPEN_TABS:
                result = await lazy.toolFns.getOpenTabs(conversation);
                break;
              case lazy.SEARCH_BROWSING_HISTORY:
                result = await lazy.toolFns.searchBrowsingHistory(
                  toolParams,
                  conversation
                );
                break;
              case lazy.GET_USER_MEMORIES:
                result = await lazy.toolFns.getUserMemories(conversation);
                break;
              default:
                throw new Error(`Unknown tool: ${toolName}`);
            }
          } catch (e) {
            result = { error: `Tool execution failed: ${String(e)}` };
          }
          conversation.addToolCallMessage({
            tool_call_id: tc.id,
            body: result,
            name: toolName,
          });
        }

        fullToolCalls.push(...tool_calls);
        continue;
      }

      // No tool calls — done.
      break;
    }

    fullContent = fullContent.trim();
    if (!fullContent) {
      throw new Error("Chat returned empty response");
    }
    // If the response contains raw <|channel|> tokens, MLPA failed to
    // translate the model's tool call into structured delta.tool_calls.
    // Treat this as a transient error so the framework retries.
    if (fullContent.includes("<|channel|>")) {
      throw new Error("Chat returned untranslated <|channel|> tokens");
    }

    // Ensure fullContent is in an assistant message for newMessages
    // extraction.
    const finalMsg = conversation.messages[conversation.messages.length - 1];
    if (!finalMsg?.content?.body || finalMsg.content.body.trim() === "") {
      if (finalMsg?.role === "assistant") {
        conversation.messages.pop();
      }
      conversation.addAssistantMessage("text", fullContent);
    }

    const newMessages = conversation
      .getMessagesInOpenAiFormat()
      .slice(msgCountBefore);
    return {
      content: fullContent,
      toolCalls: fullToolCalls,
      sentMessages,
      newMessages,
    };
  } catch (error) {
    console.error(`[chat] attempt to submit prompt failed:`, error);
    throw error;
  }
}
