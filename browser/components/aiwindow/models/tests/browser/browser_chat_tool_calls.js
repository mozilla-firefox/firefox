/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { MockEngineManager, MockSearchManager } = ChromeUtils.importESModule(
  "resource://testing-common/AIWindowTestUtils.sys.mjs"
);
const { Chat } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/Chat.sys.mjs"
);
const { ChatConversation } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/ui/modules/ChatConversation.sys.mjs"
);
const { ChatStore } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/ui/modules/ChatStore.sys.mjs"
);
const { SearchService } = ChromeUtils.importESModule(
  "moz-src:///toolkit/components/search/SearchService.sys.mjs"
);
const { MESSAGE_ROLE } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/ui/modules/AIWindowConstants.sys.mjs"
);
const { ToolUI, UI_TYPES, UI_UPDATE_TYPES } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/ui/modules/ToolUI.sys.mjs"
);
const { MODEL_FEATURES, SERVICE_TYPES, PURPOSES } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/Utils.sys.mjs"
);
const {
  GET_OPEN_TABS,
  GET_PAGE_CONTENT,
  GET_USER_MEMORIES,
  GetPageContent,
  MANAGE_TABS,
  RunSearch,
  SEARCH_THE_WEB,
  SEARCH_THE_WEB_ANSWERS_PREF,
  SEARCH_THE_WEB_FAST_PREF,
  toolFns,
} = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/Tools.sys.mjs"
);

const PAGE_TEXT =
  "This article describes a Firefox tool call integration test.";
const PAGE_HTML = `<!DOCTYPE html><html><head><title>Tool Call Test</title></head>
     <body><article><h1>Tool Call Test</h1><p>${PAGE_TEXT}</p></article></body></html>`;
const URL_TOKEN = "§url_token: UNKNOWN_1§";
const SKIPPED_BODY = {
  error: "Tool call skipped because another tool ended the turn.",
};

/**
 * Serves the test page in a tab, mocks the model, and hands the task the
 * pieces every scenario needs. Tears everything down even when the task fails.
 *
 * @param {(fixture: object) => Promise<void>} task
 */
async function withToolCallFixture(task) {
  const { url, server } = serveHTML(PAGE_HTML);
  const tab = await BrowserTestUtils.openNewForegroundTab(gBrowser, url);
  const activeWindowStub = sinon
    .stub(AIWindow, "isAIWindowActive")
    .callsFake(win => win === window);
  const mockEngineManager = new MockEngineManager();
  const fetchPromises = [];

  const createConversation = async userOpts => {
    const conversation = new ChatConversation({
      pageUrl: new URL(url),
      pageMeta: {},
    });
    conversation.engine = await openAIEngine.build({
      model: "test-model",
      serviceType: SERVICE_TYPES.AI,
      purpose: PURPOSES.CHAT,
      flowId: null,
      feature: MODEL_FEATURES.CHAT,
    });
    conversation.addUserMessage(
      "List my tabs and read the test article.",
      null,
      userOpts
    );
    conversation.addAssistantMessage("text", "");
    conversation.tokenToUrl.set("UNKNOWN_1", url);
    conversation.urlToToken.set(url, "UNKNOWN_1");
    return conversation;
  };

  const fetchWithHistory = (conversation, options = {}) => {
    const promise = Chat.fetchWithHistory({
      conversation,
      browsingContext: tab.linkedBrowser.browsingContext,
      mode: "sidebar",
      ...options,
    });
    fetchPromises.push(promise);
    return promise;
  };

  const captureChatRequest = () =>
    mockEngineManager.captureRequest({ purpose: PURPOSES.CHAT });

  try {
    await task({
      url,
      mockEngineManager,
      createConversation,
      fetchWithHistory,
      captureChatRequest,
    });
  } finally {
    await mockEngineManager.rejectAllRequests();
    await Promise.allSettled(fetchPromises);
    mockEngineManager.cleanupMocks();
    activeWindowStub.restore();
    BrowserTestUtils.removeTab(tab);
    await new Promise(resolve => server.stop(resolve));
  }
}

function makeToolCall(id, name, args = "") {
  return { id, type: "function", function: { name, arguments: args } };
}

function tabsCall() {
  return makeToolCall("call_tabs", GET_OPEN_TABS);
}

function pageCall() {
  return makeToolCall(
    "call_page",
    GET_PAGE_CONTENT,
    JSON.stringify({ url_list: [URL_TOKEN] })
  );
}

/**
 * Asserts the batch reached the model as one assistant message followed by one
 * result per call, in order, and returns both for further assertions.
 *
 * @param {object[]} messages - The messages of the follow-up model request.
 * @param {object[]} toolCalls - The tool calls the first turn responded with.
 * @returns {{batch: object, results: object[]}}
 */
function getToolResults(messages, toolCalls) {
  const assistantIndex = messages.findIndex(m => m.tool_calls);
  Assert.greaterOrEqual(
    assistantIndex,
    0,
    "The assistant tool calls are sent back to the model"
  );
  Assert.equal(
    messages.filter(m => m.tool_calls).length,
    1,
    "The batch is recorded in one assistant message"
  );
  const batch = messages[assistantIndex];
  Assert.deepEqual(
    batch.tool_calls.map(tc => tc.id),
    toolCalls.map(tc => tc.id),
    "Every requested call is preserved in order"
  );
  const results = messages.slice(assistantIndex + 1);
  Assert.deepEqual(
    results.map(m => [m.role, m.tool_call_id]),
    toolCalls.map(tc => ["tool", tc.id]),
    "Every call has exactly one result, in tool_calls order, right after the assistant message"
  );
  return { batch, results };
}

function getToolCallEvents() {
  return (Glean.smartWindow.toolCall.testGetValue() ?? [])
    .map(event => [event.extra.tool_name, event.extra.error])
    .sort();
}

add_task(async function test_chat_tool_call_batches() {
  await withToolCallFixture(
    async ({
      mockEngineManager,
      createConversation,
      fetchWithHistory,
      captureChatRequest,
    }) => {
      for (const scenario of [
        "success",
        "invalid_arguments",
        "empty_later_arguments",
      ]) {
        info(`Testing tool call batch: ${scenario}`);
        const conversation = await createConversation();
        const fetchPromise = fetchWithHistory(conversation);
        const firstTurn = await captureChatRequest();
        const tabs = tabsCall();
        if (scenario === "invalid_arguments") {
          tabs.function.arguments = "invalid json {";
        }
        const toolCalls =
          scenario === "empty_later_arguments"
            ? [pageCall(), tabs]
            : [tabs, pageCall()];
        firstTurn.respond({ text: "", toolCalls });

        const followupTurn = await captureChatRequest();
        const { batch, results } = getToolResults(
          followupTurn.request.args,
          toolCalls
        );
        const pageResult = JSON.parse(
          results.find(m => m.tool_call_id === "call_page").content
        );
        Assert.ok(
          pageResult.some(text => text.includes(PAGE_TEXT)),
          "The follow-up includes text extracted from the real page"
        );
        const tabsResult = JSON.parse(
          results.find(m => m.tool_call_id === "call_tabs").content
        );
        if (scenario === "invalid_arguments") {
          Assert.equal(
            tabsResult.error,
            "Invalid JSON arguments",
            "Invalid arguments produce a matching error without losing later calls"
          );
        } else {
          Assert.ok(
            tabsResult.tabs.some(
              item =>
                item.url === URL_TOKEN && item.title.includes("Tool Call Test")
            ),
            "The follow-up includes the real open tab"
          );
          Assert.equal(
            batch.tool_calls.find(tc => tc.id === "call_tabs").function
              .arguments,
            "{}",
            "Empty arguments are normalized even for later calls"
          );
        }

        const answer = "I found the test tab and read its article.";
        followupTurn.respond(answer);
        await fetchPromise;
        Assert.equal(
          conversation.messages.findLast(
            m => m.role === MESSAGE_ROLE.ASSISTANT && m.content.type === "text"
          ).content.body,
          answer,
          "The assistant finishes the turn after receiving the complete batch"
        );
        mockEngineManager.assertAllRequestsHandled();
      }
    }
  );
});

