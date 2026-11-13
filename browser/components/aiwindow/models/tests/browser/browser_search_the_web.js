/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { SEARCH_ANSWER_SCHEMA, runSearchTheWeb } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/search/SearchWorkflow.sys.mjs"
);

const {
  GetPageContent,
  GET_PAGE_CONTENT,
  SEARCH_QUERY_ENDPOINT_PREF,
  SEARCH_QUERY_APIKEY_PREF,
  SEARCH_THE_WEB_FAST_PREF,
  SEARCH_THE_WEB_ANSWERS_PREF,
  SEARCH_THE_WEB_ANSWERS_DESCRIPTION,
  SEARCH_THE_WEB,
  searchTheWebToolConfig,
} = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/Tools.sys.mjs"
);

const { MESSAGE_ROLE } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/ui/modules/AIWindowConstants.sys.mjs"
);

const { PURPOSES, MODEL_FEATURES, SERVICE_TYPES } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/Utils.sys.mjs"
);

const { Chat } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/Chat.sys.mjs"
);

const {
  replaceUrlsWithTokens,
  expandUrlTokensInToolParams,
  sanitizeUntrustedContent,
} = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/ChatUtils.sys.mjs"
);

const { MockEngineManager, MockSearchManager } = ChromeUtils.importESModule(
  "resource://testing-common/AIWindowTestUtils.sys.mjs"
);

const { ChatConversation } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/ui/modules/ChatConversation.sys.mjs"
);

const TEST_MODEL = "test-model";
const SEARCH_ENDPOINT = "https://search.example.test/v1/search";
const SEARCH_API_KEY = "mock-search-api-key";

/**
 * Point the search provider at the mocked endpoint and provide a test API key.
 * Both path prefs are always set explicitly so each task states which path it
 * exercises rather than depending on the defaults.
 *
 * @param {boolean} [fast] - Value for SEARCH_THE_WEB_FAST_PREF.
 * @param {boolean} [answers] - Value for SEARCH_THE_WEB_ANSWERS_PREF.
 * @param {string} [endpoint] - Value for SEARCH_QUERY_ENDPOINT_PREF. Pass an
 *   empty string to exercise the provider's missing-configuration path.
 * @returns {Promise<void>}
 */
async function pushSearchPrefs(
  fast = false,
  answers = false,
  endpoint = SEARCH_ENDPOINT
) {
  await SpecialPowers.pushPrefEnv({
    set: [
      [SEARCH_QUERY_ENDPOINT_PREF, endpoint],
      [SEARCH_QUERY_APIKEY_PREF, SEARCH_API_KEY],
      [SEARCH_THE_WEB_FAST_PREF, fast],
      [SEARCH_THE_WEB_ANSWERS_PREF, answers],
    ],
  });
}

/**
 * Serve several real result pages from one HTTP server so the workflow's page
 * reads hit the real extractor.
 *
 * @param {string[]} bodies - HTML body for each result page.
 * @returns {{urls: string[], requestCounts: number[], server: object}}
 *   The page URLs, per-page request counts, and server.
 */
function serveResultPages(bodies) {
  const server = new HttpServer();
  const requestCounts = bodies.map(() => 0);
  const paths = bodies.map((body, index) => {
    const path = `/result-${index}.html`;
    server.registerPathHandler(path, (_request, response) => {
      requestCounts[index]++;
      response.setHeader("Content-Type", "text/html");
      response.write(body);
    });
    return path;
  });
  server.start(-1);
  const { primaryHost, primaryPort } = server.identity;
  const urls = paths.map(
    // eslint-disable-next-line sdl/no-insecure-url
    path => `http://${primaryHost}:${primaryPort}${path}`
  );
  return { urls, requestCounts, server };
}

add_task(async function test_search_the_web_end_to_end() {
  const query = "What is the featured widget's price?";
  const pageContent = "The featured widget is on sale for nine dollars today.";
  const {
    urls: [pageUrl],
    requestCounts,
    server: pageServer,
  } = serveResultPages([
    `<!DOCTYPE html><html><head><meta charset="utf-8" /><title>Widget Store</title></head>
      <body><article><h1>Widget Store</h1><p>${pageContent}</p></article></body></html>`,
  ]);
  const results = [
    {
      title: "Widget Store",
      url: pageUrl,
      text: "A page containing the featured widget's price.",
    },
  ];
  await pushSearchPrefs();

  const mockEngineManager = new MockEngineManager();
  const mockSearchManager = new MockSearchManager();
  const conversation = new ChatConversation({
    pageUrl: new URL("https://example.com"),
    pageMeta: {},
  });

  try {
    const runPromise = runSearchTheWeb({ query }, conversation);

    const search = await mockSearchManager.captureRequest();
    Assert.equal(
      search.request.url,
      SEARCH_ENDPOINT,
      "The provider uses the configured search endpoint"
    );
    Assert.equal(
      search.request.options.method,
      "POST",
      "The provider sends a POST request"
    );
    Assert.deepEqual(
      search.request.options.headers,
      {
        "Content-Type": "application/json",
        Accept: "application/json",
        "service-type": "search",
        Authorization: `Bearer ${SEARCH_API_KEY}`,
      },
      "The provider sends the expected search headers"
    );
    Assert.deepEqual(
      JSON.parse(search.request.options.body),
      { query, max_results: 10 },
      "The provider sends the expected search body"
    );
    search.respond({ results });

    const readTurn = await mockEngineManager.captureRequest({
      purpose: PURPOSES.CHAT,
    });
    Assert.equal(
      readTurn.request.tool_choice,
      "auto",
      "The answer-generation model may request page content"
    );
    Assert.deepEqual(
      readTurn.request.tools.map(tool => tool.function.name),
      [GET_PAGE_CONTENT],
      "Only get_page_content is offered to the answer-generation model"
    );
    const readRequestArgs = JSON.stringify(readTurn.request.args);
    Assert.ok(
      readRequestArgs.includes(query),
      "The model request includes the search query"
    );
    Assert.ok(
      readRequestArgs.includes("Widget Store"),
      "The model request includes the normalized search result"
    );
    Assert.ok(
      readRequestArgs.includes("result_1"),
      "The search result has a stable result id"
    );
    readTurn.respond({
      text: "",
      tokens: null,
      isPrompt: false,
      toolCalls: [
        {
          id: "read_search_result",
          function: {
            name: GET_PAGE_CONTENT,
            arguments: JSON.stringify({ result_ids: ["result_1"] }),
          },
        },
      ],
    });

    const pageContentTurn = await mockEngineManager.captureRequest({
      purpose: PURPOSES.CHAT,
    });
    Assert.ok(
      JSON.stringify(pageContentTurn.request.args).includes(pageContent),
      "The next model request includes content extracted from the result page"
    );
    pageContentTurn.respond("The page contains enough information to answer.");

    const answerTurn = await mockEngineManager.captureRequest({
      purpose: PURPOSES.CHAT,
    });
    Assert.equal(
      answerTurn.request.tool_choice,
      "none",
      "The final generation cannot call tools"
    );
    Assert.deepEqual(
      answerTurn.request.tools,
      [],
      "No tools are offered during final generation"
    );
    const expectedAnswer = {
      answer: "The widget is nine dollars.",
      could_answer: true,
      confidence: 0.9,
    };
    answerTurn.respond(JSON.stringify(expectedAnswer));

    const result = await runPromise;
    conversation.securityProperties.commit();

    Assert.deepEqual(
      result,
      {
        ...expectedAnswer,
        searched_urls: [pageUrl],
        read_urls: [pageUrl],
        requiresSearchHandoff: false,
      },
      "The workflow returns the validated answer and code-tracked URLs"
    );
    Assert.deepEqual(
      conversation.getCitationsSnapshot(),
      [{ url: pageUrl, title: "Widget Store" }],
      "The read page is registered as a citation on the conversation"
    );
    Assert.equal(
      requestCounts[0],
      1,
      "The selected result page is fetched once"
    );
    Assert.deepEqual(
      [...conversation.seenUrls],
      [pageUrl],
      "The search result is recorded as a seen URL"
    );
    Assert.deepEqual(
      [...conversation.serpUrlsForAnonymousFetch],
      [pageUrl],
      "The search result is eligible for anonymous page extraction"
    );
    Assert.ok(
      conversation.securityProperties.privateData,
      "Running a web search marks the conversation as private"
    );
    Assert.ok(
      conversation.securityProperties.untrustedInput,
      "Search results are treated as untrusted input"
    );
    mockEngineManager.assertAllRequestsHandled();
    mockSearchManager.assertAllRequestsHandled();
  } finally {
    await mockEngineManager.rejectAllRequests();
    mockSearchManager.rejectAllRequests();
    mockEngineManager.cleanupMocks();
    mockSearchManager.cleanupMocks();
    await SpecialPowers.popPrefEnv();
    await new Promise(resolve => pageServer.stop(resolve));
  }
});

