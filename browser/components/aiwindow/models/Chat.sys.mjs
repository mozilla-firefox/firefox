/**
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

import { AuthProvider } from "moz-src:///browser/components/aiwindow/models/AuthProvider.sys.mjs";
import { MESSAGE_ROLE } from "moz-src:///browser/components/aiwindow/models/Conversation.sys.mjs";
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
  MANAGE_TABS,
  AITAB_PREF,
  AITAB_TOOLS,
  INCLUDE_CHAT_TAB_PREF,
  GENERATE_AITAB,
  ADD_MEMORY,
  SEARCH_THE_WEB,
  SEARCH_THE_WEB_PATH,
  searchTheWebToolConfig,
  selectSearchTheWebPath,
  GET_SKILL,
} from "moz-src:///browser/components/aiwindow/models/Tools.sys.mjs";
import { runSearchTheWeb } from "moz-src:///browser/components/aiwindow/models/search/SearchWorkflow.sys.mjs";

import { expandUrlTokensInToolParams } from "moz-src:///browser/components/aiwindow/models/ChatUtils.sys.mjs";
import { runLLMaJTelemetry } from "moz-src:///browser/components/aiwindow/models/TelemetryUtils.sys.mjs";
import { getSmartPageName } from "chrome://browser/content/aiwindow/modules/TrustedInternalURLs.mjs";

/**
 * Execute a specific tool and return the result
 * Exported for testing purposes
 *
 * @param {string} toolName - The name of the tool to execute
 * @param {object} toolParams - The parameters to pass to the tool
 * @param {string} toolCallId - The ID of the tool call
 * @param {ChatConversation} conversation - The conversation context
 * @param {BrowsingContext} browsingContext - The browsing context (can be null for some tools)
 * @param {string} mode - The mode of operation (e.g., "fullpage", "sidebar", "urlbar")
 * @returns {Promise<object>} The result of the tool execution, or
 *   `{ toolResult, cardShown }` for manage_tabs
 * @private
 */
export async function executeToolByName(
  toolName,
  toolParams,
  toolCallId,
  conversation,
  browsingContext,
  mode
) {
  let result;
  switch (toolName) {
    case GET_PAGE_CONTENT: {
      const startTime = new Date();
      result = await GetPageContent.getPageContentText(
        toolParams,
        conversation
      );
      Glean.smartWindow.getPageContent.record({
        location: mode,
        chat_id: conversation.id,
        message_seq: conversation.messageCount,
        length: result.reduce((acc, curr) => acc + (curr?.length || 0), 0),
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
        model: conversation.engine?.model,
      });
      break;
    }
    case GET_OPEN_TABS:
      result = await toolFns.getOpenTabs(toolParams, conversation);
      break;
    case SEARCH_BROWSING_HISTORY:
      result = await toolFns.searchBrowsingHistory(toolParams, conversation);
      break;
    case GET_USER_MEMORIES:
      result = await toolFns.getUserMemories(conversation);
      break;
    case GET_NAVIGATION_INFO:
      result = await toolFns.getNavigationInfo(toolParams);
      break;
    case GET_SKILL:
      result = await toolFns.getSkill({
        toolParams,
        model: conversation.engine?.model,
      });
      break;
    case MANAGE_TABS: {
      const { toolResult, uiData } = await toolFns.manageTabs(
        toolParams,
        conversation,
        mode,
        conversation.engine?.model,
        toolCallId
      );
      if (uiData) {
        conversation.addUIToolToCurrentMessage(toolCallId, uiData);
      }
      result = { toolResult, cardShown: !!uiData };
      break;
    }
    case ADD_MEMORY: {
      result = await toolFns.addMemory(toolParams, conversation);
      break;
    }
    default: {
      const err = new Error(`No such tool: ${toolName}`);
      err.clientReason = "unknownTool";
      throw err;
    }
  }
  return result;
}

/**
 * Handlers for tools that are feature-gated by a pref and intended to be
 * added or removed independently of the main tool dispatch. Lookups happen
 * before the main switch so each new gated tool does not grow
 * fetchWithHistory's cyclomatic complexity.
 *
 */
const FEATURE_GATED_HANDLERS = new Map([
  [SEARCH_THE_WEB, runSearchTheWeb],
  [GENERATE_AITAB, runGenerateAiTab],
]);

