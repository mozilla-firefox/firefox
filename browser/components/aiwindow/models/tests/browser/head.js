/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { HttpServer } = ChromeUtils.importESModule(
  "resource://testing-common/httpd.sys.mjs"
);

const { sinon } = ChromeUtils.importESModule(
  "resource://testing-common/Sinon.sys.mjs"
);

const { AuthProvider } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/AuthProvider.sys.mjs"
);
const { openAIEngine } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/Utils.sys.mjs"
);

const { MockRegistrar } = ChromeUtils.importESModule(
  "resource://testing-common/MockRegistrar.sys.mjs"
);

const { PAGE_SETTLE_DELAY_MS, setPageSettleDelayForTesting } =
  ChromeUtils.importESModule(
    "moz-src:///browser/components/aiwindow/models/agents/Monitor.sys.mjs"
  );

setPageSettleDelayForTesting(0);
registerCleanupFunction(() =>
  setPageSettleDelayForTesting(PAGE_SETTLE_DELAY_MS)
);

/**
 * Start an HTTP server that serves HTML content.
 *
 * @param {string} html - The HTML content to serve
 * @returns {object} An object containing:
 *   - url: The URL where the content is served
 *   - serverClosed: Promise that resolves when the server stops
 */
function serveHTML(html) {
  const server = new HttpServer();

  server.registerPathHandler("/test-page.html", (_request, response) => {
    response.setHeader("Content-Type", "text/html");
    response.write(html);
  });

  server.start(-1);

  const { primaryHost, primaryPort } = server.identity;
  // eslint-disable-next-line sdl/no-insecure-url
  const url = `http://${primaryHost}:${primaryPort}/test-page.html`;

  return {
    url,
    server,
  };
}

/**
 * Set up a test for the get_page_content tool call by serving HTML and loading it.
 *
 * @param {string} html - The HTML content to serve and test
 * @returns {Promise<object>} An object containing:
 *   - tab: The opened browser tab
 *   - url: The URL of the loaded page
 *   - GetPageContent: The GetPageContent class
 *   - cleanup: Function to clean up the test
 */
async function setupGetPageContentTests(html) {
  const { GetPageContent } = ChromeUtils.importESModule(
    "moz-src:///browser/components/aiwindow/models/Tools.sys.mjs"
  );

  const { url, server } = serveHTML(html);

  const tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    url,
    true // waitForLoad
  );

  const url_list = [url];

  return {
    url_list,
    GetPageContent,
    async cleanup() {
      info("Cleaning up test");
      BrowserTestUtils.removeTab(tab);
      await new Promise(resolve => server.stop(resolve));
    },
  };
}

function readRequestBody(request) {
  const stream = request.bodyInputStream;
  const available = stream.available();
  return NetUtil.readInputStreamToString(stream, available, {
    charset: "UTF-8",
  });
}

// eslint-disable-next-line jsdoc/require-param
/**
 * @deprecated - Please use MockEngineManager in AIWindowTestUtils.sys.mjs unless
 * a test is explicitly needing to test the network layer of the OpenAI chat protocol.
 */