add_task(async function test_terminal_tool_skips_remaining_calls() {
  await withToolCallFixture(
    async ({
      mockEngineManager,
      createConversation,
      fetchWithHistory,
      captureChatRequest,
    }) => {
      Services.fog.testResetFOG();
      const pageContentSpy = sinon.spy(GetPageContent, "getPageContentText");
      try {
        const conversation = await createConversation();
        const fetchPromise = fetchWithHistory(conversation);
        const firstTurn = await captureChatRequest();
        const toolCalls = [
          makeToolCall(
            "call_manage",
            MANAGE_TABS,
            JSON.stringify({
              action: "close_tabs",
              ask_confirmation: true,
              url_tokens: [URL_TOKEN],
            })
          ),
          pageCall(),
        ];
        firstTurn.respond({ text: "", toolCalls });
        await fetchPromise;

        mockEngineManager.assertAllRequestsHandled();
        Assert.ok(
          pageContentSpy.notCalled,
          "A tool after the terminal one never runs"
        );
        const toolMessages = conversation.messages.filter(
          m => m.role === MESSAGE_ROLE.TOOL
        );
        Assert.deepEqual(
          toolMessages.map(m => m.content.tool_call_id),
          toolCalls.map(tc => tc.id),
          "Tool results stay in tool_calls order when a terminal tool ends the turn"
        );
        Assert.ok(
          toolMessages.find(m => m.content.tool_call_id === "call_manage")
            .content.body.pending,
          "The terminal tool's own result is recorded"
        );
        Assert.deepEqual(
          toolMessages.find(m => m.content.tool_call_id === "call_page")
            ?.content.body,
          SKIPPED_BODY,
          "The remaining call is resolved as skipped"
        );
        Assert.deepEqual(
          getToolCallEvents(),
          [
            [GET_PAGE_CONTENT, "skipped"],
            [MANAGE_TABS, ""],
          ].sort(),
          "Skipped calls record a tool_call event like executed ones"
        );
      } finally {
        pageContentSpy.restore();
      }
    }
  );
});

add_task(async function test_tool_confirmation_inside_batch() {
  for (const confirmed of [true, false]) {
    info(`Testing a batched tool confirmation: confirmed=${confirmed}`);
    await withToolCallFixture(
      async ({
        url,
        mockEngineManager,
        createConversation,
        fetchWithHistory,
        captureChatRequest,
      }) => {
        const conversation = await createConversation();
        const fetchPromise = fetchWithHistory(conversation);
        const firstTurn = await captureChatRequest();
        const toolCalls = [
          tabsCall(),
          makeToolCall(
            "call_manage",
            MANAGE_TABS,
            JSON.stringify({
              action: "group_tabs",
              ask_confirmation: true,
              url_tokens: [URL_TOKEN],
              label: "Tool call batch",
            })
          ),
          pageCall(),
        ];
        firstTurn.respond({ text: "", toolCalls });
        await fetchPromise;
        mockEngineManager.assertAllRequestsHandled();

        const toolMessages = conversation.messages.filter(
          m => m.role === MESSAGE_ROLE.TOOL
        );
        Assert.deepEqual(
          toolMessages.map(m => m.content.tool_call_id),
          toolCalls.map(tc => tc.id),
          "The confirmation and skipped result preserve the batch order"
        );
        const pendingMessage = toolMessages[1];
        Assert.ok(pendingMessage.content.body.pending, "The action is pending");
        Assert.deepEqual(
          toolMessages[2].content.body,
          SKIPPED_BODY,
          "The call after the confirmation is skipped"
        );
        const otherResults = structuredClone([
          toolMessages[0].content,
          toolMessages[2].content,
        ]);
        const message = conversation.messages.find(
          m => m.toolUIData?.toolCallId === "call_manage"
        );
        Assert.equal(
          message?.toolUIData.uiType,
          UI_TYPES.TAB_GROUP_CONFIRMATION,
          "The batch creates a tab-group confirmation"
        );

        const result = await ToolUI.handleUpdate(
          {
            messageId: message.id,
            toolCallId: "call_manage",
            updateType: confirmed
              ? UI_UPDATE_TYPES.CONFIRM_TAB_GROUP_SELECTION
              : UI_UPDATE_TYPES.CANCEL_TAB_SELECTION,
            updateData: {
              selectedTabs: message.toolUIData.properties.tabs,
              tabGroupLabel: "Tool call batch",
              actionType: "group_tabs",
            },
          },
          conversation,
          window,
          "sidebar"
        );
        Assert.ok(result, "The confirmation update succeeds");
        Assert.equal(
          message.toolUIData.uiType,
          confirmed ? UI_TYPES.AI_ACTION_RESULT : UI_TYPES.CANCELLED_COMPONENT,
          "The confirmation card reflects the user's choice"
        );
        Assert.ok(
          !pendingMessage.content.body.pending,
          "The matching tool result is resolved even with a later skipped result"
        );
        if (confirmed) {
          Assert.equal(
            pendingMessage.content.body.action,
            "group_tabs",
            "The confirmed tool result preserves the original action"
          );
          Assert.deepEqual(
            pendingMessage.content.body.selectedTabs.map(tab => tab.url),
            [url],
            "The confirmed tool result describes the grouped tab"
          );
        } else {
          Assert.equal(
            pendingMessage.content.body.description,
            "User cancelled the tab action. No action was taken.",
            "The tool result records the cancellation"
          );
        }
        Assert.deepEqual(
          [toolMessages[0].content, toolMessages[2].content],
          otherResults,
          "Resolving the confirmation leaves the other tool results unchanged"
        );
        Assert.deepEqual(
          conversation.messages.filter(m => m.role === MESSAGE_ROLE.TOOL),
          toolMessages,
          "The tool results remain in place without duplicates"
        );
      }
    );
  }
});

add_task(async function test_blocked_tool_inside_batch() {
  await withToolCallFixture(
    async ({
      mockEngineManager,
      createConversation,
      fetchWithHistory,
      captureChatRequest,
    }) => {
      Services.fog.testResetFOG();
      const memoriesSpy = sinon.spy(toolFns, "getUserMemories");
      try {
        const conversation = await createConversation({
          memoriesEnabled: false,
        });
        const fetchPromise = fetchWithHistory(conversation);
        const firstTurn = await captureChatRequest();
        const toolCalls = [
          tabsCall(),
          makeToolCall("call_memories", GET_USER_MEMORIES),
        ];
        firstTurn.respond({ text: "", toolCalls });

        const followupTurn = await captureChatRequest();
        const { results } = getToolResults(
          followupTurn.request.args,
          toolCalls
        );
        Assert.ok(memoriesSpy.notCalled, "The blocked tool never runs");
        Assert.ok(
          String(results[1].content).includes("they have disabled memories"),
          "The blocked call gets the memories-disabled error"
        );
        const tabsResult = JSON.parse(results[0].content);
        Assert.ok(
          tabsResult.tabs.some(
            item =>
              item.url === URL_TOKEN && item.title.includes("Tool Call Test")
          ),
          "The tool before the blocked one runs normally"
        );

        followupTurn.respond("Memories are off, but here are your tabs.");
        await fetchPromise;
        mockEngineManager.assertAllRequestsHandled();
        Assert.deepEqual(
          getToolCallEvents(),
          [
            [GET_OPEN_TABS, ""],
            [GET_USER_MEMORIES, "memories_disabled"],
          ].sort(),
          "Both the blocked and the executed call record a tool_call event"
        );
      } finally {
        memoriesSpy.restore();
      }
    }
  );
});