add_task(function test_exa_result_url_token_round_trip() {
  const conversation = new ChatConversation({});
  const results = [
    {
      title: "One",
      url: "https://round-trip.example.com/alpha",
      snippet: "s1",
    },
    { title: "Two", url: "https://round-trip.example.com/beta", snippet: "s2" },
  ];
  const urls = results.map(result => result.url);
  const formatted = results
    .map(result => `${result.title} — ${result.url}\n${result.snippet}`)
    .join("\n\n");
  conversation.addSeenUrls(urls);
  conversation.addSerpUrlsForAnonymousFetch(urls);

  Assert.equal(
    conversation.serpUrlsForAnonymousFetch.size,
    urls.length,
    "The ledger holds all result URLs"
  );

  replaceUrlsWithTokens(conversation, [{ role: "tool", content: formatted }]);

  for (const url of urls) {
    const token = conversation.urlToToken.get(url);
    Assert.ok(token, `The result URL is tokenized: ${url}`);

    const toolParams = { url_list: [`§url_token: ${token}§`] };
    expandUrlTokensInToolParams(toolParams, conversation.tokenToUrl);
    Assert.deepEqual(
      toolParams.url_list,
      [url],
      "The token expands back to the exact ledger URL"
    );
  }
});

add_task(async function test_search_the_web_reads_result_pages_up_to_limit() {
  // Four results are returned, the model asks to read all of them, and the
  // workflow caps the reads at MAX_PAGES (3), fetching the real served pages.
  const bodies = [0, 1, 2, 3].map(
    index =>
      `<!DOCTYPE html><html><head><meta charset="utf-8" /><title>Page ${index}</title></head>` +
      `<body><article><p>Result page ${index} body content.</p></article></body></html>`
  );
  const { urls, requestCounts, server: pagesServer } = serveResultPages(bodies);
  const results = urls.map((url, index) => ({
    title: `Page ${index}`,
    url,
    snippet: `snippet ${index}`,
  }));
  await pushSearchPrefs();

  const mockEngineManager = new MockEngineManager();
  const mockSearchManager = new MockSearchManager();
  const conversation = new ChatConversation({
    pageUrl: new URL("https://example.com"),
    pageMeta: {},
  });

  try {
    const runPromise = runSearchTheWeb({ query: "pages" }, conversation);
    (await mockSearchManager.captureRequest()).respond({ results });

    // First answer-gen turn: the model requests every result id.
    const readTurn = await mockEngineManager.captureRequest({
      purpose: PURPOSES.CHAT,
    });
    readTurn.respond([
      {
        text: "",
        tokens: null,
        isPrompt: false,
        toolCalls: [
          {
            id: "call_read_all",
            function: {
              name: "get_page_content",
              arguments: JSON.stringify({
                result_ids: ["result_1", "result_2", "result_3", "result_4"],
              }),
            },
          },
        ],
      },
    ]);

    // Second turn: no further reads, so the loop ends.
    (
      await mockEngineManager.captureRequest({ purpose: PURPOSES.CHAT })
    ).respond("");

    // Final turn: structured answer.
    (
      await mockEngineManager.captureRequest({ purpose: PURPOSES.CHAT })
    ).respond(
      JSON.stringify({
        answer: "Answer from pages.",
        could_answer: true,
        confidence: 0.8,
      })
    );

    const result = await runPromise;

    Assert.equal(
      result.searched_urls.length,
      4,
      "All four search results are tracked as searched"
    );
    Assert.equal(
      result.read_urls.length,
      3,
      "The page-read loop is capped at MAX_PAGES (3)"
    );
    for (const readUrl of result.read_urls) {
      Assert.ok(
        urls.includes(readUrl),
        `A read URL is one of the served result pages: ${readUrl}`
      );
    }
    Assert.deepEqual(
      conversation.getCitationsSnapshot().map(citation => citation.url),
      result.read_urls,
      "The citation snapshot matches the read URLs"
    );
    Assert.deepEqual(
      requestCounts,
      [1, 1, 1, 0],
      "Only the first three result pages are fetched"
    );
    mockEngineManager.assertAllRequestsHandled();
    mockSearchManager.assertAllRequestsHandled();
  } finally {
    await mockEngineManager.rejectAllRequests();
    mockSearchManager.rejectAllRequests();
    mockEngineManager.cleanupMocks();
    mockSearchManager.cleanupMocks();
    await SpecialPowers.popPrefEnv();
    await new Promise(resolve => pagesServer.stop(resolve));
  }
});

add_task(async function test_search_the_web_invalid_answer_is_not_answered() {
  // A non-JSON model answer must fail schema validation and default to a
  // not-answered result so the assistant falls back rather than surfacing junk.
  const results = [
    { title: "Page", url: "https://example.com/page", snippet: "s" },
  ];
  await pushSearchPrefs();

  const mockEngineManager = new MockEngineManager();
  const mockSearchManager = new MockSearchManager();
  const conversation = new ChatConversation({
    pageUrl: new URL("https://example.com"),
    pageMeta: {},
  });

  try {
    const runPromise = runSearchTheWeb({ query: "q" }, conversation);
    (await mockSearchManager.captureRequest()).respond({ results });
    (
      await mockEngineManager.captureRequest({ purpose: PURPOSES.CHAT })
    ).respond("");
    (
      await mockEngineManager.captureRequest({ purpose: PURPOSES.CHAT })
    ).respond("this is not valid json");

    const result = await runPromise;
    Assert.equal(
      result.could_answer,
      false,
      "A non-JSON answer is treated as not-answered"
    );
    Assert.equal(
      result.answer,
      "",
      "The answer defaults to empty on malformed output"
    );
    mockEngineManager.assertAllRequestsHandled();
    mockSearchManager.assertAllRequestsHandled();
  } finally {
    await mockEngineManager.rejectAllRequests();
    mockSearchManager.rejectAllRequests();
    mockEngineManager.cleanupMocks();
    mockSearchManager.cleanupMocks();
    await SpecialPowers.popPrefEnv();
  }
});

