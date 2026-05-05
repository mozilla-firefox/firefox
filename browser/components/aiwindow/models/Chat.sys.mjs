/**
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

import { ToolRoleOpts } from "moz-src:///browser/components/aiwindow/ui/modules/ChatMessage.sys.mjs";
import {
  openAIEngine,
  DEFAULT_MODEL,
  MODEL_FEATURES,
} from "moz-src:///browser/components/aiwindow/models/Utils.sys.mjs";
import {
  toolsConfig,
  toolFns,
  GetPageContent,
  RunSearch,
  GET_OPEN_TABS,
  SEARCH_BROWSING_HISTORY,
  GET_PAGE_CONTENT,
  RUN_SEARCH,
  GET_USER_MEMORIES,
  GET_NAVIGATION_INFO,
} from "moz-src:///browser/components/aiwindow/models/Tools.sys.mjs";
import {
  expandUrlTokensInToolParams,
  replaceUrlsWithTokens,
} from "moz-src:///browser/components/aiwindow/models/ChatUtils.sys.mjs";
import { compactMessages } from "moz-src:///browser/components/aiwindow/models/PromptOptimizer.sys.mjs";
import {
  TelemetryEngine,
  submitTelemetryResult,
} from "moz-src:///browser/components/aiwindow/models/TelemetryUtils.sys.mjs";

// Hard limit on how many times run_search can execute per conversation turn.
// Prevents infinite tool-call loops when the model repeatedly requests search.
// Bug 2024006.
const MAX_RUN_SEARCH_PER_TURN = 3;

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  AIWindow:
    "moz-src:///browser/components/aiwindow/ui/modules/AIWindow.sys.mjs",
  SearchService: "moz-src:///toolkit/components/search/SearchService.sys.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "console", () =>
  console.createInstance({
    prefix: "Conversation",
    maxLogLevelPref: "browser.smartwindow.conversation.logLevel",
  })
);

/**
 * @import { ChatConversation } from "moz-src:///browser/components/aiwindow/ui/modules/ChatConversation.sys.mjs"
 */

/**
 * Represents a tool call request from the language model.
 *
 * @typedef {object} ToolCall
 * @property {string} id - e.g. "call_91e28da3a0f4469586aaa01c"
 * @property {"function"} type - Here just "function"
 * @property {{name: string, arguments: unknown }} function - The name and stringified
 *   arguments for the function, e.g. { name: "get_user_memories", arguments: "{}" }
 */

/**
 * Chat
 */
export const Chat = {};

XPCOMUtils.defineLazyPreferenceGetter(
  Chat,
  "modelId",
  "browser.smartwindow.model",
  DEFAULT_MODEL[MODEL_FEATURES.CHAT]
);

/**
 * Log chat stream traffic.
 * Automatically formats the output and is controlled by the logLevel pref.
 * Data is wrapped in an array to keep the console output flat and clickable.
 *
 * @param {number} turn
 * @param {string} action
 * @param {object | Array} [data]
 * @param {string} [extraText]
 */
function logConversationStream(turn, action, data = null, extraText = "") {
  try {
    let prefix = `[Chat][Turn ${turn}][${action.padEnd(10)}]`;

    if (extraText) {
      prefix += ` ${extraText}`;
    }

    if (data) {
      lazy.console.debug(prefix, [data]);
    } else {
      lazy.console.debug(prefix);
    }
  } catch (err) {
    // Failsafe: If logging ever breaks, print a raw error but DO NOT crash the stream
    lazy.console.error("[Chat] Debug logger failed to format:", err, {
      turn,
      action,
    });
  }
}