/**
 * @typedef {object} GenerateAiTabFailure
 * @property {false} success - If the AITab was generated successfully
 * @property {string} toolResult - A tool result message
 */

/**
 * @typedef {object} GenerateAiTabToolUI
 * @property {"aitab"} uiType - The ToolUI type
 * @property {{
 *  state: "choose",
 *  viewerURL: string,
 *  title: string
 * }} properties - The ToolUI update data for the AITab progress card
 */

/**
 * @typedef {object} GenerateAiTabSuccess
 * @property {true} success - If the AITab was generated successfully
 * @property {{message: string, page: {slug: string}}} toolResult - The
 *   model response and the stored page's slug
 * @property {GenerateAiTabToolUI} [uiData] - Data to update the ToolUI,
 *   absent for a modification
 */

/**
 * @typedef {object} GenerateAiTabResult
 * @property {boolean} success - If the AITab was generated successfully
 * @property {GenerateAiTabSuccess["toolResult"] | string} toolResult - The
 *   tool result for the model
 * @property {boolean} endsTurn - Whether the ToolUI card is the response, so
 *   the model gets no follow-up turn
 */

/**
 * Tool entrypoint for generate_aitab. Attaches the AITab options
 * after the page has been generated and saved.
 *
 * @param {object} params
 * @param {string[]} [params.url_list] - List of URLs to use to generate the AITab
 * @param {string} [params.focus] - A focus string used to generate the AITab
 * @param {ChatConversation} conversation - ChatConversation that initiated AITab
 * @param {AbortSignal} [signal] - Cancels in-flight page extractions.
 * @param {any} _mode - Operation mode, unused
 * @param {string} [toolCallId] - The tool call's ID
 * @param {BrowsingContext} [browsingContext] - The chat's browsing context,
 *   used to find the selected tab of its window
 *
 * @returns {Promise<GenerateAiTabResult>} The tool response after attaching
 *   its UI data to the conversation.
 */
async function runGenerateAiTab(
  params,
  conversation,
  signal,
  _mode,
  toolCallId,
  browsingContext
) {
  const { success, toolResult, uiData } = await toolFns.createAITab(
    params,
    conversation,
    signal
  );

  if (toolCallId && uiData) {
    conversation.addUIToolToCurrentMessage(toolCallId, uiData);
  } else if (success) {
    reloadSelectedSmartPage(browsingContext, toolResult.page.slug);
  }

  return { success, toolResult, endsTurn: success && !!uiData };
}

/**
 * Reloads the selected tab of the chat's window when it shows the page with
 * `slug`, so a modified page displays its new version.
 *
 * @param {BrowsingContext} [browsingContext]
 * @param {string} slug
 */
function reloadSelectedSmartPage(browsingContext, slug) {
  const browser = browsingContext?.topChromeWindow?.gBrowser?.selectedBrowser;
  const url = browser?.currentURI && URL.parse(browser.currentURI.spec);
  if (getSmartPageName(url) === slug) {
    browser.reload();
  }
}

/**
 * Slow tools that surface the action log's pending row before their handler
 * resolves (not the same as feature-gating). Pending rows use a placeholder
 * body, so tools with body-dependent labels must provide a pendingLabel.
 */
const TOOLS_WITH_PENDING_ACTION_LOG = new Set([SEARCH_THE_WEB, GENERATE_AITAB]);

/**
 * Splits a tool result into the reply the tool is writing for the user, if any,
 * and the body that stays in the conversation. Only the answers path of
 * search_the_web sets `directAnswerStream`; it is lifted out both because a
 * generator is not something to serialize into a tool message, and because the
 * prose it carries becomes the assistant turn rather than history the model
 * answers from.
 *
 * Requiring an async-iterable `directAnswerStream` is the whole guard: it
 * already excludes every other result shape a tool returns, including the
 * arrays and strings the rest-destructure below would otherwise mangle.
 *
 * @param {unknown} result
 * @returns {{directAnswerStream: AsyncGenerator<object>|null, toolBody: unknown}}
 */
function splitDirectAnswerStream(result) {
  if (
    typeof result?.directAnswerStream?.[Symbol.asyncIterator] !== "function"
  ) {
    return { directAnswerStream: null, toolBody: result };
  }
  const { directAnswerStream, ...toolBody } = result;
  return { directAnswerStream, toolBody };
}