add_task(async function test_search_the_web_retrieval_error_returns_failure() {
  // A non-2xx from the search endpoint must be caught and returned as a
  // not-answered result with an error, never thrown.
  await pushSearchPrefs();

  const mockSearchManager = new MockSearchManager();
  const conversation = new ChatConversation({
    pageUrl: new URL("https://example.com"),
    pageMeta: {},
  });

  try {
    const runPromise = runSearchTheWeb({ query: "q" }, conversation);
    (await mockSearchManager.captureRequest()).respond("boom", {
      status: 500,
      statusText: "Internal Server Error",
    });
    const result = await runPromise;
    Assert.equal(
      result.could_answer,
      false,
      "A retrieval error yields a not-answered result"
    );
    Assert.deepEqual(
      result.searched_urls,
      [],
      "No URLs are searched when retrieval fails"
    );
    Assert.ok(
      result.error,
      "An error message is returned so the assistant can fall back"
    );
    mockSearchManager.assertAllRequestsHandled();
  } finally {
    mockSearchManager.rejectAllRequests();
    mockSearchManager.cleanupMocks();
    await SpecialPowers.popPrefEnv();
  }
});

add_task(async function test_search_the_web_no_results_returns_failure() {
  // An empty result set short-circuits before answer generation.
  await pushSearchPrefs();

  const mockSearchManager = new MockSearchManager();
  const conversation = new ChatConversation({
    pageUrl: new URL("https://example.com"),
    pageMeta: {},
  });

  try {
    const runPromise = runSearchTheWeb({ query: "q" }, conversation);
    (await mockSearchManager.captureRequest()).respond({ results: [] });
    const result = await runPromise;
    Assert.equal(
      result.could_answer,
      false,
      "No results yields a not-answered result"
    );
    Assert.deepEqual(result.searched_urls, [], "No URLs are searched");
    mockSearchManager.assertAllRequestsHandled();
  } finally {
    mockSearchManager.rejectAllRequests();
    mockSearchManager.cleanupMocks();
    await SpecialPowers.popPrefEnv();
  }
});

add_task(async function test_search_the_web_second_call_escalates_to_handoff() {
  // The first search_the_web call answers in chat and marks the turn as having
  // searched; a second call in the same turn escalates to the handoff (kind
  // HANDOFF) without running another retrieval. Empty results keep the first
  // call fast — the tool still ran, which is what marks the turn.
  await pushSearchPrefs();

  const mockSearchManager = new MockSearchManager();
  const conversation = new ChatConversation({
    pageUrl: new URL("https://example.com"),
    pageMeta: {},
  });

  try {
    const firstPromise = runSearchTheWeb({ query: "weather" }, conversation);
    (await mockSearchManager.captureRequest()).respond({ results: [] });
    const first = await firstPromise;
    Assert.equal(
      first.requiresSearchHandoff,
      false,
      "The first call answers in chat (ANSWER), not a handoff"
    );
    Assert.equal(
      conversation._searchTheWebTurn,
      conversation.currentTurnIndex(),
      "The first call marks the current turn as having searched"
    );

    // No captureRequest() is set up for the second call: had it tried to
    // retrieve again, this await would hang. Its resolving is the proof that the
    // handoff short-circuits before any Exa search.
    const second = await runSearchTheWeb(
      { query: "weather again" },
      conversation
    );
    Assert.equal(
      second.requiresSearchHandoff,
      true,
      "A second call in the same turn escalates to the handoff"
    );

    mockSearchManager.assertAllRequestsHandled();
  } finally {
    mockSearchManager.rejectAllRequests();
    mockSearchManager.cleanupMocks();
    await SpecialPowers.popPrefEnv();
  }
});

add_task(async function test_search_the_web_page_read_timeout_does_not_hang() {
  // A headless page load that never settles must not hang the answer flow:
  // readPage races the fetch against a resolving timeout, so a stuck read
  // yields a fallback and generateAnswer still produces an answer. The read
  // timeout pref is shrunk so the test doesn't wait the 15s default.
  await pushSearchPrefs();
  await SpecialPowers.pushPrefEnv({
    set: [["browser.smartwindow.search.readTimeoutMs", 50]],
  });

  const sb = sinon.createSandbox();
  const mockEngineManager = new MockEngineManager();
  const mockSearchManager = new MockSearchManager();
  const conversation = new ChatConversation({
    pageUrl: new URL("https://example.com"),
    pageMeta: {},
  });

  // Simulate a stuck headless load: getPageContent never settles.
  const hangStub = sb
    .stub(GetPageContent, "getPageContent")
    .callsFake(() => new Promise(() => {}));

  try {
    const runPromise = runSearchTheWeb({ query: "widgets" }, conversation);

    (await mockSearchManager.captureRequest()).respond({
      results: [
        {
          title: "Widget Store",
          url: "https://widgets.example/store",
          text: "A page with widget prices.",
        },
      ],
    });

    // First answer-generation turn: the model asks to read the result page.
    (
      await mockEngineManager.captureRequest({ purpose: PURPOSES.CHAT })
    ).respond({
      text: "",
      tokens: null,
      isPrompt: false,
      toolCalls: [
        {
          id: "read_1",
          function: {
            name: GET_PAGE_CONTENT,
            arguments: JSON.stringify({ result_ids: ["result_1"] }),
          },
        },
      ],
    });

    // The read hangs and times out (~50ms). The fallback text is fed back as
    // the tool result, so the next request's args must contain it — proof the
    // stuck read resolved instead of hanging the flow.
    const afterReadTurn = await mockEngineManager.captureRequest({
      purpose: PURPOSES.CHAT,
    });
    Assert.ok(
      JSON.stringify(afterReadTurn.request.args).includes("Timed out reading"),
      "A stuck page read resolves to the timeout fallback"
    );
    afterReadTurn.respond("The results are enough to answer.");

    // Final structured-answer turn.
    (
      await mockEngineManager.captureRequest({ purpose: PURPOSES.CHAT })
    ).respond(
      JSON.stringify({
        answer: "Widgets vary in price.",
        could_answer: true,
        confidence: 0.6,
      })
    );

    const result = await runPromise;
    Assert.ok(
      result.could_answer,
      "The workflow still produces an answer despite the stuck read"
    );
    Assert.ok(
      hangStub.called,
      "getPageContent was invoked (and abandoned on timeout)"
    );
    mockEngineManager.assertAllRequestsHandled();
    mockSearchManager.assertAllRequestsHandled();
  } finally {
    sb.restore();
    await mockEngineManager.rejectAllRequests();
    mockSearchManager.rejectAllRequests();
    mockEngineManager.cleanupMocks();
    mockSearchManager.cleanupMocks();
    await SpecialPowers.popPrefEnv(); // read-timeout pref
    await SpecialPowers.popPrefEnv(); // search prefs
  }
});

// ---------------------------------------------------------------------------
// Fast path (SEARCH_THE_WEB_FAST_PREF on): Exa snippets are returned directly
// to the main assistant, with no answer generation and no page reads.
// ---------------------------------------------------------------------------

// Mirrors MAX_RESULTS_RETRIEVED / MAX_RESULTS_RETURNED in SearchWorkflow: the
// fast path over-fetches so that dropping unusable results still leaves a full
// set.
const FAST_MAX_REQUESTED = 5;
const FAST_MAX_RETURNED = 3;

// Mirrors MAX_SNIPPET_LENGTH in SearchWorkflow.
const FAST_MAX_SNIPPET_LENGTH = 2000;

// Long enough to clear the workflow's MIN_SNIPPET_LENGTH.
const GOOD_SNIPPET = "A sufficiently long snippet of page text.";