add_task(async function test_two_searches_in_one_batch() {
  await SpecialPowers.pushPrefEnv({
    set: [
      [SEARCH_THE_WEB_FAST_PREF, true],
      [SEARCH_THE_WEB_ANSWERS_PREF, false],
    ],
  });
  const mockSearchManager = new MockSearchManager();
  const handoffStub = sinon.stub(AIWindow, "openSidebarAndContinue");
  const runSearchStub = sinon
    .stub(RunSearch, "runSearch")
    .resolves("Search handoff results");
  const queries = ["first query", "second query"];
  const resultFor = index => ({
    title: `Result ${index + 1}`,
    url: `https://results.example/${index + 1}`,
    text: `The ${queries[index]} result page explains the topic in enough detail to be a useful snippet.`,
  });

  try {
    await withToolCallFixture(
      async ({
        mockEngineManager,
        createConversation,
        fetchWithHistory,
        captureChatRequest,
      }) => {
        const conversation = await createConversation();
        const fetchPromise = fetchWithHistory(conversation);
        const firstTurn = await captureChatRequest();
        const toolCalls = queries.map((query, index) =>
          makeToolCall(
            `call_search_${index + 1}`,
            SEARCH_THE_WEB,
            JSON.stringify({ query })
          )
        );
        firstTurn.respond({ text: "", toolCalls });

        const firstSearch = await mockSearchManager.captureRequest();
        Assert.equal(
          JSON.parse(firstSearch.request.options.body).query,
          queries[0],
          "The first call searches its own query"
        );
        firstSearch.respond({ results: [resultFor(0)] });

        await TestUtils.waitForCondition(
          () =>
            mockSearchManager.requests.length ||
            runSearchStub.called ||
            handoffStub.called,
          "Waiting for the second search to run or hand off"
        );
        Assert.ok(
          !runSearchStub.called && !handoffStub.called,
          "A second search_the_web in the same batch runs a search rather than a search handoff"
        );

        if (mockSearchManager.requests.length) {
          const secondSearch = await mockSearchManager.captureRequest();
          Assert.equal(
            JSON.parse(secondSearch.request.options.body).query,
            queries[1],
            "The second call searches its own query"
          );
          secondSearch.respond({ results: [resultFor(1)] });

          const followupTurn = await captureChatRequest();
          const { results } = getToolResults(
            followupTurn.request.args,
            toolCalls
          );
          results.forEach((result, index) => {
            const body = JSON.parse(result.content);
            Assert.equal(
              body.requiresSearchHandoff,
              false,
              `Search ${index + 1} completes without a handoff`
            );
            Assert.equal(
              body.results?.[0]?.snippet,
              resultFor(index).text,
              `Search ${index + 1} carries its own results`
            );
          });
          followupTurn.respond("Both searches are done.");
        }

        await fetchPromise;
        mockEngineManager.assertAllRequestsHandled();
      }
    );
  } finally {
    handoffStub.restore();
    runSearchStub.restore();
    mockSearchManager.rejectAllRequests();
    mockSearchManager.cleanupMocks();
    await SpecialPowers.popPrefEnv();
  }
});

add_task(async function test_invalid_search_preserves_handoff() {
  await SpecialPowers.pushPrefEnv({
    set: [
      [SEARCH_THE_WEB_FAST_PREF, true],
      [SEARCH_THE_WEB_ANSWERS_PREF, false],
    ],
  });
  const mockSearchManager = new MockSearchManager();
  const handoffStub = sinon.stub(AIWindow, "openSidebarAndContinue");
  const runSearchStub = sinon
    .stub(RunSearch, "runSearch")
    .resolves("Search handoff results");
  try {
    await withToolCallFixture(
      async ({
        mockEngineManager,
        createConversation,
        fetchWithHistory,
        captureChatRequest,
      }) => {
        const conversation = await createConversation();
        const fetchPromise = fetchWithHistory(conversation);
        const firstTurn = await captureChatRequest();
        const toolCalls = ["valid query", ""].map((query, index) =>
          makeToolCall(
            `call_search_${index}`,
            SEARCH_THE_WEB,
            JSON.stringify({ query })
          )
        );
        firstTurn.respond({ text: "", toolCalls });
        const search = await mockSearchManager.captureRequest();
        search.respond({
          results: [
            {
              title: "Search result",
              url: "https://results.example/1",
              text: "This search result explains the topic in enough detail to be a useful snippet.",
            },
          ],
        });
        const followupTurn = await captureChatRequest();
        const { results } = getToolResults(
          followupTurn.request.args,
          toolCalls
        );
        Assert.equal(
          JSON.parse(results[1].content).error,
          "a non-empty query is required",
          "The later search rejects its empty query"
        );
        Assert.equal(
          conversation._searchTheWebTurn,
          conversation.currentTurnIndex(),
          "The invalid search preserves the earlier search's turn marker"
        );
        followupTurn.respond({
          text: "",
          toolCalls: [
            makeToolCall(
              "call_handoff",
              SEARCH_THE_WEB,
              JSON.stringify({ query: "fallback query" })
            ),
          ],
        });
        await TestUtils.waitForCondition(
          () => handoffStub.called || mockSearchManager.requests.length,
          "Waiting for the next search to hand off or request another search"
        );
        Assert.ok(handoffStub.calledOnce, "The next round hands off");
        Assert.ok(runSearchStub.calledOnce, "The search handoff tool runs");
        mockSearchManager.rejectAllRequests();
        mockEngineManager.rejectAllRequests();
        await fetchPromise;
        mockEngineManager.assertAllRequestsHandled();
      }
    );
  } finally {
    handoffStub.restore();
    runSearchStub.restore();
    mockSearchManager.rejectAllRequests();
    mockSearchManager.cleanupMocks();
    await SpecialPowers.popPrefEnv();
  }
});