function startMockOpenAI({
  streamChunks = ["Hello from mock."],
  toolCall = null,
  followupChunks = ["Tool complete."],
  onRequest,
} = {}) {
  const server = new HttpServer();

  server.registerPathHandler("/v1/chat/completions", (request, response) => {
    let bodyText = "";
    if (request.method === "POST") {
      try {
        bodyText = readRequestBody(request);
      } catch (_) {}
    }

    let body;
    try {
      body = JSON.parse(bodyText || "{}");
    } catch (_) {
      body = {};
    }

    onRequest?.(body);

    const wantsStream = !!body.stream;
    const tools = Array.isArray(body.tools) ? body.tools : [];
    const askedForTools = tools.length;
    const messages = Array.isArray(body.messages) ? body.messages : [];
    const hasToolResult = messages.some(m => m && m.role === "tool");

    const startSSE = () => {
      response.setStatusLine(request.httpVersion, 200, "OK");
      response.setHeader(
        "Content-Type",
        "text/event-stream; charset=utf-8",
        false
      );
      response.setHeader("Cache-Control", "no-cache", false);
      response.setHeader("Access-Control-Allow-Origin", "*", false);
      response.processAsync();
    };

    const sendSSE = obj => {
      response.write(`data: ${JSON.stringify(obj)}\n\n`);
    };

    if (wantsStream && toolCall && askedForTools && !hasToolResult) {
      startSSE();

      const timestamp = Math.floor(Date.now() / 1000);
      sendSSE({
        id: "chatcmpl-aiwindow-stream-tool-1",
        object: "chat.completion.chunk",
        created: timestamp,
        model: "aiwindow-mock",
        choices: [
          {
            index: 0,
            delta: {
              content: "",
              tool_calls: [
                {
                  index: 0,
                  id: "call_1",
                  type: "function",
                  function: {
                    name: toolCall.name,
                    arguments: toolCall.args ?? "{}",
                  },
                },
              ],
            },
            finish_reason: null,
          },
        ],
      });

      sendSSE({
        id: "chatcmpl-aiwindow-stream-tool-2",
        object: "chat.completion.chunk",
        created: timestamp,
        model: "aiwindow-mock",
        choices: [{ index: 0, delta: {}, finish_reason: "tool_calls" }],
        usage: { prompt_tokens: 10, completion_tokens: 5, total_tokens: 15 },
      });

      response.write("data: [DONE]\n\n");
      response.finish();
      return;
    }

    if (wantsStream && toolCall && askedForTools && hasToolResult) {
      startSSE();

      const timestamp = Math.floor(Date.now() / 1000);
      followupChunks.forEach((chunk, index) => {
        const isLast = index === followupChunks.length - 1;
        const event = {
          id: `chatcmpl-aiwindow-stream-tool-followup-${index}`,
          object: "chat.completion.chunk",
          created: timestamp,
          model: "aiwindow-mock",
          choices: [
            {
              index: 0,
              delta: { content: chunk },
              finish_reason: isLast ? "stop" : null,
            },
          ],
        };
        if (isLast) {
          event.usage = {
            prompt_tokens: 10,
            completion_tokens: 5,
            total_tokens: 15,
          };
        }
        sendSSE(event);
      });

      response.write("data: [DONE]\n\n");
      response.finish();
      return;
    }

    if (wantsStream) {
      startSSE();

      const timestamp = Math.floor(Date.now() / 1000);
      streamChunks.forEach((chunk, index) => {
        const isLast = index === streamChunks.length - 1;
        const event = {
          id: `chatcmpl-aiwindow-stream-${index}`,
          object: "chat.completion.chunk",
          created: timestamp,
          model: "aiwindow-mock",
          choices: [
            {
              index: 0,
              delta: { content: chunk },
              finish_reason: isLast ? "stop" : null,
            },
          ],
        };
        if (isLast) {
          event.usage = {
            prompt_tokens: 10,
            completion_tokens: 5,
            total_tokens: 15,
          };
        }
        sendSSE(event);
      });

      response.write("data: [DONE]\n\n");
      response.finish();
      return;
    }

    const payload = {
      id: "chatcmpl-aiwindow",
      object: "chat.completion",
      created: Math.floor(Date.now() / 1000),
      model: "aiwindow-mock",
      choices: [
        {
          index: 0,
          message: {
            role: "assistant",
            content: streamChunks.join(""),
          },
          finish_reason: "stop",
        },
      ],
      usage: { prompt_tokens: 0, completion_tokens: 0, total_tokens: 0 },
    };

    response.setStatusLine(request.httpVersion, 200, "OK");
    response.setHeader(
      "Content-Type",
      "application/json; charset=utf-8",
      false
    );
    response.setHeader("Access-Control-Allow-Origin", "*", false);
    response.write(JSON.stringify(payload));
  });

  server.start(-1);
  return { server, port: server.identity.primaryPort };
}

function stopMockOpenAI(server) {
  return new Promise(resolve => server.stop(resolve));
}