/**
 * Runs the fast path against a mocked search endpoint.
 *
 * @param {object} options
 * @param {string} [options.query]
 * @param {object} options.response - Body the mocked endpoint returns.
 * @param {number} [options.status] - HTTP status the mocked endpoint returns.
 * @param {*} [options.rejectWith] - When set, the request fails below the HTTP
 *   layer with this reason instead of responding.
 * @param {boolean} [options.jsonThrows] - When true, the response arrives with
 *   its status but its body fails to parse.
 * @param {string} [options.mode] - Surface passed through for telemetry.
 * @returns {Promise<{result: object, request: object, conversation: ChatConversation}>}
 */
async function runFastSearchWithMock({
  query = "widgets",
  response,
  status = 200,
  rejectWith = null,
  jsonThrows = false,
  mode,
}) {
  await pushSearchPrefs(true);
  const conversation = new ChatConversation({
    pageUrl: new URL("https://example.com"),
    pageMeta: {},
  });
  const mockSearchManager = new MockSearchManager();
  try {
    const runPromise = runSearchTheWeb(
      { query },
      conversation,
      undefined,
      mode
    );
    const search = await mockSearchManager.captureRequest();
    if (rejectWith) {
      search.reject(rejectWith);
    } else {
      search.respond(response, { status, jsonThrows });
    }
    const result = await runPromise;
    mockSearchManager.assertAllRequestsHandled();
    return { result, request: search.request, conversation };
  } finally {
    mockSearchManager.rejectAllRequests();
    mockSearchManager.cleanupMocks();
    await SpecialPowers.popPrefEnv();
  }
}

add_task(async function test_fast_search_returns_snippets() {
  const query = "What is the featured widget's price?";
  const results = [
    {
      title: "Widget Store",
      url: "https://widgets.example/store",
      text: "The featured widget is on sale for nine dollars today.",
    },
    {
      title: "Widget Reviews",
      url: "https://widgets.example/reviews",
      text: "Reviewers agree the featured widget is a reasonable buy.",
    },
  ];

  // The conversation-level effects (citations, the fetch ledger, the security
  // flags) are asserted in test_fast_search_through_chat, which drives the real
  // caller instead of committing the flags on its own behalf.
  const { result, request } = await runFastSearchWithMock({
    query,
    response: { results },
  });

  Assert.deepEqual(
    JSON.parse(request.options.body),
    { query, max_results: FAST_MAX_REQUESTED },
    "The fast path over-fetches so filtering still leaves a full set"
  );
  Assert.deepEqual(
    result,
    {
      results: results.map(item => ({
        title: sanitizeUntrustedContent(item.title),
        url: item.url,
        snippet: item.text,
      })),
      requiresSearchHandoff: false,
    },
    "The Exa results are returned directly, with no generated answer"
  );
});

add_task(async function test_fast_search_filters_and_caps_results() {
  // Five results come back: one with a non-http URL and one with a stub
  // snippet are dropped, leaving exactly the cap of three.
  const { result, conversation } = await runFastSearchWithMock({
    response: {
      results: [
        {
          title: "First",
          url: "https://widgets.example/first",
          text: GOOD_SNIPPET,
        },
        {
          title: "Not a web URL",
          url: "ftp://widgets.example/file",
          text: GOOD_SNIPPET,
        },
        { title: "Stub", url: "https://widgets.example/stub", text: "Sign in" },
        {
          title: "Second",
          url: "https://widgets.example/second",
          text: GOOD_SNIPPET,
        },
        {
          title: "Third",
          url: "https://widgets.example/third",
          text: GOOD_SNIPPET,
        },
      ],
    },
  });

  Assert.deepEqual(
    result.results.map(item => item.url),
    [
      "https://widgets.example/first",
      "https://widgets.example/second",
      "https://widgets.example/third",
    ],
    "Bad URLs and unusable snippets are dropped, capped at the returned max"
  );
  Assert.equal(
    result.results.length,
    FAST_MAX_RETURNED,
    "No more than the returned max reaches the assistant"
  );
  Assert.deepEqual(
    [...conversation.serpUrlsForAnonymousFetch],
    result.results.map(item => item.url),
    "Only the returned results are fetchable; dropped results are not"
  );
});

add_task(async function test_fast_search_snippet_collapsed_and_truncated() {
  const { result } = await runFastSearchWithMock({
    response: {
      results: [
        {
          title: "Whitespace",
          url: "https://widgets.example/whitespace",
          text: "  Lots\n\n  of\tirregular   whitespace in this snippet.  ",
        },
        {
          title: "Long",
          url: "https://widgets.example/long",
          text: "word ".repeat(1000),
        },
      ],
    },
  });

  Assert.equal(
    result.results[0].snippet,
    "Lots of irregular whitespace in this snippet.",
    "Whitespace is collapsed and the snippet is trimmed"
  );
  // MAX_SNIPPET_LENGTH (2000) plus the single-character ellipsis.
  Assert.equal(
    result.results[1].snippet.length,
    2001,
    "An over-long snippet is truncated to the cap"
  );
  Assert.ok(
    result.results[1].snippet.endsWith("…"),
    "A truncated snippet is marked with an ellipsis"
  );
});

add_task(async function test_fast_search_second_call_escalates_to_handoff() {
  // The handoff escalation is shared by both paths: a second call in the same
  // turn short-circuits before any Exa retrieval.
  await pushSearchPrefs(true);

  const mockSearchManager = new MockSearchManager();
  const conversation = new ChatConversation({
    pageUrl: new URL("https://example.com"),
    pageMeta: {},
  });

  try {
    const firstPromise = runSearchTheWeb({ query: "weather" }, conversation);
    (await mockSearchManager.captureRequest()).respond({ results: [] });
    const first = await firstPromise;
    Assert.equal(
      first.requiresSearchHandoff,
      false,
      "The first call answers in chat, not a handoff"
    );

    // No captureRequest() is set up for the second call: had it tried to
    // retrieve again, this await would hang.
    const second = await runSearchTheWeb(
      { query: "weather again" },
      conversation
    );
    Assert.equal(
      second.requiresSearchHandoff,
      true,
      "A second call in the same turn escalates to the handoff"
    );

    mockSearchManager.assertAllRequestsHandled();
  } finally {
    mockSearchManager.rejectAllRequests();
    mockSearchManager.cleanupMocks();
    await SpecialPowers.popPrefEnv();
  }
});

