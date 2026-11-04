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
            tabsResult.some(
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
          tabsResult.some(
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
    for (const scenario of [
      "success",
      "empty_query",
      "no_answer",
      "request_error",
    ]) {
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
              if (scenario === "request_error") {
                answers.reject(new Error("Answer request failed"));
              } else {
                answers.respond({
                  finalOutput: scenario === "no_answer" ? "" : answer,
                });
              }
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
          Array.isArray(toolMessages[0].content.body),
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