Object.assign(Chat, {
  lastUsage: null,

  /**
   * Stream assistant output with tool-call support.
   * Yields assistant text chunks as they arrive. If the model issues tool calls,
   * we execute them locally, append results to the conversation, and continue
   * streaming the model's follow-up answer. Repeats until no more tool calls.
   *
   * @param {object} options
   * @param {ChatConversation} options.conversation
   * @param {openAIEngine} options.engineInstance
   * @param {BrowsingContext} options.browsingContext - Omitted for tests only.
   * @param {"fullpage" | "sidebar" | "urlbar"} options.mode - See the MODE in ai-window.mjs
   * @param {AbortSignal} [options.signal]
   */
  async fetchWithHistory({
    conversation,
    engineInstance,
    browsingContext,
    mode,
    signal,
  }) {
    if (!browsingContext && !Cu.isInAutomation) {
      throw new Error(
        "The browsingContext must exist for fetchWithHistory unless we're in automation."
      );
    }
    const fxAccountToken = await openAIEngine.getFxAccountToken();
    if (!fxAccountToken) {
      console.error("fetchWithHistory Account Token null or undefined");
      const fxaError = new Error("FxA token unavailable");
      fxaError.error = 4; // ACCOUNT_ERROR: triggers FxA sign-in prompt in the UI
      throw fxaError;
    }

    const toolRoleOpts = new ToolRoleOpts(this.modelId);
    const currentTurn = conversation.currentTurnIndex();
    const config = engineInstance.getConfig(engineInstance.feature);
    const inferenceParams = config?.parameters || {};

    /**
     * For the first turn only, we use exactly what the user typed as the `run_search` search query.
     * To make that work, we use a different tool definition for the first turn vs. all subsequent turns.
     */
    let chatToolsConfig = structuredClone(toolsConfig);
    let isVerbatimQuery = true;
    if (currentTurn > 0) {
      chatToolsConfig =
        RunSearch.setGeneratedSearchQueryDescription(chatToolsConfig);
      isVerbatimQuery = false;
    }

    let fullResponseText = "";
    const searchExecuted = conversation._searchExecutedTurn === currentTurn;
    let blockedSearchAttempts = 0;

    const streamModelResponse = () => {
      const rawMessages = conversation.getMessagesInOpenAiFormat();
      lazy.console.log(
        `Request (${conversation.securityProperties.getLogText()})`,
        rawMessages.at(-1)
      );
      const messages = compactMessages(rawMessages);

      // This is done in-place on the messages.
      replaceUrlsWithTokens(conversation, messages);

      // Debug logging: Record only the latest message being sent to the model
      logConversationStream(currentTurn, "CHAT SEND", messages.at(-1));

      return engineInstance.runWithGenerator({
        streamOptions: { enabled: true },
        fxAccountToken,
        chatId: conversation.id,
        tool_choice: "auto",
        tools: chatToolsConfig,
        args: messages,
        signal,
        ...inferenceParams,
      });
    };

    while (true) {
      /** @type {ToolCall[] | null} */
      let pendingToolCalls = null;

      try {
        this.lastUsage = null;
        const response = await conversation.receiveResponse(
          streamModelResponse()
        );
        fullResponseText = response.fullResponseText;
        pendingToolCalls = response.pendingToolCalls;
        lazy.console.log("Response", { fullResponseText, pendingToolCalls });

        // Debug logging: Record the raw text and requested tool calls from the model
        logConversationStream(currentTurn, "CHAT RECV", {
          text: fullResponseText,
          toolCalls: pendingToolCalls,
        });

        if (response.usage) {
          this.lastUsage = response.usage;
        }
      } catch (err) {
        console.error("fetchWithHistory streaming error:", err);
        throw err;
      }

      if (!pendingToolCalls || pendingToolCalls.length === 0) {
        // Debug logging: Mark the end of the streaming loop for this turn
        logConversationStream(currentTurn, "STREAM END");
        // We only run telemetry on our own endpoints
        if (!openAIEngine.hasCustomEndpoint()) {
          console.warn("TRIGGERS");
          const telemetryEngine = new TelemetryEngine();
          const triggers = await telemetryEngine.getTriggers(conversation);
          console.warn("TRIGGERS: ", triggers);
          telemetryEngine
            .runTelemetry(triggers, conversation)
            .then(results => {
              submitTelemetryResult(
                results,
                conversation,
                this.modelId,
              );
            })
            .catch(e => console.error("Telemetry run failed:", e));
        }
        return;
      }

      if (signal?.aborted) {
        logConversationStream(currentTurn, "STREAM END", null, "aborted");
        return;
      }

      // Guard: if the first pending tool call is a duplicate run_search,
      // return an error tool result so the model continues without
      // executing the search or navigating the browser.
      // Bug 2024006: after MAX_RUN_SEARCH_PER_TURN blocked attempts, remove
      // the tool entirely so the model is forced to respond with text.
      // @todo Bug 2006159 - Check all pending tool calls, not just the first
      const firstPending = pendingToolCalls[0]?.function;
      if (firstPending?.name === RUN_SEARCH && searchExecuted) {
        blockedSearchAttempts++;

        const blockedCalls = pendingToolCalls.slice(0, 1).map(tc => ({
          id: tc.id,
          type: "function",
          function: {
            name: tc.function.name,
            arguments: tc.function.arguments || "{}",
          },
        }));
        conversation.addAssistantMessage("function", {
          tool_calls: blockedCalls,
        });

        for (const tc of pendingToolCalls.slice(0, 1)) {
          const content = {
            tool_call_id: tc.id,
            body: "ERROR: run_search tool call error: You may only run one search per user message. Respond to the user with what you have already found and ask if they want you to proceed with the next search. Do not hallucinate search results.",
            name: tc.function.name,
          };
          conversation.addToolCallMessage(content, currentTurn, toolRoleOpts);
        }

        if (blockedSearchAttempts === MAX_RUN_SEARCH_PER_TURN) {
          chatToolsConfig = chatToolsConfig.filter(
            t => t.function?.name !== RUN_SEARCH
          );
        }
        continue;
      }
      // If the user disabled memories in the last message, the assistant
      // should not be able to retrieve them using the get_user_memories tool
      else if (firstPending?.name === GET_USER_MEMORIES) {
        const lastUserMessage =
          conversation.messages.findLast(m => m.role === 0) ?? null;
        if (lastUserMessage.memoriesEnabled === false) {
          for (const tc of pendingToolCalls.slice(0, 1)) {
            const content = {
              tool_call_id: tc.id,
              body: "ERROR: get_user_memories tool call error: inform the user that they have disabled memories, so they cannot be retrieved.",
              name: tc.function.name,
            };
            conversation.addToolCallMessage(content, currentTurn, toolRoleOpts);
          }
          continue;
        }
      }

      // @todo Bug 2006159 - Implement parallel tool calling

      // Take the last tool call and ensure the serialized tool calls expand any
      // URL tokens.
      const lastToolCall = structuredClone(pendingToolCalls[0]);
      if (!lastToolCall.function.arguments) {
        // Ensure that the arguments are always present.
        lastToolCall.function.arguments = "{}";
      }
      expandUrlTokensInToolParams(
        lastToolCall.function,
        conversation.tokenToUrl
      );

      conversation.addAssistantMessage("function", {
        tool_calls: [lastToolCall],
      });

      lazy.AIWindow.chatStore?.updateConversation(conversation).catch(() => {});

      for (const toolCall of pendingToolCalls) {
        const { id, function: functionSpec } = toolCall;
        const toolName = functionSpec?.name || "";
        let toolParams = {};

        try {
          toolParams = functionSpec?.arguments
            ? JSON.parse(functionSpec.arguments)
            : {};

          expandUrlTokensInToolParams(toolParams, conversation.tokenToUrl);
        } catch {
          const content = {
            tool_call_id: id,
            body: { error: "Invalid JSON arguments" },
          };
          conversation.addToolCallMessage(content, currentTurn, toolRoleOpts);
          continue;
        }

        // Make sure we aren't using a generated query when we shouldn't be
        if (
          toolName === RUN_SEARCH &&
          isVerbatimQuery &&
          toolParams.hasOwnProperty("query")
        ) {
          delete toolParams.query;
        }

        // Capture the embedder element before running tools, as navigation during
        // a tool call such as search handoff can replace the browsing context.
        const originalEmbedderElement = browsingContext?.embedderElement;

        // Dispatch the required arguments to different tool calls. Wrap this in a
        // try/catch so the conversation can be updated for failed calls.
        let result;
        try {
          switch (toolName) {
            case GET_PAGE_CONTENT: {
              const startTime = new Date();
              result = await GetPageContent.getPageContent(
                toolParams,
                conversation
              );
              Glean.smartWindow.getPageContent.record({
                location: mode,
                chat_id: conversation.id,
                message_seq: conversation.messageCount,
                length: result.reduce(
                  (acc, curr) => acc + (curr?.length || 0),
                  0
                ),
                time: new Date() - startTime,
              });
              break;
            }
            case RUN_SEARCH: {
              result = await RunSearch.runSearch(
                toolParams,
                browsingContext,
                conversation
              );
              const engine = await lazy.SearchService.getDefault();
              Glean.smartWindow.searchHandoff.record({
                location: mode,
                chat_id: conversation.id,
                message_seq: conversation.messageCount,
                provider: engine.name ?? "unknown",
                model: engineInstance?.model,
              });
              conversation._searchExecutedTurn = currentTurn;
              break;
            }
            case GET_OPEN_TABS:
              result = await toolFns.getOpenTabs(conversation);
              break;
            case SEARCH_BROWSING_HISTORY:
              result = await toolFns.searchBrowsingHistory(
                toolParams,
                conversation
              );
              break;
            case GET_USER_MEMORIES:
              result = await toolFns.getUserMemories(conversation);
              break;
            case GET_NAVIGATION_INFO:
              result = await toolFns.getNavigationInfo(toolParams);
              break;
            default:
              throw new Error(`No such tool: ${toolName}`);
          }

          // Debug logging: Record the data returned by the tool before feeding it to the model
          logConversationStream(
            currentTurn,
            "TOOL EXEC",
            { arguments: toolParams, result },
            toolName
          );

          const content = { tool_call_id: id, body: result, name: toolName };
          conversation.addToolCallMessage(content, currentTurn, toolRoleOpts);
        } catch (error) {
          console.error(error);
          result = { error: `Tool execution failed: ${String(error)}` };
          const content = { tool_call_id: id, body: result };
          conversation.addToolCallMessage(content, currentTurn, toolRoleOpts);
        }

        lazy.AIWindow.chatStore
          ?.updateConversation(conversation)
          .catch(() => {});

        // Perform the search handoff if the RUN_SEARCH tool was run.
        if (toolName === RUN_SEARCH) {
          // Commit here because we return early below and never reach the
          // post-loop commit.
          conversation.securityProperties.commit();
          lazy.console.log(
            `Security commit ${conversation.securityProperties.getLogText()}`
          );

          const win = originalEmbedderElement?.ownerGlobal;
          if (!win || win.closed) {
            console.error(
              "run_search: Associated window not available or closed, aborting search handoff"
            );
            return;
          }

          const searchHandoffTab = win.gBrowser.getTabForBrowser(
            originalEmbedderElement
          );
          if (!searchHandoffTab) {
            console.error(
              "run_search: Original tab no longer exists, aborting search handoff"
            );
            return;
          }
          if (!searchHandoffTab.selected) {
            win.gBrowser.selectedTab = searchHandoffTab;
          }

          lazy.AIWindow.openSidebarAndContinue(win, conversation);
          return;
        }

        // @todo Bug 2006159 - Implement parallel tool calling
        break;
      }

      // Commit flags once all tool calls in this batch have finished so that
      // no tool call can observe flags staged by a sibling call.
      conversation.securityProperties.commit();
      lazy.console.log(
        `Security commit ${conversation.securityProperties.getLogText()}`
      );
    }
  },
});
