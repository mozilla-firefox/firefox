/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Tests for all expected requests when an iframe is loading a subdocument.
 */

const TOP_FILE_NAME = "html_frame-test-page.html";
const SUB_FILE_NAME = "html_frame-subdocument.html";
const TOP_URL = EXAMPLE_URL + TOP_FILE_NAME;
const SUB_URL = EXAMPLE_URL + SUB_FILE_NAME;

const EXPECTED_REQUESTS_TOP = [
  {
    method: "GET",
    url: TOP_URL,
    causeType: "document",
    causeUri: null,
    stack: true, // Expects that request has an empty stack trace
  },
  {
    method: "GET",
    url: EXAMPLE_URL + "stylesheet_request",
    causeType: "stylesheet",
    causeUri: TOP_URL,
    stack: false, // Expects that request has no stack trace
  },
  {
    method: "GET",
    url: EXAMPLE_URL + "img_request",
    causeType: "img",
    causeUri: TOP_URL,
    stack: false,
  },
  {
    method: "GET",
    url: EXAMPLE_URL + "xhr_request",
    causeType: "xhr",
    causeUri: TOP_URL,
    // Expects that request has a stack trace with items
    stack: [
      {
        fn: "performXhrRequest",
        file: TOP_FILE_NAME,
        line: 25,
        asyncCause: null,
      },
    ],
  },
  {
    method: "GET",
    url: EXAMPLE_URL + "fetch_request",
    causeType: "fetch",
    causeUri: TOP_URL,
    stack: [
      {
        fn: "performFetchRequest",
        file: TOP_FILE_NAME,
        line: 29,
        asyncCause: null,
      },
    ],
  },
  {
    method: "GET",
    url: EXAMPLE_URL + "promise_fetch_request",
    causeType: "fetch",
    causeUri: TOP_URL,
    stack: [
      {
        fn: "performPromiseFetchRequest",
        file: TOP_FILE_NAME,
        line: 41,
        asyncCause: null,
      },
      {
        fn: null,
        file: TOP_FILE_NAME,
        line: 40,
        asyncCause: "promise callback",
      },
    ],
  },
  {
    method: "GET",
    url: EXAMPLE_URL + "timeout_fetch_request",
    causeType: "fetch",
    causeUri: TOP_URL,
    stack: [
      {
        fn: "performTimeoutFetchRequest",
        file: TOP_FILE_NAME,
        line: 43,
        asyncCause: null,
      },
      {
        fn: "performPromiseFetchRequest",
        file: TOP_FILE_NAME,
        line: 42,
        asyncCause: "setTimeout handler",
      },
    ],
  },
  {
    method: "POST",
    url: EXAMPLE_URL + "beacon_request",
    causeType: "beacon",
    causeUri: TOP_URL,
    stack: [
      {
        fn: "performBeaconRequest",
        file: TOP_FILE_NAME,
        line: 33,
        asyncCause: null,
      },
    ],
  },
];

const EXPECTED_REQUESTS_SUB = [
  {
    method: "GET",
    url: SUB_URL,
    causeType: "subdocument",
    causeUri: TOP_URL,
    stack: false,
  },
  {
    method: "GET",
    url: EXAMPLE_URL + "stylesheet_request",
    causeType: "stylesheet",
    causeUri: SUB_URL,
    stack: false,
  },
  {
    method: "GET",
    url: EXAMPLE_URL + "img_request",
    causeType: "img",
    causeUri: SUB_URL,
    stack: false,
  },
  {
    method: "GET",
    url: EXAMPLE_URL + "xhr_request",
    causeType: "xhr",
    causeUri: SUB_URL,
    stack: [
      {
        fn: "performXhrRequest",
        file: SUB_FILE_NAME,
        line: 24,
        asyncCause: null,
      },
    ],
  },
  {
    method: "GET",
    url: EXAMPLE_URL + "fetch_request",
    causeType: "fetch",
    causeUri: SUB_URL,
    stack: [
      {
        fn: "performFetchRequest",
        file: SUB_FILE_NAME,
        line: 28,
        asyncCause: null,
      },
    ],
  },
  {
    method: "GET",
    url: EXAMPLE_URL + "promise_fetch_request",
    causeType: "fetch",
    causeUri: SUB_URL,
    stack: [
      {
        fn: "performPromiseFetchRequest",
        file: SUB_FILE_NAME,
        line: 40,
        asyncCause: null,
      },
      {
        fn: null,
        file: SUB_FILE_NAME,
        line: 39,
        asyncCause: "promise callback",
      },
    ],
  },
  {
    method: "GET",
    url: EXAMPLE_URL + "timeout_fetch_request",
    causeType: "fetch",
    causeUri: SUB_URL,
    stack: [
      {
        fn: "performTimeoutFetchRequest",
        file: SUB_FILE_NAME,
        line: 42,
        asyncCause: null,
      },
      {
        fn: "performPromiseFetchRequest",
        file: SUB_FILE_NAME,
        line: 41,
        asyncCause: "setTimeout handler",
      },
    ],
  },
  {
    method: "POST",
    url: EXAMPLE_URL + "beacon_request",
    causeType: "beacon",
    causeUri: SUB_URL,
    stack: [
      {
        fn: "performBeaconRequest",
        file: SUB_FILE_NAME,
        line: 32,
        asyncCause: null,
      },
    ],
  },
];