add_task(async function test_fast_search_through_chat() {
  // Drives the real caller: Chat.fetchWithHistory runs the tool-call loop, so
  // the conversation-level effects happen the way they do in production. In
  // particular the security flags the tool stages are committed by the turn
  // that follows the tool batch, so this test never calls commit() itself.
  const query = "how much are widgets?";
  const results = [
    {
      title: "Widget Store",
      url: "https://widgets.example/store",
      text: "The featured widget is on sale for nine dollars today.",
    },
    {
      title: "Widget Reviews",
      url: "https://widgets.example/reviews",
      text: "Reviewers agree the featured widget is a reasonable buy.",
    },
  ];

  await pushSearchPrefs(true);
  const mockSearchManager = new MockSearchManager();
  const wireRequests = [];

  try {
    await withServer(
      {
        toolCall: { name: SEARCH_THE_WEB, args: JSON.stringify({ query }) },
        followupChunks: ["Widgets cost nine dollars."],
        onRequest(body) {
          wireRequests.push(body);
        },
      },
      async () => {
        const conversation = new ChatConversation({
          pageUrl: new URL("https://example.com"),
          pageMeta: {},
        });
        conversation.engine = await openAIEngine.build({
          model: TEST_MODEL,
          serviceType: SERVICE_TYPES.AI,
          purpose: PURPOSES.CHAT,
          flowId: null,
          feature: MODEL_FEATURES.CHAT,
        });
        conversation.addUserMessage(query, "https://example.com", 0);
        conversation.addAssistantMessage("text", "");

        const fetchPromise = Chat.fetchWithHistory({ conversation });
        await mockSearchManager.respondTo({ response: { results } });
        await fetchPromise;

        const toolMessages = conversation.messages.filter(
          message => message.role === MESSAGE_ROLE.TOOL
        );
        Assert.equal(toolMessages.length, 1, "One search tool result recorded");
        Assert.deepEqual(
          toolMessages[0].content.body,
          {
            results: results.map(item => ({
              title: sanitizeUntrustedContent(item.title),
              url: item.url,
              snippet: item.text,
            })),
            requiresSearchHandoff: false,
          },
          "The snippets reach the conversation as the tool result"
        );

        // No commit() here: the follow-up turn committed the staged flags.
        Assert.ok(
          conversation.securityProperties.privateData,
          "The follow-up turn commits the private-data flag the tool staged"
        );
        Assert.ok(
          conversation.securityProperties.untrustedInput,
          "The follow-up turn commits the untrusted-input flag the tool staged"
        );

        Assert.deepEqual(
          conversation.getCitationsSnapshot(),
          results.map(item => ({ url: item.url, title: item.title })),
          "Every returned result is registered as a citation, with its raw title"
        );
        Assert.deepEqual(
          [...conversation.serpUrlsForAnonymousFetch],
          results.map(item => item.url),
          "The returned results are eligible for anonymous page extraction"
        );

        // The follow-up turn carries the tool result back to the model, which
        // is where the generic URL tokenization has to have taken effect.
        Assert.equal(
          wireRequests.length,
          2,
          "A tool turn and a follow-up turn"
        );
        const wireToolMessage = wireRequests[1].messages.find(
          message => message.role === "tool"
        );
        Assert.ok(
          wireToolMessage,
          "The follow-up turn includes the tool result"
        );
        for (const item of results) {
          const token = conversation.urlToToken.get(item.url);
          Assert.ok(token, `The result URL is tokenized: ${item.url}`);
          Assert.ok(
            wireToolMessage.content.includes(`§url_token: ${token}§`),
            "The model sees the token for the result URL"
          );
          Assert.ok(
            !wireToolMessage.content.includes(item.url),
            "The model does not see the raw result URL"
          );
        }
      }
    );
  } finally {
    mockSearchManager.rejectAllRequests();
    mockSearchManager.cleanupMocks();
    await SpecialPowers.popPrefEnv();
  }
});

// ---------------------------------------------------------------------------
// Fast-path telemetry: the smart_window.search_the_web event, and the
// smart_window.tool_call error a failed search has to populate.
// ---------------------------------------------------------------------------

// Processing drops two of these: one for a non-http URL, one for a stub
// snippet.
const TELEMETRY_RESULTS = [
  { title: "First", url: "https://widgets.example/first", text: GOOD_SNIPPET },
  {
    title: "Not a web URL",
    url: "ftp://widgets.example/file",
    text: GOOD_SNIPPET,
  },
  { title: "Stub", url: "https://widgets.example/stub", text: "Sign in" },
  {
    title: "Second",
    url: "https://widgets.example/second",
    text: GOOD_SNIPPET,
  },
  { title: "Third", url: "https://widgets.example/third", text: GOOD_SNIPPET },
];

/**
 * Reads back the one search_the_web event the flow is expected to record.
 *
 * @returns {object} The event's extra keys. Quantities read back as strings.
 */
function getSearchTheWebExtra() {
  const events = Glean.smartWindow.searchTheWeb.testGetValue();
  Assert.equal(events?.length, 1, "One search_the_web event is recorded");
  return events[0].extra;
}

/**
 * Asserts the stage durations are present and internally consistent.
 *
 * @param {object} extra - Extras from the search_the_web event.
 */
function assertDurationsAreCoherent(extra) {
  const total = Number(extra.total_duration);
  const retrieval = Number(extra.retrieval_duration);
  const processing = Number(extra.processing_duration);
  Assert.ok(
    Number.isInteger(total) && total >= 0,
    `total_duration is a non-negative integer: ${extra.total_duration}`
  );
  Assert.ok(
    Number.isInteger(retrieval) && retrieval >= 0,
    `retrieval_duration is a non-negative integer: ${extra.retrieval_duration}`
  );
  Assert.ok(
    Number.isInteger(processing) && processing >= 0,
    `processing_duration is a non-negative integer: ${extra.processing_duration}`
  );
  // The stages are disjoint segments of the flow, so together they cannot
  // exceed it. Rounding each independently costs at most 1ms per stage.
  Assert.lessOrEqual(
    retrieval + processing,
    total + 2,
    "The stage durations fit inside the total"
  );
}

add_task(async function test_fast_search_telemetry_success() {
  Services.fog.testResetFOG();

  const { result } = await runFastSearchWithMock({
    mode: "sidebar",
    response: { results: TELEMETRY_RESULTS },
  });
  Assert.equal(
    result.results.length,
    FAST_MAX_RETURNED,
    "The search succeeded, so the event describes a success"
  );

  const extra = getSearchTheWebExtra();
  Assert.equal(extra.error, "", "A successful search records no error");
  Assert.equal(extra.http_status, "200", "The success status is recorded");
  Assert.equal(
    extra.results_retrieved,
    String(TELEMETRY_RESULTS.length),
    "Every result the provider returned is counted"
  );
  Assert.equal(
    extra.results_returned,
    String(FAST_MAX_RETURNED),
    "Only the results handed to the assistant are counted as returned"
  );
  Assert.equal(
    extra.snippet_chars_returned,
    String(GOOD_SNIPPET.length * FAST_MAX_RETURNED),
    "The characters of every returned snippet are counted"
  );
  Assert.equal(
    extra.snippet_chars_truncated,
    "0",
    "Nothing was truncated, since every snippet is under the cap"
  );
  Assert.equal(extra.location, "sidebar", "The surface is recorded");
  assertDurationsAreCoherent(extra);
});

add_task(async function test_fast_search_telemetry_snippet_characters() {
  // One snippet under the cap and one over it, so the returned count covers
  // both and the truncated count is exactly the overflow.
  Services.fog.testResetFOG();

  const longSnippet = "word ".repeat(1000);
  const collapsedLength = longSnippet.replace(/\s+/g, " ").trim().length;

  const { result } = await runFastSearchWithMock({
    response: {
      results: [
        {
          title: "Short",
          url: "https://widgets.example/short",
          text: GOOD_SNIPPET,
        },
        {
          title: "Long",
          url: "https://widgets.example/long",
          text: longSnippet,
        },
      ],
    },
  });
  Assert.equal(result.results.length, 2, "Both results reach the assistant");

  const extra = getSearchTheWebExtra();
  Assert.equal(extra.error, "", "The search succeeded");
  Assert.equal(
    extra.snippet_chars_returned,
    // The truncated snippet carries a single appended ellipsis character.
    String(GOOD_SNIPPET.length + FAST_MAX_SNIPPET_LENGTH + 1),
    "Returned characters are the snippets as the assistant received them"
  );
  Assert.equal(
    extra.snippet_chars_truncated,
    String(collapsedLength - FAST_MAX_SNIPPET_LENGTH),
    "Truncated characters are exactly what the cap removed"
  );
});

