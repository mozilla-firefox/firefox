/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Test that CSP violations display in the netmonitor when blocked
 */

add_task(async function () {
  info("Test requests blocked by CSP in the top level document");
  await testRequestsBlockedByCSP(
    HTTPS_EXAMPLE_URL,
    HTTPS_EXAMPLE_URL + "html_csp-test-page.html"
  );

  // The html_csp-frame-test-page.html (in the .com domain) includes
  // an iframe from the .org domain
  info("Test requests blocked by CSP in remote frames");
  await testRequestsBlockedByCSP(
    HTTPS_EXAMPLE_ORG_URL,
    HTTPS_EXAMPLE_URL + "html_csp-frame-test-page.html"
  );
});

async function testRequestsBlockedByCSP(baseUrl, page) {
  const { monitor } = await initNetMonitor(page, { requestCount: 3 });

  const { document, store, windowRequire } = monitor.panelWin;
  const Actions = windowRequire("devtools/client/netmonitor/src/actions/index");
  const { getDisplayedRequests } = windowRequire(
    "devtools/client/netmonitor/src/selectors/index"
  );

  const scriptFileName = "js_websocket-worker-test.js";
  const styleFileName = "internal-loaded.css";

  store.dispatch(Actions.batchEnable(false));

  const onNetworkEvents = waitForNetworkEvents(monitor, 3);
  await reloadSelectedTab();
  info("Waiting until the requests appear in netmonitor");
  await onNetworkEvents;

  const displayedRequests = getDisplayedRequests(store.getState());

  const styleRequest = displayedRequests.find(request =>
    request.url.includes(styleFileName)
  );

  info("Ensure the attempt to load a CSS file shows a blocked CSP error");

  await verifyRequestItemTarget(
    document,
    displayedRequests,
    styleRequest,
    "GET",
    baseUrl + styleFileName,
    {
      transferred: "CSP",
      cause: { type: "stylesheet" },
      type: "",
    }
  );

  const scriptRequest = displayedRequests.find(request =>
    request.url.includes(scriptFileName)
  );

  info("Test that the attempt to load a JS file shows a blocked CSP error");

  await verifyRequestItemTarget(
    document,
    displayedRequests,
    scriptRequest,
    "GET",
    baseUrl + scriptFileName,
    {
      transferred: "CSP",
      cause: { type: "script" },
      type: "",
    }
  );

  info("Test that header infomation is available for blocked CSP requests");

  const requestEl = document.querySelector(
    `.requests-list-column[title*="${scriptFileName}"]`
  ).parentNode;

  const waitForHeadersPanel = waitUntil(() =>
    document.querySelector("#headers-panel .panel-container")
  );
  clickElement(requestEl, monitor);
  await waitForHeadersPanel;

  ok(
    document.querySelector(".headers-overview"),
    "There is request overview details"
  );
  ok(
    document.querySelector(".accordion #requestHeaders"),
    "There is request header information"
  );
  ok(
    !document.querySelector(".accordion #responseHeaders"),
    "There is no response header information"
  );

  await teardown(monitor);
}