async function withServer(serverOptions, task) {
  const { server, port } = startMockOpenAI(serverOptions);
  await SpecialPowers.pushPrefEnv({
    set: [["browser.smartwindow.endpoint", `http://localhost:${port}/v1`]],
  });

  const getTokenStub = sinon
    .stub(AuthProvider, "getToken")
    .resolves("mock-fxa-token");

  try {
    await task({ port });
  } finally {
    getTokenStub.restore();
    await SpecialPowers.popPrefEnv();
    await stopMockOpenAI(server);
  }
}

function startTitleGenerationServer(title) {
  return startMockOpenAI({ streamChunks: [title] });
}

/**
 * Replaces the platform alerts service with a mock that records shown alerts
 * and their observers, so tests can simulate action-button clicks.
 *
 * @returns {{ alerts: object[], observers: object[], cleanup: () => void }}
 */
function mockAlertsService() {
  const alerts = [];
  const observers = [];
  const service = {
    QueryInterface: ChromeUtils.generateQI(["nsIAlertsService"]),
    showAlert(alert, observer) {
      alerts.push(alert);
      observers.push(observer);
    },
    closeAlert() {},
  };
  const cid = MockRegistrar.register("@mozilla.org/alerts-service;1", service);
  return {
    alerts,
    observers,
    // Drops the creation notification so tests can count run notifications
    reset: () => {
      alerts.length = 0;
      observers.length = 0;
    },
    cleanup: () => MockRegistrar.unregister(cid),
  };
}

/**
 * Have the chat model call a tool, the way production runs one: start a chat
 * turn on `conversation` and answer it with the tool call. Answer any requests
 * the tool makes to other engines before awaiting the result.
 *
 * @param {MockEngineManager} mockEngine
 * @param {ChatConversation} conversation
 * @param {object} options
 * @param {string} options.toolName
 * @param {object} options.toolParams - The arguments the model calls it with.
 * @returns {Promise<{body: any, toolUIData: object}>} Once the turn is over,
 *   the tool's result and the UI data it attached to the assistant message, if
 *   any.
 */
async function callTool(mockEngine, conversation, { toolName, toolParams }) {
  const { Chat } = ChromeUtils.importESModule(
    "moz-src:///browser/components/aiwindow/models/Chat.sys.mjs"
  );
  const { PURPOSES } = ChromeUtils.importESModule(
    "moz-src:///browser/components/aiwindow/models/Utils.sys.mjs"
  );
  const { MESSAGE_ROLE } = ChromeUtils.importESModule(
    "moz-src:///browser/components/aiwindow/ui/modules/AIWindowConstants.sys.mjs"
  );

  conversation.addUserMessage(`Call ${toolName}`);
  conversation.addAssistantMessage("text", "");
  const turn = Chat.fetchWithHistory({ conversation });
  const id = await mockEngine.respondToWithToolCall({ toolName, toolParams });

  // Most tools hand their result back to the model for a follow-up turn, which
  // ends the turn once answered.
  const chatEngine = mockEngine.engines.get(PURPOSES.CHAT);
  await Promise.race([turn, chatEngine.waitForRunRequest()]);
  if (chatEngine.runRequests.size) {
    await mockEngine.respondTo({ purpose: PURPOSES.CHAT, response: "" });
  }
  await turn;

  const message = conversation.messages.find(
    m => m.role === MESSAGE_ROLE.TOOL && m.content.tool_call_id === id
  );
  Assert.ok(message, `The ${toolName} call left its result in the chat`);
  return {
    body: message.content.body,
    toolUIData: conversation.messages.findLast(
      m => m.toolUIData?.toolCallId === id
    )?.toolUIData,
  };
}

/**
 * Store a user chat message at an exact `created_date` so tasks can place
 * messages on either side of the watermark boundary. Goes through the real
 * ChatStore, so the memories chat source reads it back over real SQL.
 *
 * @param {number} createdDate  Message creation time in ms since epoch
 * @param {string} body         Message body
 */