/**
 * An error named the way the provider's request deadline surfaces, so a
 * timeout can be exercised without waiting one out.
 *
 * @returns {Error}
 */
function abortError() {
  return Object.assign(new Error("aborted"), { name: "AbortError" });
}

// Retrieval failures and empty result sets, each stating the category and
// status the event has to record. The optional count fields are asserted only
// where a case is specifically about them.
const ERROR_CASES = [
  {
    name: "no_results",
    mock: { response: { results: [] } },
    error: "no_results",
    status: "200",
    retrieved: "0",
    returned: "0",
  },
  {
    name: "results_all_dropped",
    mock: {
      response: {
        results: [
          {
            title: "Stub",
            url: "https://widgets.example/stub",
            text: "Sign in",
          },
        ],
      },
    },
    error: "no_results",
    status: "200",
    retrieved: "1",
    returned: "0",
  },
  {
    name: "http_error",
    mock: { response: "upstream exploded", status: 503 },
    error: "http_error",
    status: "503",
    retrieved: "0",
    processingZero: true,
  },
  {
    name: "network_error",
    mock: {
      rejectWith: new TypeError(
        "NetworkError when attempting to fetch resource"
      ),
    },
    error: "network_error",
    status: "0",
  },
  {
    name: "timeout",
    mock: { rejectWith: abortError() },
    error: "timeout",
    status: "0",
  },
  {
    name: "unparseable_body",
    mock: { response: "<html>gateway error</html>", jsonThrows: true },
    error: "retrieval_failed",
    status: "200",
  },
];

add_task(async function test_fast_search_telemetry_error_categories() {
  for (const testCase of ERROR_CASES) {
    info(`error category: ${testCase.name}`);
    Services.fog.testResetFOG();

    const { result } = await runFastSearchWithMock(testCase.mock);
    Assert.ok(result.error, `${testCase.name}: the flow reports a failure`);

    const extra = getSearchTheWebExtra();
    Assert.equal(
      extra.error,
      testCase.error,
      `${testCase.name}: error category`
    );
    Assert.equal(
      extra.http_status,
      testCase.status,
      `${testCase.name}: http_status`
    );
    Assert.equal(
      extra.snippet_chars_returned,
      "0",
      `${testCase.name}: no snippet characters returned`
    );
    Assert.equal(
      extra.snippet_chars_truncated,
      "0",
      `${testCase.name}: no snippet characters truncated`
    );
    if (testCase.retrieved !== undefined) {
      Assert.equal(
        extra.results_retrieved,
        testCase.retrieved,
        `${testCase.name}: results_retrieved`
      );
    }
    if (testCase.returned !== undefined) {
      Assert.equal(
        extra.results_returned,
        testCase.returned,
        `${testCase.name}: results_returned`
      );
    }
    if (testCase.processingZero) {
      Assert.equal(
        extra.processing_duration,
        "0",
        `${testCase.name}: processing never ran, so it is reported as 0`
      );
    }
    assertDurationsAreCoherent(extra);
  }
});

// Failures that happen before the provider issues a request, so no mocked
// response is involved and nothing can be retrieved.
const PREREQUEST_FAILURE_CASES = [
  {
    name: "invalid_query",
    query: "   ",
    endpoint: SEARCH_ENDPOINT,
    error: "invalid_query",
    retrievalZero: true,
  },
  {
    name: "config_error",
    query: "widgets",
    endpoint: "",
    error: "config_error",
  },
];

add_task(async function test_fast_search_telemetry_prerequest_failures() {
  for (const testCase of PREREQUEST_FAILURE_CASES) {
    info(`pre-request failure: ${testCase.name}`);
    Services.fog.testResetFOG();
    await pushSearchPrefs(true, false, testCase.endpoint);

    try {
      const conversation = new ChatConversation({
        pageUrl: new URL("https://example.com"),
        pageMeta: {},
      });
      const result = await runSearchTheWeb(
        { query: testCase.query },
        conversation
      );
      Assert.ok(result.error, `${testCase.name}: the flow reports a failure`);

      const extra = getSearchTheWebExtra();
      Assert.equal(
        extra.error,
        testCase.error,
        `${testCase.name}: error category`
      );
      Assert.equal(
        extra.http_status,
        "0",
        `${testCase.name}: no request was made`
      );
      Assert.equal(
        extra.results_retrieved,
        "0",
        `${testCase.name}: nothing was retrieved`
      );
      if (testCase.retrievalZero) {
        Assert.equal(
          extra.retrieval_duration,
          "0",
          `${testCase.name}: no retrieval was attempted`
        );
      }
    } finally {
      await SpecialPowers.popPrefEnv();
    }
  }
});

add_task(async function test_fast_search_telemetry_skipped_for_handoff() {
  // The handoff retrieves nothing, so reporting it as a search would add
  // zero-duration successes and dilute the latency distribution.
  Services.fog.testResetFOG();
  await pushSearchPrefs(true);

  const mockSearchManager = new MockSearchManager();
  const conversation = new ChatConversation({
    pageUrl: new URL("https://example.com"),
    pageMeta: {},
  });

  try {
    const firstPromise = runSearchTheWeb({ query: "weather" }, conversation);
    (await mockSearchManager.captureRequest()).respond({
      results: TELEMETRY_RESULTS,
    });
    await firstPromise;

    const second = await runSearchTheWeb(
      { query: "weather again" },
      conversation
    );
    Assert.ok(second.requiresSearchHandoff, "The second call escalates");

    const events = Glean.smartWindow.searchTheWeb.testGetValue();
    Assert.equal(
      events?.length,
      1,
      "Only the call that actually searched is recorded"
    );

    mockSearchManager.assertAllRequestsHandled();
  } finally {
    mockSearchManager.rejectAllRequests();
    mockSearchManager.cleanupMocks();
    await SpecialPowers.popPrefEnv();
  }
});

add_task(async function test_grounded_path_records_no_search_the_web_event() {
  // The event covers the snippets-based flow only.
  Services.fog.testResetFOG();
  await pushSearchPrefs(false);

  const mockSearchManager = new MockSearchManager();
  const conversation = new ChatConversation({
    pageUrl: new URL("https://example.com"),
    pageMeta: {},
  });

  try {
    const runPromise = runSearchTheWeb({ query: "widgets" }, conversation);
    (await mockSearchManager.captureRequest()).respond({ results: [] });
    await runPromise;

    Assert.equal(
      Glean.smartWindow.searchTheWeb.testGetValue(),
      null,
      "The grounded path records no search_the_web event"
    );

    mockSearchManager.assertAllRequestsHandled();
  } finally {
    mockSearchManager.rejectAllRequests();
    mockSearchManager.cleanupMocks();
    await SpecialPowers.popPrefEnv();
  }
});