add_task(async function test_answers_search_preserves_other_calls_on_failure() {
  await SpecialPowers.pushPrefEnv({
    set: [[SEARCH_THE_WEB_ANSWERS_PREF, true]],
  });
  try {
    for (const scenario of ["success", "empty_query", "no_answer"]) {
      info(`Testing answers batch: ${scenario}`);
      await withToolCallFixture(
        async ({
          mockEngineManager,
          createConversation,
          fetchWithHistory,
          captureChatRequest,
        }) => {
          const sandbox = sinon.createSandbox();
          const tabsSpy = sandbox.spy(toolFns, "getOpenTabs");
          const pageSpy = sandbox.spy(GetPageContent, "getPageContentText");
          try {
            const conversation = await createConversation();
            const fetchPromise = fetchWithHistory(conversation);
            const firstTurn = await captureChatRequest();
            const searchCall = makeToolCall(
              "call_answers",
              SEARCH_THE_WEB,
              JSON.stringify({
                query: scenario === "empty_query" ? "" : "test query",
              })
            );
            const toolCalls = [tabsCall(), searchCall, pageCall()];
            firstTurn.respond({ text: "", toolCalls });

            const answer = "The search answered the query.";
            if (scenario !== "empty_query") {
              const answers = await mockEngineManager.captureRequest({
                purpose: "unknown",
              });
              Assert.ok(
                tabsSpy.notCalled && pageSpy.notCalled,
                "Search runs before the other calls"
              );
              answers.respond({
                finalOutput: scenario === "no_answer" ? "" : answer,
              });
            }

            if (scenario === "success") {
              await fetchPromise;
              Assert.ok(
                tabsSpy.notCalled && pageSpy.notCalled,
                "A direct answer skips the other tool calls"
              );
              const toolMessages = conversation.messages.filter(
                m => m.role === MESSAGE_ROLE.TOOL
              );
              Assert.deepEqual(
                toolMessages.map(m => m.content.tool_call_id),
                toolCalls.map(tc => tc.id),
                "Results preserve the original order after executing search first"
              );
              for (const message of toolMessages) {
                if (message.content.tool_call_id !== searchCall.id) {
                  Assert.deepEqual(message.content.body, SKIPPED_BODY);
                }
              }
              const searchBody = toolMessages.find(
                m => m.content.tool_call_id === searchCall.id
              ).content.body;
              Assert.strictEqual(
                searchBody.directAnswer,
                true,
                "The stored search result keeps the marker the action log reads"
              );
              Assert.ok(
                !("directAnswerStream" in searchBody),
                "The stream itself is not stored"
              );
            } else {
              const followupTurn = await captureChatRequest();
              const { results } = getToolResults(
                followupTurn.request.args,
                toolCalls
              );
              Assert.ok(
                tabsSpy.calledOnce && pageSpy.calledOnce,
                "The other calls run when search produces no answer stream"
              );
              Assert.ok(
                JSON.parse(
                  results.find(m => m.tool_call_id === searchCall.id).content
                ).error,
                "The search failure is recorded"
              );
              Assert.ok(
                JSON.parse(
                  results.find(m => m.tool_call_id === "call_page").content
                ).some(text => text.includes(PAGE_TEXT)),
                "The model receives the other tool's real result"
              );
              followupTurn.respond("Here is the article instead.");
              await fetchPromise;
            }
            mockEngineManager.assertAllRequestsHandled();
          } finally {
            sandbox.restore();
          }
        }
      );
    }
  } finally {
    await SpecialPowers.popPrefEnv();
  }
});

add_task(async function test_search_handoff_skips_remaining_calls() {
  const controller = new AbortController();
  const sandbox = sinon.createSandbox();
  const handoffStub = sandbox.stub(AIWindow, "openSidebarAndContinue");
  const runSearchStub = sandbox
    .stub(RunSearch, "runSearch")
    .callsFake(async () => {
      controller.abort();
      return "Search handoff results";
    });
  const pageContentSpy = sandbox.spy(GetPageContent, "getPageContentText");
  try {
    await withToolCallFixture(
      async ({
        mockEngineManager,
        createConversation,
        fetchWithHistory,
        captureChatRequest,
      }) => {
        Services.fog.testResetFOG();
        const conversation = await createConversation();
        conversation._searchTheWebTurn = conversation.currentTurnIndex();
        const fetchPromise = fetchWithHistory(conversation, {
          signal: controller.signal,
        });
        const firstTurn = await captureChatRequest();
        const toolCalls = [
          tabsCall(),
          makeToolCall(
            "call_handoff",
            SEARCH_THE_WEB,
            JSON.stringify({ query: "test query" })
          ),
          pageCall(),
        ];
        firstTurn.respond({ text: "", toolCalls });
        await fetchPromise;
        mockEngineManager.assertAllRequestsHandled();

        Assert.ok(runSearchStub.calledOnce, "The search handoff runs once");
        Assert.ok(
          handoffStub.calledOnceWithExactly(window, conversation),
          "The sidebar continues the conversation after the old request is aborted"
        );
        Assert.equal(
          conversation._searchExecutedTurn,
          conversation.currentTurnIndex(),
          "The handoff is recorded to prevent another search on this turn"
        );
        Assert.equal(
          Glean.smartWindow.searchHandoff.testGetValue()?.[0]?.extra.reason,
          "repeat_search",
          "The handoff is recorded as caused by the repeat search"
        );
        Assert.ok(
          pageContentSpy.notCalled,
          "The tool after the handoff never runs"
        );
        const toolMessages = conversation.messages.filter(
          m => m.role === MESSAGE_ROLE.TOOL
        );
        Assert.deepEqual(
          toolMessages.map(m => m.content.tool_call_id),
          toolCalls.map(tc => tc.id),
          "The handoff preserves one result per call in batch order"
        );
        Assert.equal(
          toolMessages[1].content.body,
          "Search handoff results",
          "The search result is recorded before continuing in the sidebar"
        );
        Assert.ok(
          Array.isArray(toolMessages[0].content.body.tabs),
          "The call before a search handoff runs normally"
        );
        Assert.deepEqual(
          toolMessages[2].content.body,
          SKIPPED_BODY,
          "The remaining tool is marked skipped by the handoff"
        );
      }
    );
  } finally {
    sandbox.restore();
  }
});

add_task(async function test_search_failure_hands_off() {
  for (const path of ["fast", "answers"]) {
    info(`Testing a ${path} search failure`);
    await SpecialPowers.pushPrefEnv({
      set: [
        [SEARCH_THE_WEB_FAST_PREF, path === "fast"],
        [SEARCH_THE_WEB_ANSWERS_PREF, path === "answers"],
      ],
    });
    const mockSearchManager = new MockSearchManager();
    const sandbox = sinon.createSandbox();
    const handoffStub = sandbox.stub(AIWindow, "openSidebarAndContinue");
    const runSearchStub = sandbox
      .stub(RunSearch, "runSearch")
      .resolves("Search handoff results");
    const pageContentSpy = sandbox.spy(GetPageContent, "getPageContentText");
    try {
      await withToolCallFixture(
        async ({
          mockEngineManager,
          createConversation,
          fetchWithHistory,
          captureChatRequest,
        }) => {
          Services.fog.testResetFOG();
          const conversation = await createConversation();
          const fetchPromise = fetchWithHistory(conversation);
          const firstTurn = await captureChatRequest();
          const toolCalls = [
            makeToolCall(
              "call_search",
              SEARCH_THE_WEB,
              JSON.stringify({ query: "test query" })
            ),
            pageCall(),
          ];
          firstTurn.respond({ text: "", toolCalls });
          if (path === "fast") {
            (await mockSearchManager.captureRequest()).respond(
              "upstream exploded",
              { status: 503 }
            );
          } else {
            (
              await mockEngineManager.captureRequest({ purpose: "unknown" })
            ).reject(new Error("Answer request failed"));
          }
          await fetchPromise;
          mockEngineManager.assertAllRequestsHandled();

          Assert.ok(runSearchStub.calledOnce, "The failed search hands off");
          Assert.ok(
            handoffStub.calledOnceWithExactly(window, conversation),
            "The sidebar continues the conversation"
          );
          Assert.ok(
            pageContentSpy.notCalled,
            "The call after the handoff never runs"
          );
          const toolMessages = conversation.messages.filter(
            m => m.role === MESSAGE_ROLE.TOOL
          );
          Assert.equal(
            toolMessages[0].content.body,
            "Search handoff results",
            "The handoff result is recorded for the search call"
          );
          Assert.deepEqual(
            toolMessages[1].content.body,
            SKIPPED_BODY,
            "The remaining call is marked skipped by the handoff"
          );
          Assert.deepEqual(
            getToolCallEvents(),
            [
              [GET_PAGE_CONTENT, "skipped"],
              [SEARCH_THE_WEB, "execution_failed"],
            ].sort(),
            "The search that handed off is still recorded as failed"
          );
          const handoffEvents = Glean.smartWindow.searchHandoff.testGetValue();
          Assert.equal(
            handoffEvents?.length,
            1,
            "The handoff is recorded once"
          );
          Assert.equal(
            handoffEvents?.[0]?.extra.reason,
            "search_failed",
            "The handoff is recorded as caused by the failed search"
          );
        }
      );
    } finally {
      sandbox.restore();
      mockSearchManager.rejectAllRequests();
      mockSearchManager.cleanupMocks();
      await SpecialPowers.popPrefEnv();
    }
  }
});