async function addChatMessage(createdDate, body) {
  const { ChatStore, ChatConversation, ChatMessage, MESSAGE_ROLE } =
    ChromeUtils.importESModule(
      "moz-src:///browser/components/aiwindow/ui/modules/ChatStore.sys.mjs"
    );
  await ChatStore.updateConversation(
    new ChatConversation({
      title: body,
      description: "",
      pageUrl: new URL("https://example.com/chat"),
      pageMeta: {},
      messages: [
        new ChatMessage({
          createdDate,
          ordinal: 0,
          role: MESSAGE_ROLE.USER,
          content: { body },
          pageUrl: new URL("https://example.com/chat"),
        }),
      ],
    })
  );
}

/**
 * Store a page visit at an exact visit date so tasks can place visits on either
 * side of the watermark boundary.
 *
 * `getRecentHistory` reads page visits from moz_places_metadata rather than
 * moz_historyvisits, so the metadata row has to be written by hand. Its
 * `total_view_time` must exceed the source's own minimum viewtime filter for the
 * visit to be returned at all.
 *
 * @param {string} url            URL to record a visit for
 * @param {string} title          Page title, which cannot be null or the source
 *                                filters the visit out
 * @param {number} visitDateMs    Visit time in ms since epoch
 */
async function writePageVisitToMozPlaces(url, title, visitDateMs) {
  await PlacesUtils.history.insertMany([
    { url, title, visits: [{ date: new Date(visitDateMs) }] },
  ]);

  await PlacesUtils.withConnectionWrapper(
    "memories-test-metadata",
    async db => {
      const rows = await db.execute(
        "SELECT id FROM moz_places WHERE url_hash = hash(:url) AND url = :url",
        { url }
      );
      await db.execute(
        `INSERT INTO moz_places_metadata
           (place_id, created_at, updated_at, total_view_time,
            typing_time, key_presses, scrolling_time, scrolling_distance, document_type)
         VALUES
           (:place_id, :created_at, :created_at, 30000, 0, 0, 0, 0, 0)`,
        {
          place_id: rows[0].getResultByName("id"),
          created_at: visitDateMs,
        }
      );
    }
  );
}

/**
 * Record a page visit and wait for the resulting "page-visited" notification to
 * have been dispatched, arming the memories scheduler's browsing trigger.
 *
 * @param {string} url  The URL to record a visit for
 */
async function visitPage(url) {
  const { PlacesTestUtils } = ChromeUtils.importESModule(
    "resource://testing-common/PlacesTestUtils.sys.mjs"
  );
  const visited = new Promise(resolve => {
    const listener = () => {
      PlacesUtils.observers.removeListener(["page-visited"], listener);
      resolve();
    };
    PlacesUtils.observers.addListener(["page-visited"], listener);
  });
  await PlacesTestUtils.addVisits(url);
  await visited;
}

/**
 * Return all events recorded for a generation run metric
 *
 * @param {object} metric  Glean event metric, e.g.
 *                         Glean.smartWindow.memoriesGenerationRun
 * @returns {Array<object>} The events recorded
 */
function getGenerationRunEvents(metric) {
  return metric.testGetValue() ?? [];
}

/**
 * Assert that exactly one expected Glean run was recorded.
 *
 * Runs called directly, without the scheduler, record no event. For those,
 * pass the stats object the run filled in, which is checked instead.
 *
 * @param {object} metric    Glean event metric, e.g.
 *                           Glean.smartWindow.memoriesGenerationRun
 * @param {object} expected  Expected extra values the Glean run should include
 * @param {object} [options]
 * @param {object} [options.stats]  Stats from a direct call to check instead
 *                                  of the recorded event
 */
function assertGenerationRunEvent(metric, expected, { stats } = {}) {
  const toExtras = values =>
    Object.fromEntries(
      Object.entries(values).map(([key, value]) => [key, String(value)])
    );
  let actual;
  if (stats) {
    Assert.deepEqual(
      getGenerationRunEvents(metric),
      [],
      "A direct call records no run."
    );
    actual = toExtras(stats);
  } else {
    const events = getGenerationRunEvents(metric);
    Assert.equal(events.length, 1, "One run should be recorded.");
    actual = events[0].extra;
  }
  Assert.deepEqual(
    actual,
    toExtras(expected),
    "The run should be recorded with the expected extras."
  );
}