add_task(async function test_fast_search_failure_marks_tool_call_error() {
  // Drives the real caller so tool_call is recorded the way it is in
  // production, where a failed search resolves with an error-bearing result
  // instead of throwing.
  Services.fog.testResetFOG();
  await pushSearchPrefs(true);

  const query = "how much are widgets?";
  const mockSearchManager = new MockSearchManager();

  try {
    await withServer(
      {
        toolCall: { name: SEARCH_THE_WEB, args: JSON.stringify({ query }) },
        followupChunks: ["I could not search just now."],
      },
      async () => {
        const conversation = new ChatConversation({
          pageUrl: new URL("https://example.com"),
          pageMeta: {},
        });
        conversation.engine = await openAIEngine.build({
          model: TEST_MODEL,
          serviceType: SERVICE_TYPES.AI,
          purpose: PURPOSES.CHAT,
          flowId: null,
          feature: MODEL_FEATURES.CHAT,
        });
        conversation.addUserMessage(query, "https://example.com", 0);
        conversation.addAssistantMessage("text", "");

        const fetchPromise = Chat.fetchWithHistory({
          conversation,
          mode: "fullpage",
        });
        await mockSearchManager.respondTo({
          response: "upstream exploded",
          status: 503,
        });
        await fetchPromise;

        const toolCalls = Glean.smartWindow.toolCall.testGetValue();
        Assert.equal(toolCalls?.length, 1, "One tool_call event is recorded");
        Assert.equal(
          toolCalls[0].extra.tool_name,
          SEARCH_THE_WEB,
          "The event is attributed to search_the_web"
        );
        Assert.equal(
          toolCalls[0].extra.error,
          "execution_failed",
          "A failed search is not counted as a successful tool call"
        );

        const extra = getSearchTheWebExtra();
        Assert.equal(
          extra.error,
          "http_error",
          "The precise reason lives on the search_the_web event"
        );
        Assert.equal(
          extra.location,
          "fullpage",
          "Chat passes the surface through to the flow"
        );
      }
    );
  } finally {
    mockSearchManager.rejectAllRequests();
    mockSearchManager.cleanupMocks();
    await SpecialPowers.popPrefEnv();
  }
});

add_task(
  async function test_fast_search_success_leaves_tool_call_error_empty() {
    Services.fog.testResetFOG();
    await pushSearchPrefs(true);

    const query = "how much are widgets?";
    const mockSearchManager = new MockSearchManager();

    try {
      await withServer(
        {
          toolCall: { name: SEARCH_THE_WEB, args: JSON.stringify({ query }) },
          followupChunks: ["Widgets cost nine dollars."],
        },
        async () => {
          const conversation = new ChatConversation({
            pageUrl: new URL("https://example.com"),
            pageMeta: {},
          });
          conversation.engine = await openAIEngine.build({
            model: TEST_MODEL,
            serviceType: SERVICE_TYPES.AI,
            purpose: PURPOSES.CHAT,
            flowId: null,
            feature: MODEL_FEATURES.CHAT,
          });
          conversation.addUserMessage(query, "https://example.com", 0);
          conversation.addAssistantMessage("text", "");

          const fetchPromise = Chat.fetchWithHistory({ conversation });
          await mockSearchManager.respondTo({
            response: { results: TELEMETRY_RESULTS },
          });
          await fetchPromise;

          const toolCalls = Glean.smartWindow.toolCall.testGetValue();
          Assert.equal(toolCalls?.length, 1, "One tool_call event is recorded");
          Assert.equal(
            toolCalls[0].extra.error,
            "",
            "A successful search still reports no tool_call error"
          );
        }
      );
    } finally {
      mockSearchManager.rejectAllRequests();
      mockSearchManager.cleanupMocks();
      await SpecialPowers.popPrefEnv();
    }
  }
);

// ---------------------------------------------------------------------------
// Answers path (SEARCH_THE_WEB_ANSWERS_PREF on, no custom endpoint): one
// non-streaming call to Exa's answer service, whose reply is delivered to the
// user as the assistant turn rather than handed back to the model.
// ---------------------------------------------------------------------------

// The model name that routes an MLPA request to the answer service, mirroring
// ANSWERS_MODEL in SearchWorkflow.
const ANSWERS_MODEL = "exa";
const ANSWERS_ENGINE_KEY = "unknown";
const MODEL_CHOICE_PREF = "browser.smartwindow.firstrun.modelChoice";
const CUSTOM_MODEL_CHOICE_ID = "0";

/**
 * A conversation whose engine reports the given endpoint kind, which is what
 * gates the answers path.
 *
 * @param {boolean} [isDefaultEndpoint]
 * @returns {ChatConversation}
 */
function makeAnswersConversation(isDefaultEndpoint = true) {
  const conversation = new ChatConversation({
    pageUrl: new URL("https://example.com"),
    pageMeta: {},
  });
  conversation.engine = { isDefaultEndpoint };
  return conversation;
}

add_task(async function test_answers_search_returns_answer_and_citations() {
  const query = "kid friendly things to do in Dallas";
  await pushSearchPrefs(false, true);

  const mockEngineManager = new MockEngineManager();
  const conversation = makeAnswersConversation();

  try {
    const runPromise = runSearchTheWeb({ query }, conversation);

    const answers = await mockEngineManager.captureRequest({
      purpose: ANSWERS_ENGINE_KEY,
    });

    Assert.deepEqual(
      answers.request.args.at(-1),
      { role: "user", content: query },
      "The query is sent as an ordinary chat-completions user message"
    );
    Assert.equal(
      answers.request.args[0].role,
      "system",
      "A system message asks the service to leave citations out of the prose"
    );
    Assert.ok(
      !answers.request.streamOptions?.enabled,
      "The request is non-streaming: only a complete response carries citations"
    );

    answers.respond({
      finalOutput: "Perot Museum and the Dallas Zoo are both good.",
      providerSpecificFields: {
        citations: [
          { url: "https://perot.example/", title: "Perot Museum" },
          { url: "https://zoo.example/", title: "Dallas Zoo" },
          // Dropped: not a web URL, so it is never offered as a source.
          { url: "javascript:alert(1)", title: "Not a web URL" },
        ],
      },
    });

    const result = await runPromise;

    const [engineOptions] = openAIEngine._createEngine.args.find(
      ([options]) => options.modelId === ANSWERS_MODEL
    );
    Assert.equal(
      engineOptions.serviceType,
      SERVICE_TYPES.SW_ANSWER,
      "The engine is built with the service type that routes to /answers"
    );
    Assert.equal(
      engineOptions.purpose,
      undefined,
      "No purpose is sent: the answer service rejects the chat ones"
    );

    Assert.deepEqual(
      result.read_urls,
      ["https://perot.example/", "https://zoo.example/"],
      "The cited URLs are reported back, with non-web URLs dropped"
    );
    Assert.equal(
      result.requiresSearchHandoff,
      false,
      "A successful answer is not a handoff"
    );
    Assert.strictEqual(
      result.directAnswer,
      true,
      "The result is marked as a reply Exa wrote, for the action log"
    );

    let delivered = "";
    for await (const chunk of result.directAnswerStream) {
      delivered += chunk.text;
    }
    Assert.equal(
      delivered,
      "Perot Museum and the Dallas Zoo are both good.",
      "The service's prose is delivered whole, as written"
    );

    Assert.deepEqual(
      conversation.getCitationsSnapshot(),
      [
        { url: "https://perot.example/", title: "Perot Museum" },
        { url: "https://zoo.example/", title: "Dallas Zoo" },
      ],
      "Each cited source is registered for the reply's source chips"
    );
    Assert.deepEqual(
      [...conversation.serpUrlsForAnonymousFetch],
      ["https://perot.example/", "https://zoo.example/"],
      "The cited pages are eligible for a follow-up anonymous page read"
    );

    mockEngineManager.assertAllRequestsHandled();
  } finally {
    await mockEngineManager.rejectAllRequests();
    mockEngineManager.cleanupMocks();
    await SpecialPowers.popPrefEnv();
  }
});