add_task(async function test_search_failure_after_stop_does_not_hand_off() {
  for (const path of ["fast", "answers"]) {
    info(`Testing a ${path} search that fails after the response is stopped`);
    await SpecialPowers.pushPrefEnv({
      set: [
        [SEARCH_THE_WEB_FAST_PREF, path === "fast"],
        [SEARCH_THE_WEB_ANSWERS_PREF, path === "answers"],
      ],
    });
    const mockSearchManager = new MockSearchManager();
    const sandbox = sinon.createSandbox();
    const handoffStub = sandbox.stub(AIWindow, "openSidebarAndContinue");
    const runSearchStub = sandbox
      .stub(RunSearch, "runSearch")
      .resolves("Search handoff results");
    try {
      await withToolCallFixture(
        async ({
          mockEngineManager,
          createConversation,
          fetchWithHistory,
          captureChatRequest,
        }) => {
          Services.fog.testResetFOG();
          const controller = new AbortController();
          const conversation = await createConversation();
          const fetchPromise = fetchWithHistory(conversation, {
            signal: controller.signal,
          });
          const firstTurn = await captureChatRequest();
          firstTurn.respond({
            text: "",
            toolCalls: [
              makeToolCall(
                "call_search",
                SEARCH_THE_WEB,
                JSON.stringify({ query: "test query" })
              ),
            ],
          });
          if (path === "fast") {
            const search = await mockSearchManager.captureRequest();
            controller.abort();
            search.respond("upstream exploded", { status: 503 });
          } else {
            const answers = await mockEngineManager.captureRequest({
              purpose: "unknown",
            });
            controller.abort();
            answers.reject(
              Object.assign(new Error("aborted"), { name: "AbortError" })
            );
          }

          Assert.equal(
            await fetchPromise,
            null,
            "The stopped response returns without a turn result"
          );
          mockEngineManager.assertAllRequestsHandled();
          Assert.ok(
            runSearchStub.notCalled,
            "A stopped response never runs the search handoff"
          );
          Assert.ok(
            handoffStub.notCalled,
            "A stopped response never continues in the sidebar"
          );
          Assert.equal(
            Glean.smartWindow.searchHandoff.testGetValue(),
            null,
            "No handoff is recorded"
          );
        }
      );
    } finally {
      sandbox.restore();
      mockSearchManager.rejectAllRequests();
      mockSearchManager.cleanupMocks();
      await SpecialPowers.popPrefEnv();
    }
  }
});

add_task(async function test_stop_before_move_to_sidebar_cancels_search() {
  // The search fails and the handoff starts, then the user stops the response
  // while the default search engine is looked up: before the chat's tab is
  // selected or a full-page chat moves to the sidebar. The real performSearch
  // runs, so loadSearch is the navigation that must not start.
  await SpecialPowers.pushPrefEnv({
    set: [
      [SEARCH_THE_WEB_FAST_PREF, true],
      [SEARCH_THE_WEB_ANSWERS_PREF, false],
    ],
  });
  const getDefaultEngine = SearchService.getDefault.bind(SearchService);
  try {
    for (const chatLocation of ["fullpage", "sidebar"]) {
      info(`Stopping a ${chatLocation} handoff before the move to the sidebar`);
      const mockSearchManager = new MockSearchManager();
      const sandbox = sinon.createSandbox();
      const handoffStub = sandbox.stub(AIWindow, "openSidebarAndContinue");
      const moveStub = sandbox
        .stub(AIWindow, "moveConversationToSidebar")
        .resolves(null);
      const loadSearchStub = sandbox
        .stub(SearchUIUtils, "loadSearch")
        .resolves();
      try {
        await withToolCallFixture(
          async ({
            url,
            mockEngineManager,
            createConversation,
            fetchWithHistory,
            captureChatRequest,
          }) => {
            Services.fog.testResetFOG();
            const controller = new AbortController();
            const lookupStub = sandbox
              .stub(SearchService, "getDefault")
              .callsFake(async () => {
                controller.abort();
                return getDefaultEngine();
              });
            const stopStubs = [lookupStub];
            if (chatLocation === "fullpage") {
              stopStubs.push(
                sandbox
                  .stub(AIWindow, "isAIWindowContentPage")
                  .callsFake(uri => uri?.spec === url)
              );
            }
            const conversation = await createConversation();
            try {
              const fetchPromise = fetchWithHistory(conversation, {
                signal: controller.signal,
              });
              const firstTurn = await captureChatRequest();
              firstTurn.respond({
                text: "",
                toolCalls: [
                  makeToolCall(
                    "call_search",
                    SEARCH_THE_WEB,
                    JSON.stringify({ query: "test query" })
                  ),
                ],
              });
              (await mockSearchManager.captureRequest()).respond(
                "upstream exploded",
                { status: 503 }
              );

              Assert.equal(
                await fetchPromise,
                null,
                "The stopped response returns without a turn result"
              );
              Assert.ok(
                lookupStub.calledOnce,
                "The handoff had started when the user stopped"
              );
              Assert.ok(
                moveStub.notCalled,
                "The chat is never moved to the sidebar"
              );
              Assert.ok(
                loadSearchStub.notCalled,
                "The search page never starts loading"
              );
              Assert.ok(
                handoffStub.notCalled,
                "The conversation does not continue in the sidebar"
              );
              Assert.equal(
                Glean.smartWindow.searchHandoff.testGetValue(),
                null,
                "No handoff is recorded"
              );
              Assert.deepEqual(
                getToolCallEvents(),
                [[SEARCH_THE_WEB, "execution_failed"]],
                "The failed search is recorded once"
              );
              Assert.notEqual(
                conversation._searchExecutedTurn,
                conversation.currentTurnIndex(),
                "The stopped handoff does not mark the turn as handed off"
              );
            } finally {
              stopStubs.forEach(stub => stub.restore());
            }

            // A retry runs on the same turn, so it must not be blocked as a
            // repeat of a handoff that never ran.
            Services.fog.testResetFOG();
            sandbox
              .stub(RunSearch, "runSearch")
              .resolves("Search handoff results");
            const retryPromise = fetchWithHistory(conversation);
            const retryTurn = await captureChatRequest();
            retryTurn.respond({
              text: "",
              toolCalls: [
                makeToolCall(
                  "call_retry",
                  SEARCH_THE_WEB,
                  JSON.stringify({ query: "test query" })
                ),
              ],
            });
            const chatEngine = mockEngineManager.engines.get(PURPOSES.CHAT);
            await TestUtils.waitForCondition(
              () => handoffStub.called || chatEngine.runRequests.size,
              "Waiting for the retried search to run or be blocked"
            );
            if (chatEngine.runRequests.size) {
              (await captureChatRequest()).respond("The search was blocked.");
            }
            await retryPromise;
            mockEngineManager.assertAllRequestsHandled();

            const retryEvents = getToolCallEvents();
            Assert.equal(
              retryEvents.length,
              1,
              "The retry makes one search call"
            );
            Assert.notEqual(
              retryEvents[0][1],
              "duplicate_search",
              "The retried search is not blocked as a duplicate"
            );
          }
        );
      } finally {
        sandbox.restore();
        mockSearchManager.rejectAllRequests();
        mockSearchManager.cleanupMocks();
      }
    }
  } finally {
    await SpecialPowers.popPrefEnv();
  }
});