const REQUEST_COUNT =
  EXPECTED_REQUESTS_TOP.length + EXPECTED_REQUESTS_SUB.length;

add_task(async function () {
  // the initNetMonitor function clears the network request list after the
  // page is loaded. That's why we first load a bogus page from SIMPLE_URL,
  // and only then load the real thing from TOP_URL - we want to catch
  // all the requests the page is making, not only the XHRs.
  // We can't use about:blank here, because initNetMonitor checks that the
  // page has actually made at least one request.
  const { monitor } = await initNetMonitor(SIMPLE_URL, { requestCount: 1 });

  const { document, store, windowRequire, connector } = monitor.panelWin;
  const Actions = windowRequire("devtools/client/netmonitor/src/actions/index");
  const { getDisplayedRequests, getSortedRequests } = windowRequire(
    "devtools/client/netmonitor/src/selectors/index"
  );

  store.dispatch(Actions.batchEnable(false));

  const onNetworkEvents = waitForNetworkEvents(monitor, REQUEST_COUNT);
  await navigateTo(TOP_URL);
  await onNetworkEvents;

  is(
    store.getState().requests.requests.length,
    REQUEST_COUNT,
    "All the page events should be recorded."
  );

  // Fetch stack-trace data from the backend and wait till
  // all packets are received.
  const requests = getSortedRequests(store.getState());
  await Promise.all(
    requests.map(requestItem =>
      connector.requestData(requestItem.id, "stackTrace")
    )
  );

  // While there is a defined order for requests in each document separately, the requests
  // from different documents may interleave in various ways that change per test run, so
  // there is not a single order when considering all the requests together.
  let currentTop = 0;
  let currentSub = 0;
  for (let i = 0; i < REQUEST_COUNT; i++) {
    const requestItem = getSortedRequests(store.getState())[i];

    const itemUrl = requestItem.url;
    const itemCauseUri = requestItem.cause.loadingDocumentUri;
    let spec;
    if (itemUrl == SUB_URL || itemCauseUri == SUB_URL) {
      spec = EXPECTED_REQUESTS_SUB[currentSub++];
    } else {
      spec = EXPECTED_REQUESTS_TOP[currentTop++];
    }
    const { method, url, causeType, causeUri, stack } = spec;

    await verifyRequestItemTarget(
      document,
      getDisplayedRequests(store.getState()),
      requestItem,
      method,
      url,
      { cause: { type: causeType, loadingDocumentUri: causeUri } }
    );

    const { stacktrace } = requestItem;
    const stackLen = stacktrace ? stacktrace.length : 0;

    if (stack) {
      ok(stacktrace, `Request #${i} has a stacktrace`);

      if (Array.isArray(stack)) {
        Assert.greater(
          stackLen,
          0,
          `Request #${i} (${causeType}) has a stacktrace with ${stackLen} items`
        );

        // Since "stack" is an array, check the details about the top stack frames
        stack.forEach((frame, j) => {
          is(
            stacktrace[j].functionName,
            frame.fn,
            `Request #${i} has the correct function on JS stack frame #${j}`
          );
          is(
            stacktrace[j].filename.split("/").pop(),
            frame.file,
            `Request #${i} has the correct file on JS stack frame #${j}`
          );
          is(
            stacktrace[j].lineNumber,
            frame.line,
            `Request #${i} has the correct line number on JS stack frame #${j}`
          );
          is(
            stacktrace[j].asyncCause,
            frame.asyncCause,
            `Request #${i} has the correct async cause on JS stack frame #${j}`
          );
        });
      }
    } else {
      is(stackLen, 0, `Request #${i} (${causeType}) has an empty stacktrace`);
    }
  }

  await teardown(monitor);
});