/**
 * Removes any feature-gated tools whose enable pref is currently off, so the
 * model is never offered tools the build is not configured to support, and
 * swaps in pref-selected variants of a tool's config.
 *
 * Treats `tools` as read-only: it only ever drops or substitutes whole entries,
 * so every operation here must be non-mutating (`filter`/`map`/`toSorted`, never
 * `splice`/`sort`/index assignment). Callers pass the shared `toolsConfig`
 * constant, and when no gate applies it is returned by identity.
 *
 * @param {object[]} tools
 * @param {ChatConversation} conversation
 * @returns {object[]}
 */
function filterFeatureGatedTools(tools, conversation) {
  let filtered = tools;
  // The three search_the_web paths return different shapes, so the description
  // and parameters the model sees have to match the path that will run. Both
  // this and runSearchTheWeb's dispatch read the same selectSearchTheWebPath,
  // so they cannot disagree about which one that is.
  const searchTheWebConfig = searchTheWebToolConfig(conversation);
  if (searchTheWebConfig) {
    filtered = filtered.map(t =>
      t.function?.name === SEARCH_THE_WEB ? searchTheWebConfig : t
    );
  }
  if (!Services.prefs.getBoolPref(AITAB_PREF, false)) {
    filtered = filtered.filter(t => !AITAB_TOOLS.has(t.function?.name));
  }
  if (!Services.prefs.getBoolPref(INCLUDE_CHAT_TAB_PREF, false)) {
    filtered = filtered.map(t => {
      if (t.function.name !== MANAGE_TABS) {
        return t;
      }
      const properties = { ...t.function.parameters.properties };
      delete properties.include_chat_tab;
      return {
        ...t,
        function: {
          ...t.function,
          parameters: { ...t.function.parameters, properties },
        },
      };
    });
  }
  return filtered;
}

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  AIWindow:
    "moz-src:///browser/components/aiwindow/ui/modules/AIWindow.sys.mjs",
  SearchService: "moz-src:///toolkit/components/search/SearchService.sys.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "console", () =>
  console.createInstance({
    logModule: "Conversation",
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
 * Records the smart_window.tool_call Glean event for any tool invocation
 * issued by the assistant. The prompt_version extra captures the Remote
 * Settings prompt configuration version loaded for the chat feature so
 * tool-call behavior can be correlated with prompt revisions during
 * debugging.
 *
 * @param {object} options
 * @param {string} options.toolName
 * @param {"fullpage" | "sidebar" | "urlbar"} options.mode
 * @param {ChatConversation} options.conversation
 * @param {string} options.error - Canonical error code, or "" on success
 */
function recordToolCallEvent({ toolName, mode, conversation, error }) {
  Glean.smartWindow.toolCall.record({
    location: mode,
    chat_id: conversation.id,
    message_seq: conversation.messageCount,
    tool_name: toolName,
    model: conversation.engine?.model,
    prompt_version: conversation.systemPromptVersion,
    error,
  });
}

/**
 * Chat
 */
export const Chat = {};

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
/**
 * Attach a default clientReason to a streaming error if it doesn't already
 * carry classification info we recognize downstream.
 *
 * @param {unknown} err
 */
function classifyStreamingError(err) {
  if (!err || (typeof err !== "object" && typeof err !== "function")) {
    return;
  }
  const hasClassification =
    err.clientReason ||
    "status" in err ||
    err.error ||
    err.metadata?.errorMessage;
  if (!hasClassification) {
    err.clientReason = Services.io.offline ? "offline" : "connectionFailure";
  }
}

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

/** @typedef {{modify_slug: string | undefined}} AITabToolUIArgs */
/** @typedef {AITabToolUIArgs | undefined} ToolUIArgs */

/**
 * Gets the necessary ToolUI args for a particular ToolUI
 * so it can add the args to the tool message
 *
 * @param {string} toolName
 * @param {object} toolParams
 *
 * @returns {ToolUIArgs}
 */
function getToolUIArgs(toolName, toolParams) {
  switch (toolName) {
    case GENERATE_AITAB:
      return { modify_slug: toolParams.modify_slug };
    default:
      return undefined;
  }
}

Object.assign(Chat, {
  lastUsage: null,

  /* eslint-disable complexity -- TODO Bug 2075083 - complexity 58/54 over limit */
  /**
   * Stream assistant output with tool-call support.
   * Yields assistant text chunks as they arrive. If the model issues tool calls,
   * we execute them locally, append results to the conversation, and continue
   * streaming the model's follow-up answer. Repeats until no more tool calls.
   *
   * @param {object} options
   * @param {ChatConversation} options.conversation
   * @param {BrowsingContext} options.browsingContext - Omitted for tests only.
   * @param {"fullpage" | "sidebar" | "urlbar"} options.mode - See the MODE in ai-window.mjs
   * @param {AbortSignal} [options.signal]
   * @param {Promise<string|null>} [options.tokenPromise] - A token
   *   fetch the caller already started, so a cold or expired token resolves
   *   alongside prompt construction rather than after it. Omit to fetch here.
   */
  async fetchWithHistory({
    conversation,
    browsingContext,
    mode,
    signal,
    tokenPromise,
  }) {
    if (!browsingContext && !Cu.isInAutomation) {
      const err = new Error(
        "The browsingContext must exist for fetchWithHistory unless we're in automation."
      );
      err.clientReason = "missingBrowsingContext";
      throw err;
    }
    const token = await (tokenPromise ?? AuthProvider.getToken());
    if (!token) {
      console.error("fetchWithHistory Account Token null or undefined");
      const tokenError = new Error("Token unavailable");
      tokenError.clientReason = "fxaTokenUnavailable";
      throw tokenError;
    }

    const currentTurn = conversation.currentTurnIndex();

    /**
     * On the first turn if `search_the_web` fallbacks to run_search, uses the
     * user's verbatim query; later turns use a model-generated query. See
     * comment above tool execution for further details.
     */
    const isVerbatimQuery = currentTurn === 0;
    const chatToolsConfig = filterFeatureGatedTools(toolsConfig, conversation);

    let fullResponseText = "";

    // A search handoff sets `_searchExecutedTurn` (in executeToolByName). If a
    // later re-entry on the same turn (openSidebarAndContinue -> fetchWithHistory)
    // sees it, we block a repeat search_the_web so the handoff can't re-trigger
    // itself in a loop; the model stays free to answer or call another tool.
    const searchExecuted = conversation._searchExecutedTurn === currentTurn;

    const streamModelResponse = () => {
      const snapshot = conversation.compactChatCompletions();

      const stream = conversation.runWithGenerator({
        streamOptions: { enabled: true },
        token,
        chatId: conversation.id,
        tools: chatToolsConfig,
        args: snapshot,
        inferenceParams: { tool_choice: "auto" },
        signal,
      });

      // Logged after runWithGenerator, which is what commits the flags, so the
      // ones printed are the ones this request is actually governed by. The
      // generator body does not run until it is iterated, so nothing has been
      // sent yet.
      lazy.console.log(
        `Request (${conversation.securityProperties.getLogText()})`,
        snapshot.at(-1)
      );

      // Debug logging: Record only the latest message being sent to the model
      logConversationStream(currentTurn, "CHAT SEND", snapshot.at(-1));

      return stream;
    };

    while (true) {
      /** @type {ToolCall[] | null} */
      let pendingToolCalls = null;

      ChromeUtils.addProfilerMarker(
        "SmartWindow",
        {},
        "chat-server-request-start"
      );
      const turnStart = ChromeUtils.now();
      try {
        this.lastUsage = null;
        const response = await conversation.receiveResponse(
          streamModelResponse()
        );
        fullResponseText = response.fullResponseText;
        pendingToolCalls = response.pendingToolCalls;

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
        classifyStreamingError(err);
        throw err;
      } finally {
        ChromeUtils.addProfilerMarker(
          "SmartWindow",
          { startTime: turnStart },
          "ServerE2E"
        );
      }

      if (!pendingToolCalls || pendingToolCalls.length === 0) {
        ChromeUtils.addProfilerMarker("SmartWindow", {}, "chat-no-tool-calls");
        // Debug logging: Mark the end of the streaming loop for this turn
        logConversationStream(currentTurn, "STREAM END");
        if (!conversation.engine?.isCustomEndpoint) {
          // We only run telemetry on our own endpoints
          runLLMaJTelemetry(conversation);
        }
        return;
      }

      if (signal?.aborted) {
        logConversationStream(currentTurn, "STREAM END", null, "aborted");
        return;
      }

      // Ensure all serialized tool calls have arguments and expand URL tokens.
      const toolCalls = structuredClone(pendingToolCalls);
      for (const toolCall of toolCalls) {
        if (!toolCall.function.arguments) {
          toolCall.function.arguments = "{}";
        }
        expandUrlTokensInToolParams(toolCall.function, conversation.tokenToUrl);
      }

      conversation.addAssistantMessage("function", {
        tool_calls: toolCalls,
      });

      const toolMessages = new Map(
        pendingToolCalls.map(toolCall => [
          toolCall.id,
          conversation.addToolCallMessage({
            tool_call_id: toolCall.id,
            body: { pending: true },
          }),
        ])
      );
      const unresolvedToolCalls = new Set(pendingToolCalls);
      let activeToolCall = null;
      let isSearchHandoff = false;

      lazy.AIWindow.chatStore?.updateConversation(conversation).catch(() => {});

      ChromeUtils.addProfilerMarker(
        "SmartWindow",
        {},
        `chat-tools-detected(${pendingToolCalls.length})`
      );

      const resolveToolCall = (toolCall, content) => {
        if (!unresolvedToolCalls.delete(toolCall)) {
          return false;
        }
        conversation.updateToolCallMessage(
          toolMessages.get(toolCall.id),
          content
        );
        if (!unresolvedToolCalls.size) {
          signal?.removeEventListener("abort", abortToolCalls);
        }
        return true;
      };

      // Resolve skipped calls in place so they stay with their original batch.
      const resolveToolCalls = (calls, body, error) => {
        for (const remaining of calls) {
          const content = {
            tool_call_id: remaining.id,
            body: { error: body },
          };
          const { name, args } = toolMessages.get(remaining.id).content;
          if (name) {
            content.name = name;
            content.args = args;
          }
          if (!resolveToolCall(remaining, content)) {
            continue;
          }
          recordToolCallEvent({
            toolName: remaining.function?.name || "",
            mode,
            conversation,
            error,
          });
        }
      };

      const abortToolCalls = () => {
        if (isSearchHandoff) {
          return;
        }
        resolveToolCalls(
          [...unresolvedToolCalls].filter(tc => tc !== activeToolCall),
          "Tool call skipped because the response was stopped.",
          "aborted"
        );
        lazy.AIWindow.chatStore
          ?.updateConversation(conversation)
          .catch(() => {});
      };
      signal?.addEventListener("abort", abortToolCalls, { once: true });

      // runSearchTheWeb escalates to a search handoff once a search has run
      // this turn. The calls in one batch are a single model round, so each
      // search sees the state from before the batch; the handoff waits for
      // the next round.
      const shouldSearchHandoff =
        conversation._searchTheWebTurn === currentTurn;
      const answersToolCall =
        !searchExecuted &&
        !shouldSearchHandoff &&
        selectSearchTheWebPath(conversation) === SEARCH_THE_WEB_PATH.ANSWERS
          ? (pendingToolCalls.find(tc => {
              if (tc.function?.name !== SEARCH_THE_WEB) {
                return false;
              }
              try {
                const query = JSON.parse(tc.function.arguments || "{}")?.query;
                return typeof query === "string" && Boolean(query.trim());
              } catch {
                return false;
              }
            }) ?? null)
          : null;
      const orderedToolCalls = answersToolCall
        ? [
            answersToolCall,
            ...pendingToolCalls.filter(tc => tc !== answersToolCall),
          ]
        : pendingToolCalls;

      for (const toolCall of orderedToolCalls) {
        if (signal?.aborted) {
          abortToolCalls();
          logConversationStream(currentTurn, "STREAM END", null, "aborted");
          return;
        }

        activeToolCall = null;
        isSearchHandoff = false;

        const { id, function: functionSpec } = toolCall;
        const toolName = functionSpec?.name || "";
        let toolParams = {};

        let blockedError = "";
        let blockedBody;
        // A search handoff already ran this turn. Block a repeat search so
        // openSidebarAndContinue cannot re-trigger the handoff in a loop.
        if (toolName === SEARCH_THE_WEB && searchExecuted) {
          blockedError = "duplicate_search";
          blockedBody =
            "ERROR: search_the_web tool call error: You have already searched this turn. Respond to the user with what you found and ask if they want you to search again.";
        } else if (toolName === GET_USER_MEMORIES) {
          // If the user disabled memories in the last message, the assistant
          // should not be able to retrieve them using the get_user_memories tool.
          const lastUserMessage = conversation.messages.findLast(
            m => m.role === MESSAGE_ROLE.USER
          );
          if (lastUserMessage?.memoriesEnabled === false) {
            blockedError = "memories_disabled";
            blockedBody =
              "ERROR: get_user_memories tool call error: inform the user that they have disabled memories, so they cannot be retrieved.";
          }
        }
        if (blockedError) {
          resolveToolCall(toolCall, {
            tool_call_id: id,
            body: blockedBody,
            name: toolName,
          });
          recordToolCallEvent({
            toolName,
            mode,
            conversation,
            error: blockedError,
          });
          continue;
        }

        ChromeUtils.addProfilerMarker(
          "SmartWindow",
          {},
          `chat-run-tool-start(${toolName})`
        );
        const toolStart = ChromeUtils.now();

        try {
          toolParams = functionSpec?.arguments
            ? JSON.parse(functionSpec.arguments)
            : {};

          expandUrlTokensInToolParams(toolParams, conversation.tokenToUrl);
        } catch (e) {
          ChromeUtils.addProfilerMarker(
            "SmartWindow",
            {},
            `chat-run-tool-error(${toolName}:argument-parse)`
          );
          const content = {
            tool_call_id: id,
            body: { error: "Invalid JSON arguments" },
          };
          resolveToolCall(toolCall, content);
          recordToolCallEvent({
            toolName,
            mode,
            conversation,
            error: "invalid_arguments",
          });
          continue;
        }

        // Capture the embedder element before running tools, as navigation during
        // a tool call such as search handoff can replace the browsing context.
        const originalEmbedderElement = browsingContext?.embedderElement;

        // Dispatch the required arguments to different tool calls. Wrap this in a
        // try/catch so the conversation can be updated for failed calls.
        let result;
        let aiTabEndsTurn = false;
        let tabCardShown = false;
        let toolCallError = "";
        // Set when the tool is writing the user-facing reply itself, in which
        // case it is delivered below and the model gets no follow-up turn.
        let directAnswerStream = null;
        const featureGatedHandler = FEATURE_GATED_HANDLERS.get(toolName);
        const dispatchTool = name =>
          executeToolByName(
            name,
            toolParams,
            id,
            conversation,
            browsingContext,
            mode
          );

        // Emit the tool message up front with a `pending`-marked placeholder
        // body so the action log renders its pending row while the slow work
        // runs; the real result is reconciled into the same row below.
        // Show the action log's pending row when the slow tool starts.
        const toolUIArgs = getToolUIArgs(toolName, toolParams);
        activeToolCall = toolCall;
        const pendingToolMessage = toolMessages.get(id);
        if (TOOLS_WITH_PENDING_ACTION_LOG.has(toolName)) {
          conversation.updateToolCallMessage(pendingToolMessage, {
            tool_call_id: id,
            body: { pending: true },
            name: toolName,
            args: toolUIArgs,
          });
        }

        try {
          if (featureGatedHandler) {
            result = await featureGatedHandler(
              toolParams,
              conversation,
              signal,
              mode,
              toolName === SEARCH_THE_WEB ? shouldSearchHandoff : id,
              browsingContext
            );
            if (toolName === GENERATE_AITAB) {
              aiTabEndsTurn = result.endsTurn;
              result = result.toolResult;
            }
            /**
             * On the first invocation of search_the_web, SearchProvider-powered
             * web search is done. On the second invocation, search handoff (previously
             * run_search tool) is called. The logic for run_search is maintained, including
             * the instruction to use the user's original query if this is the first turn
             */
            if (!signal?.aborted && result.requiresSearchHandoff) {
              isSearchHandoff = true;
              // Mark the turn as having handed off, so a re-entry on the same
              // turn (via openSidebarAndContinue) blocks a repeat search.
              conversation._searchExecutedTurn = currentTurn;
              if (isVerbatimQuery) {
                delete toolParams.query;
              }
              result = await dispatchTool(RUN_SEARCH);
            }
          } else {
            result = await dispatchTool(toolName);
            if (toolName === MANAGE_TABS) {
              tabCardShown = result.cardShown;
              result = result.toolResult;
            }
          }

          // Debug logging: Record the data returned by the tool before feeding it to the model
          logConversationStream(
            currentTurn,
            "TOOL EXEC",
            { arguments: toolParams, result },
            toolName
          );

          ChromeUtils.addProfilerMarker(
            "SmartWindow",
            { startTime: toolStart },
            `chat-run-tool-complete(${toolName})`
          );

          const split = splitDirectAnswerStream(result);
          directAnswerStream = split.directAnswerStream;
          const content = {
            tool_call_id: id,
            body: split.toolBody,
            name: toolName,
            args: toolUIArgs,
          };
          resolveToolCall(toolCall, content);
        } catch (error) {
          isSearchHandoff = false;
          console.error(error);
          toolCallError = "execution_failed";
          result = { error: `Tool execution failed: ${String(error)}` };
          ChromeUtils.addProfilerMarker(
            "SmartWindow",
            { startTime: toolStart },
            `chat-run-tool-error(${toolName})`
          );
          // Only carry `name` (which drives the action-log card) when we
          // already showed a pending row, so it resolves. Otherwise a failed
          // call renders nothing, as before.
          const content = { tool_call_id: id, body: result, args: toolUIArgs };
          if (pendingToolMessage.content.name) {
            content.name = toolName;
          }
          resolveToolCall(toolCall, content);
        } finally {
          if (toolName === GENERATE_AITAB && !aiTabEndsTurn) {
            const message = conversation.messages.findLast(
              m => m.toolUIData?.toolCallId === id
            );
            if (message) {
              await conversation.updateToolUI(message, {}, null);
            }
          }
        }

        // A failed search returns an error-bearing result rather than
        // throwing, which would otherwise count as a success. The precise
        // category is on the search_the_web event.
        if (
          !toolCallError &&
          !isSearchHandoff &&
          toolName === SEARCH_THE_WEB &&
          result?.error
        ) {
          toolCallError = "execution_failed";
        }

        recordToolCallEvent({
          toolName,
          mode,
          conversation,
          error: toolCallError,
        });

        if (signal?.aborted && !isSearchHandoff) {
          abortToolCalls();
          logConversationStream(currentTurn, "STREAM END", null, "aborted");
          return;
        }

        const endsTurn =
          tabCardShown ||
          aiTabEndsTurn ||
          directAnswerStream ||
          isSearchHandoff;
        if (endsTurn) {
          resolveToolCalls(
            [...unresolvedToolCalls],
            "Tool call skipped because another tool ended the turn.",
            "skipped"
          );
        }

        lazy.AIWindow.chatStore
          ?.updateConversation(conversation)
          .catch(() => {});

        // These successful tools finish their turns in the UI.
        if (tabCardShown || aiTabEndsTurn) {
          // These tool calls need to return early so any staged
          // security flags that have accumulated up to this point
          // need to be committed before returning since the commit
          // at the end of the loop will not be reached.
          conversation.securityProperties.commit();
          return;
        }

        // Also terminal: the tool is writing the reply itself (the answers path
        // of search_the_web), so stream it into the assistant message rather
        // than spending another model turn rewriting prose we already have.
        // Same receiveResponse the model's own output goes through, so it picks
        // up the streaming updates, citations, persistence and completion.
        if (directAnswerStream) {
          await conversation.receiveResponse(directAnswerStream);
          logConversationStream(currentTurn, "STREAM END", null, toolName);
          return;
        }

        // Perform the search handoff if the RUN_SEARCH tool was run.
        if (isSearchHandoff) {
          const win = originalEmbedderElement?.documentGlobal;
          if (!win || win.closed) {
            console.error(
              "search_the_web: Associated window not available or closed, aborting search handoff"
            );
            return;
          }

          lazy.AIWindow.openSidebarAndContinue(win, conversation);
          return;
        }
      }
      if (signal?.aborted) {
        logConversationStream(currentTurn, "STREAM END", null, "aborted");
        return;
      }
    }
  },
});