add_task(async function test_stop_during_move_to_sidebar_completes_handoff() {
  // Once a full-page chat starts moving to the sidebar, the handoff finishes
  // even if the user stops the response, so the chat is not left open in both
  // the full page and the sidebar.
  await SpecialPowers.pushPrefEnv({
    set: [
      [SEARCH_THE_WEB_FAST_PREF, true],
      [SEARCH_THE_WEB_ANSWERS_PREF, false],
    ],
  });
  const mockSearchManager = new MockSearchManager();
  const sandbox = sinon.createSandbox();
  const handoffStub = sandbox.stub(AIWindow, "openSidebarAndContinue");
  const loadSearchStub = sandbox.stub(SearchUIUtils, "loadSearch").resolves();
  // The stubbed loadSearch loads no page, so let the wait for one time out at
  // once rather than after the default 15 seconds.
  const navigationTimeout = RunSearch.NAVIGATION_TIMEOUT_MS;
  RunSearch.NAVIGATION_TIMEOUT_MS = 0;
  try {
    await withToolCallFixture(
      async ({
        url,
        mockEngineManager,
        createConversation,
        fetchWithHistory,
        captureChatRequest,
      }) => {
        Services.fog.testResetFOG();
        const controller = new AbortController();
        sandbox
          .stub(AIWindow, "isAIWindowContentPage")
          .callsFake(uri => uri?.spec === url);
        const moveStub = sandbox
          .stub(AIWindow, "moveConversationToSidebar")
          .callsFake(async () => controller.abort());
        const conversation = await createConversation();
        const fetchPromise = fetchWithHistory(conversation, {
          signal: controller.signal,
        });
        const firstTurn = await captureChatRequest();
        firstTurn.respond({
          text: "",
          toolCalls: [
            makeToolCall(
              "call_search",
              SEARCH_THE_WEB,
              JSON.stringify({ query: "test query" })
            ),
          ],
        });
        (await mockSearchManager.captureRequest()).respond(
          "upstream exploded",
          { status: 503 }
        );

        Assert.equal(
          await fetchPromise,
          null,
          "The handed-off response returns without a turn result"
        );
        mockEngineManager.assertAllRequestsHandled();
        Assert.ok(
          moveStub.calledOnce && controller.signal.aborted,
          "The user stopped while the chat moved to the sidebar"
        );
        Assert.ok(loadSearchStub.calledOnce, "The search page starts loading");
        Assert.ok(
          handoffStub.calledOnce,
          "The handoff continues in the sidebar"
        );
        Assert.equal(
          conversation._searchExecutedTurn,
          conversation.currentTurnIndex(),
          "The completed handoff keeps the turn marked as handed off"
        );
        const handoffEvents = Glean.smartWindow.searchHandoff.testGetValue();
        Assert.equal(handoffEvents?.length, 1, "The handoff is recorded once");
        Assert.equal(
          handoffEvents?.[0]?.extra.reason,
          "search_failed",
          "The handoff is recorded as caused by the failed search"
        );
        Assert.deepEqual(
          getToolCallEvents(),
          [[SEARCH_THE_WEB, "execution_failed"]],
          "The failed search is recorded once"
        );
      }
    );
  } finally {
    RunSearch.NAVIGATION_TIMEOUT_MS = navigationTimeout;
    sandbox.restore();
    mockSearchManager.rejectAllRequests();
    mockSearchManager.cleanupMocks();
    await SpecialPowers.popPrefEnv();
  }
});

add_task(async function test_handoff_uses_one_engine_lookup() {
  // The engine is looked up once, before the last stop check. The real
  // performSearch navigates with it, and the handoff telemetry reuses it, so a
  // lookup that would fail after the navigation cannot undo the handoff.
  await SpecialPowers.pushPrefEnv({
    set: [
      [SEARCH_THE_WEB_FAST_PREF, true],
      [SEARCH_THE_WEB_ANSWERS_PREF, false],
    ],
  });
  const getDefaultEngine = SearchService.getDefault.bind(SearchService);
  const mockSearchManager = new MockSearchManager();
  const sandbox = sinon.createSandbox();
  const handoffStub = sandbox.stub(AIWindow, "openSidebarAndContinue");
  let navigated = false;
  const getDefaultStub = sandbox
    .stub(SearchService, "getDefault")
    .callsFake(() =>
      navigated
        ? Promise.reject(new Error("Search service unavailable"))
        : getDefaultEngine()
    );
  const performSearch = AIWindow.performSearch.bind(AIWindow);
  let lookupsAtPerformSearch = null;
  let lookupsAtNavigation = null;
  const performSearchStub = sandbox
    .stub(AIWindow, "performSearch")
    .callsFake((...args) => {
      lookupsAtPerformSearch = getDefaultStub.callCount;
      return performSearch(...args);
    });
  const loadSearchStub = sandbox
    .stub(SearchUIUtils, "loadSearch")
    .callsFake(async () => {
      lookupsAtNavigation = getDefaultStub.callCount;
      navigated = true;
    });
  sandbox
    .stub(ChatStore, "getMostRecentMessages")
    .resolves([{ content: { body: "test query" } }]);
  // The stubbed loadSearch loads no page, so let the wait for one time out at
  // once rather than after the default 15 seconds.
  const navigationTimeout = RunSearch.NAVIGATION_TIMEOUT_MS;
  RunSearch.NAVIGATION_TIMEOUT_MS = 0;
  try {
    await withToolCallFixture(
      async ({
        mockEngineManager,
        createConversation,
        fetchWithHistory,
        captureChatRequest,
      }) => {
        Services.fog.testResetFOG();
        const conversation = await createConversation();
        const fetchPromise = fetchWithHistory(conversation);
        const firstTurn = await captureChatRequest();
        firstTurn.respond({
          text: "",
          toolCalls: [
            makeToolCall(
              "call_search",
              SEARCH_THE_WEB,
              JSON.stringify({ query: "test query" })
            ),
          ],
        });
        (await mockSearchManager.captureRequest()).respond(
          "upstream exploded",
          { status: 503 }
        );
        const chatEngine = mockEngineManager.engines.get(PURPOSES.CHAT);
        await TestUtils.waitForCondition(
          () => handoffStub.called || chatEngine.runRequests.size,
          "Waiting for the search to hand off or return to the model"
        );
        if (chatEngine.runRequests.size) {
          (await captureChatRequest()).respond("The handoff was undone.");
        }
        await fetchPromise;
        mockEngineManager.assertAllRequestsHandled();

        Assert.ok(loadSearchStub.calledOnce, "The search page starts loading");
        const engine = performSearchStub.firstCall.args[2];
        Assert.ok(
          engine,
          "performSearch is given the engine that was already looked up"
        );
        Assert.equal(
          loadSearchStub.firstCall.args[0].engine,
          engine,
          "The search runs on that engine"
        );
        Assert.equal(
          lookupsAtNavigation,
          lookupsAtPerformSearch,
          "No engine lookup is awaited between the last stop check and the navigation"
        );
        Assert.equal(
          getDefaultStub.callCount,
          lookupsAtNavigation,
          "Nothing looks the engine up again after the navigation"
        );
        Assert.ok(
          handoffStub.calledOnce,
          "The handoff continues in the sidebar"
        );
        Assert.equal(
          conversation._searchExecutedTurn,
          conversation.currentTurnIndex(),
          "The completed handoff keeps the turn marked as handed off"
        );
        const handoffEvents = Glean.smartWindow.searchHandoff.testGetValue();
        Assert.equal(handoffEvents?.length, 1, "The handoff is recorded once");
        Assert.equal(
          handoffEvents?.[0]?.extra.provider,
          engine.name,
          "The handoff is recorded with the engine it navigated with"
        );
        Assert.equal(
          handoffEvents?.[0]?.extra.reason,
          "search_failed",
          "The handoff is recorded as caused by the failed search"
        );
      }
    );
  } finally {
    RunSearch.NAVIGATION_TIMEOUT_MS = navigationTimeout;
    sandbox.restore();
    mockSearchManager.rejectAllRequests();
    mockSearchManager.cleanupMocks();
    await SpecialPowers.popPrefEnv();
  }
});