add_task(async function test_answers_pref_ignored_on_custom_endpoint() {
  // The answers path pins a model and service type of Mozilla's and hands the
  // service's reply straight to the user, so a conversation on a custom
  // endpoint falls back to the fast path even with the pref on. The global
  // model choice is left on a Mozilla model: what counts is the engine this
  // conversation is using, e.g. a per-tab override to the custom model.
  await pushSearchPrefs(true, true);

  const mockEngineManager = new MockEngineManager();
  const mockSearchManager = new MockSearchManager();
  const conversation = makeAnswersConversation(false);

  try {
    const runPromise = runSearchTheWeb({ query: "widgets" }, conversation);

    // The answers path never touches the search endpoint, so reaching this at
    // all is the assertion: had it run, this await would hang.
    (await mockSearchManager.captureRequest()).respond({
      results: [
        {
          title: "Widget Store",
          url: "https://widgets.example/store",
          text: GOOD_SNIPPET,
        },
      ],
    });

    const result = await runPromise;

    Assert.deepEqual(
      result.results.map(item => item.url),
      ["https://widgets.example/store"],
      "A custom endpoint gets the fast path's snippets, not a written answer"
    );
    Assert.ok(
      !("directAnswerStream" in result),
      "No reply is delivered on the user's behalf"
    );
    Assert.ok(
      !("directAnswer" in result),
      "The action log does not claim Exa wrote the reply"
    );
    Assert.ok(
      !openAIEngine._createEngine.called,
      "No answer-service engine is built for a custom endpoint"
    );

    mockSearchManager.assertAllRequestsHandled();
  } finally {
    await mockEngineManager.rejectAllRequests();
    mockSearchManager.rejectAllRequests();
    mockEngineManager.cleanupMocks();
    mockSearchManager.cleanupMocks();
    await SpecialPowers.popPrefEnv();
  }
});

add_task(async function test_answers_runs_when_tab_overrides_custom_choice() {
  // The global model choice is the custom model, but this conversation's engine
  // was built from a per-tab override to a Mozilla model, so the answers path
  // still runs and the model is offered the matching tool config.
  await pushSearchPrefs(true, true);
  await SpecialPowers.pushPrefEnv({
    set: [[MODEL_CHOICE_PREF, CUSTOM_MODEL_CHOICE_ID]],
  });

  const mockEngineManager = new MockEngineManager();
  const conversation = makeAnswersConversation(true);

  try {
    Assert.equal(
      searchTheWebToolConfig(conversation)?.function.description,
      SEARCH_THE_WEB_ANSWERS_DESCRIPTION,
      "The model is offered the answers tool config"
    );

    const runPromise = runSearchTheWeb({ query: "widgets" }, conversation);

    // Reaching this at all is the assertion: had the fast path run instead,
    // no answer-service request would arrive and this await would hang.
    (
      await mockEngineManager.captureRequest({ purpose: ANSWERS_ENGINE_KEY })
    ).respond({ finalOutput: "Widgets are great." });

    const result = await runPromise;
    Assert.ok(
      result.directAnswerStream,
      "The answer is delivered as the reply"
    );

    mockEngineManager.assertAllRequestsHandled();
  } finally {
    await mockEngineManager.rejectAllRequests();
    mockEngineManager.cleanupMocks();
    await SpecialPowers.popPrefEnv(); // model choice
    await SpecialPowers.popPrefEnv(); // search prefs
  }
});

add_task(
  async function test_answers_pref_ignored_on_overridden_endpoint_pref() {
    // A Mozilla model choice resolves to browser.smartwindow.endpoint, so when
    // that pref has been pointed away from prod MLPA in about:config the engine
    // is not on the default endpoint and the answers path is withheld. Real
    // engines are built here, since that resolution is what is under test.
    await pushSearchPrefs(true, true);

    const mockEngineManager = new MockEngineManager();
    const mockSearchManager = new MockSearchManager();
    const buildChatEngine = () =>
      openAIEngine.build({
        model: TEST_MODEL,
        serviceType: SERVICE_TYPES.AI,
        purpose: PURPOSES.CHAT,
        flowId: null,
        feature: MODEL_FEATURES.CHAT,
      });
    let endpointPushed = false;

    try {
      const defaultEngine = await buildChatEngine();
      Assert.ok(
        defaultEngine.isDefaultEndpoint,
        "An engine on the default endpoint pref is on the default endpoint"
      );

      await SpecialPowers.pushPrefEnv({
        set: [["browser.smartwindow.endpoint", "http://localhost:0/v1"]],
      });
      endpointPushed = true;

      const conversation = new ChatConversation({
        pageUrl: new URL("https://example.com"),
        pageMeta: {},
      });
      conversation.engine = await buildChatEngine();

      Assert.ok(
        !conversation.engine.isDefaultEndpoint,
        "An engine on an overridden endpoint pref is not on the default endpoint"
      );
      Assert.notEqual(
        searchTheWebToolConfig(conversation)?.function.description,
        SEARCH_THE_WEB_ANSWERS_DESCRIPTION,
        "The model is not offered the answers tool config"
      );

      const runPromise = runSearchTheWeb({ query: "widgets" }, conversation);

      // The answers path never touches the search endpoint, so reaching this at
      // all is the assertion: had it run, this await would hang.
      (await mockSearchManager.captureRequest()).respond({
        results: [
          {
            title: "Widget Store",
            url: "https://widgets.example/store",
            text: GOOD_SNIPPET,
          },
        ],
      });

      const result = await runPromise;
      Assert.ok(
        !("directAnswerStream" in result),
        "No reply is delivered on the user's behalf"
      );

      mockSearchManager.assertAllRequestsHandled();
    } finally {
      await mockEngineManager.rejectAllRequests();
      mockSearchManager.rejectAllRequests();
      mockEngineManager.cleanupMocks();
      mockSearchManager.cleanupMocks();
      if (endpointPushed) {
        await SpecialPowers.popPrefEnv(); // endpoint
      }
      await SpecialPowers.popPrefEnv(); // search prefs
    }
  }
);

add_task(async function test_answers_failure_returns_turn_then_escalates() {
  await pushSearchPrefs(false, true);

  const mockEngineManager = new MockEngineManager();
  const conversation = makeAnswersConversation();

  try {
    const runPromise = runSearchTheWeb({ query: "weather" }, conversation);

    // A blank answer is a failure: there is nothing to show the user.
    (
      await mockEngineManager.captureRequest({ purpose: ANSWERS_ENGINE_KEY })
    ).respond({ finalOutput: "   " });

    const first = await runPromise;
    Assert.ok(
      !first.directAnswerStream,
      "A failed request delivers no reply to the user"
    );
    Assert.ok(
      first.error,
      "The failure reaches the model as the tool result instead"
    );
    Assert.equal(
      first.requiresSearchHandoff,
      false,
      "The model regains the turn rather than being handed off silently"
    );
    Assert.deepEqual(
      conversation.getCitationsSnapshot(),
      [],
      "A failed request leaves no citations behind"
    );

    // No captureRequest() is set up for the second call: had it tried the
    // answer service again, this await would hang.
    const second = await runSearchTheWeb(
      { query: "weather again" },
      conversation
    );
    Assert.equal(
      second.requiresSearchHandoff,
      true,
      "The model's follow-up call escalates to the search handoff"
    );

    mockEngineManager.assertAllRequestsHandled();
  } finally {
    await mockEngineManager.rejectAllRequests();
    mockEngineManager.cleanupMocks();
    await SpecialPowers.popPrefEnv();
  }
});