add_task(async function test_handoff_needs_a_default_engine() {
  // Without a default engine there is nothing to hand off to: the search page
  // is never opened, nothing marks the turn, and the model answers instead. A
  // later lookup would succeed, so a handoff that looked the engine up again
  // after the last stop check would still navigate.
  await SpecialPowers.pushPrefEnv({
    set: [
      [SEARCH_THE_WEB_FAST_PREF, true],
      [SEARCH_THE_WEB_ANSWERS_PREF, false],
    ],
  });
  const getDefaultEngine = SearchService.getDefault.bind(SearchService);
  const navigationTimeout = RunSearch.NAVIGATION_TIMEOUT_MS;
  RunSearch.NAVIGATION_TIMEOUT_MS = 0;
  try {
    for (const lookup of ["rejects", "finds_no_engine"]) {
      info(`Testing a default engine lookup that ${lookup}`);
      const mockSearchManager = new MockSearchManager();
      const sandbox = sinon.createSandbox();
      const handoffStub = sandbox.stub(AIWindow, "openSidebarAndContinue");
      const performSearchSpy = sandbox.spy(AIWindow, "performSearch");
      const loadSearchStub = sandbox
        .stub(SearchUIUtils, "loadSearch")
        .resolves();
      let searchFailed = false;
      let lookupFailed = false;
      sandbox.stub(SearchService, "getDefault").callsFake(() => {
        if (!searchFailed || lookupFailed) {
          return getDefaultEngine();
        }
        lookupFailed = true;
        return lookup === "rejects"
          ? Promise.reject(new Error("Search service unavailable"))
          : Promise.resolve(null);
      });
      sandbox
        .stub(ChatStore, "getMostRecentMessages")
        .resolves([{ content: { body: "test query" } }]);
      try {
        await withToolCallFixture(
          async ({
            mockEngineManager,
            createConversation,
            fetchWithHistory,
            captureChatRequest,
          }) => {
            Services.fog.testResetFOG();
            const conversation = await createConversation();
            const fetchPromise = fetchWithHistory(conversation);
            const firstTurn = await captureChatRequest();
            const toolCalls = [
              makeToolCall(
                "call_search",
                SEARCH_THE_WEB,
                JSON.stringify({ query: "test query" })
              ),
            ];
            firstTurn.respond({ text: "", toolCalls });
            const search = await mockSearchManager.captureRequest();
            searchFailed = true;
            search.respond("upstream exploded", { status: 503 });

            const chatEngine = mockEngineManager.engines.get(PURPOSES.CHAT);
            await TestUtils.waitForCondition(
              () => handoffStub.called || chatEngine.runRequests.size,
              "Waiting for the search to hand off or return to the model"
            );
            Assert.ok(lookupFailed, "The handoff's engine lookup failed");
            Assert.ok(
              chatEngine.runRequests.size,
              "The model gets the turn back"
            );
            if (chatEngine.runRequests.size) {
              const followupTurn = await captureChatRequest();
              const { results } = getToolResults(
                followupTurn.request.args,
                toolCalls
              );
              Assert.ok(
                JSON.parse(results[0].content).error,
                "The model is told the search could not be handed off"
              );
              followupTurn.respond("I could not search the web just now.");
            }
            await fetchPromise;
            mockEngineManager.assertAllRequestsHandled();

            Assert.ok(
              performSearchSpy.notCalled && loadSearchStub.notCalled,
              "The search page is never opened without an engine"
            );
            Assert.ok(
              handoffStub.notCalled,
              "The conversation does not continue in the sidebar"
            );
            Assert.equal(
              Glean.smartWindow.searchHandoff.testGetValue(),
              null,
              "No handoff is recorded"
            );
            Assert.deepEqual(
              getToolCallEvents(),
              [[SEARCH_THE_WEB, "execution_failed"]],
              "The search is recorded as failed"
            );
            Assert.notEqual(
              conversation._searchExecutedTurn,
              conversation.currentTurnIndex(),
              "The turn is not marked as handed off"
            );
          }
        );
      } finally {
        sandbox.restore();
        mockSearchManager.rejectAllRequests();
        mockSearchManager.cleanupMocks();
      }
    }
  } finally {
    RunSearch.NAVIGATION_TIMEOUT_MS = navigationTimeout;
    await SpecialPowers.popPrefEnv();
  }
});

add_task(async function test_stop_during_engine_lookup_leaves_tab_alone() {
  // The user stops the response while the handoff looks up the default
  // engine. The search must not start, so the chat's tab is never selected.
  await SpecialPowers.pushPrefEnv({
    set: [
      [SEARCH_THE_WEB_FAST_PREF, true],
      [SEARCH_THE_WEB_ANSWERS_PREF, false],
    ],
  });
  const getDefaultEngine = SearchService.getDefault.bind(SearchService);
  const mockSearchManager = new MockSearchManager();
  const sandbox = sinon.createSandbox();
  const handoffStub = sandbox.stub(AIWindow, "openSidebarAndContinue");
  const runSearchSpy = sandbox.spy(RunSearch, "runSearch");
  try {
    await withToolCallFixture(
      async ({
        mockEngineManager,
        createConversation,
        fetchWithHistory,
        captureChatRequest,
      }) => {
        Services.fog.testResetFOG();
        const controller = new AbortController();
        let searchFailed = false;
        const getDefaultStub = sandbox
          .stub(SearchService, "getDefault")
          .callsFake(() => {
            if (searchFailed) {
              controller.abort();
            }
            return getDefaultEngine();
          });
        const conversation = await createConversation();
        const otherTab = await BrowserTestUtils.openNewForegroundTab(
          gBrowser,
          "about:blank"
        );
        try {
          const fetchPromise = fetchWithHistory(conversation, {
            signal: controller.signal,
          });
          const firstTurn = await captureChatRequest();
          firstTurn.respond({
            text: "",
            toolCalls: [
              makeToolCall(
                "call_search",
                SEARCH_THE_WEB,
                JSON.stringify({ query: "test query" })
              ),
            ],
          });
          const search = await mockSearchManager.captureRequest();
          searchFailed = true;
          search.respond("upstream exploded", { status: 503 });

          Assert.equal(
            await fetchPromise,
            null,
            "The stopped response returns without a turn result"
          );
          mockEngineManager.assertAllRequestsHandled();
          Assert.ok(
            getDefaultStub.called,
            "The user stopped while the engine was looked up"
          );
          Assert.ok(runSearchSpy.notCalled, "The search never starts");
          Assert.equal(
            gBrowser.selectedTab,
            otherTab,
            "The chat's tab is never selected"
          );
          Assert.ok(
            handoffStub.notCalled,
            "The conversation does not continue in the sidebar"
          );
          Assert.equal(
            Glean.smartWindow.searchHandoff.testGetValue(),
            null,
            "No handoff is recorded"
          );
          Assert.deepEqual(
            getToolCallEvents(),
            [[SEARCH_THE_WEB, "execution_failed"]],
            "The failed search is recorded once"
          );
          Assert.notEqual(
            conversation._searchExecutedTurn,
            conversation.currentTurnIndex(),
            "The stopped handoff does not mark the turn as handed off"
          );
        } finally {
          BrowserTestUtils.removeTab(otherTab);
        }
      }
    );
  } finally {
    sandbox.restore();
    mockSearchManager.rejectAllRequests();
    mockSearchManager.cleanupMocks();
    await SpecialPowers.popPrefEnv();
  }
});

add_task(async function test_stop_before_tab_switch_leaves_tab_alone() {
  // A stopped response must not start the search, so the chat's tab is never
  // selected and a full-page chat never moves to the sidebar. A conversation
  // supplies the first-turn query itself. Without one, the query is read from
  // the stored messages, and the user stops during that read.
  const engine = await SearchService.getDefault();
  for (const withConversation of [true, false]) {
    info(
      `Stopping a search ${withConversation ? "with" : "without"} a conversation`
    );
    const controller = new AbortController();
    const sandbox = sinon.createSandbox();
    const readStub = sandbox
      .stub(ChatStore, "getMostRecentMessages")
      .callsFake(async () => {
        controller.abort();
        return [{ content: { body: "test query" } }];
      });
    const performSearchSpy = sandbox.spy(AIWindow, "performSearch");
    const chatTab = await BrowserTestUtils.openNewForegroundTab(
      gBrowser,
      "about:blank"
    );
    const otherTab = await BrowserTestUtils.openNewForegroundTab(
      gBrowser,
      "about:blank"
    );
    try {
      let conversation;
      if (withConversation) {
        conversation = new ChatConversation({
          pageUrl: new URL("https://example.com"),
          pageMeta: {},
        });
        conversation.addUserMessage("test query");
        controller.abort();
      }
      await Assert.rejects(
        RunSearch.runSearch(
          {},
          chatTab.linkedBrowser.browsingContext,
          conversation,
          engine,
          controller.signal
        ),
        e => e.name === "AbortError",
        "The stopped response cancels the search"
      );
      Assert.equal(
        readStub.callCount,
        withConversation ? 0 : 1,
        "Only a search without a conversation reads the stored messages"
      );
      Assert.equal(
        gBrowser.selectedTab,
        otherTab,
        "The chat's tab is never selected"
      );
      Assert.ok(
        performSearchSpy.notCalled,
        "The search page never starts loading"
      );
    } finally {
      sandbox.restore();
      BrowserTestUtils.removeTab(otherTab);
      BrowserTestUtils.removeTab(chatTab);
    }
  }
});

add_task(async function test_first_turn_search_uses_this_conversation() {
  // A first-turn handoff searches for the user's own message. It comes from
  // this conversation, not from the newest stored message, which can belong
  // to another chat. A conversation without a user message gets an error
  // rather than falling back to the stored messages.
  const engine = await SearchService.getDefault();
  const sandbox = sinon.createSandbox();
  const readStub = sandbox
    .stub(ChatStore, "getMostRecentMessages")
    .resolves([{ content: { body: "A message from another chat" } }]);
  const performSearchStub = sandbox.stub(AIWindow, "performSearch").resolves();
  // The stubbed performSearch loads no page, so let the wait for one time out
  // at once rather than after the default 15 seconds.
  const navigationTimeout = RunSearch.NAVIGATION_TIMEOUT_MS;
  RunSearch.NAVIGATION_TIMEOUT_MS = 0;
  const chatTab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "about:blank"
  );
  try {
    const emptyConversation = new ChatConversation({
      pageUrl: new URL("https://example.com"),
      pageMeta: {},
    });
    Assert.equal(
      await RunSearch.runSearch(
        {},
        chatTab.linkedBrowser.browsingContext,
        emptyConversation,
        engine
      ),
      "Error: no user messages stored to use as the search query.",
      "A conversation without a user message gets an error"
    );

    const conversation = new ChatConversation({
      pageUrl: new URL("https://example.com"),
      pageMeta: {},
    });
    conversation.addUserMessage("An earlier question");
    conversation.addAssistantMessage("text", "An earlier answer");
    conversation.addUserMessage("What is the weather in Paris?");
    await RunSearch.runSearch(
      {},
      chatTab.linkedBrowser.browsingContext,
      conversation,
      engine
    );
    Assert.ok(performSearchStub.calledOnce, "The search starts");
    Assert.equal(
      performSearchStub.firstCall.args[0],
      "What is the weather in Paris?",
      "The search uses this conversation's last user message"
    );
    Assert.ok(
      readStub.notCalled,
      "The stored messages of other chats are not read"
    );
  } finally {
    RunSearch.NAVIGATION_TIMEOUT_MS = navigationTimeout;
    sandbox.restore();
    BrowserTestUtils.removeTab(chatTab);
  }
});

add_task(async function test_abort_during_tool_stops_batch() {
  for (const scenario of ["first", "last", "single"]) {
    info(`Testing abort during ${scenario} tool`);
    await withToolCallFixture(
      async ({
        mockEngineManager,
        createConversation,
        fetchWithHistory,
        captureChatRequest,
      }) => {
        Services.fog.testResetFOG();
        const controller = new AbortController();
        const tabsStub = sinon
          .stub(toolFns, "getOpenTabs")
          .callsFake(async () => {
            controller.abort();
            return [];
          });
        const pageContentSpy = sinon.spy(GetPageContent, "getPageContentText");
        try {
          const conversation = await createConversation();
          const fetchPromise = fetchWithHistory(conversation, {
            signal: controller.signal,
          });
          const firstTurn = await captureChatRequest();
          const toolCalls = [tabsCall()];
          if (scenario === "first") {
            toolCalls.push(pageCall());
          } else if (scenario === "last") {
            toolCalls.unshift(pageCall());
          }
          firstTurn.respond({ text: "", toolCalls });

          let outcome = null;
          const settled = fetchPromise.then(
            () => (outcome = "resolved"),
            () => (outcome = "rejected")
          );
          const chatEngine = mockEngineManager.engines.get(PURPOSES.CHAT);
          await TestUtils.waitForCondition(
            () => outcome || chatEngine.runRequests.size,
            "Waiting for fetchWithHistory to return or request another model turn"
          );
          Assert.ok(
            tabsStub.calledOnce,
            "The tool running when the user aborted finishes"
          );
          Assert.ok(
            scenario === "last"
              ? pageContentSpy.calledOnce
              : pageContentSpy.notCalled,
            "Only tools before the abort run"
          );
          Assert.equal(
            outcome,
            "resolved",
            "fetchWithHistory returns after the abort without another model request"
          );
          mockEngineManager.rejectAllRequests();
          await settled;
          const tabsResult = conversation.messages.find(
            m =>
              m.role === MESSAGE_ROLE.TOOL &&
              m.content.tool_call_id === "call_tabs"
          );
          Assert.deepEqual(
            tabsResult.content.body,
            [],
            "The running tool's result is recorded after cancellation"
          );
          if (scenario === "first") {
            const skipped = conversation.messages.find(
              m =>
                m.role === MESSAGE_ROLE.TOOL &&
                m.content.tool_call_id === "call_page"
            );
            Assert.deepEqual(skipped.content.body, {
              error: "Tool call skipped because the response was stopped.",
            });
          }
          Assert.deepEqual(
            getToolCallEvents(),
            [
              [GET_OPEN_TABS, ""],
              ...(scenario === "single"
                ? []
                : [[GET_PAGE_CONTENT, scenario === "first" ? "aborted" : ""]]),
            ].sort(),
            "Executed and skipped calls each record their own outcome once"
          );
          if (scenario === "last") {
            const pageResult = conversation.messages.find(
              m =>
                m.role === MESSAGE_ROLE.TOOL &&
                m.content.tool_call_id === "call_page"
            );
            Assert.ok(
              pageResult?.content.body.some(text => text.includes(PAGE_TEXT)),
              "The tool before cancellation extracts the real page content"
            );
          }
        } finally {
          tabsStub.restore();
          pageContentSpy.restore();
        }
      }
    );
  }
});
