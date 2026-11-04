/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Tests if request JS stack is properly reported if the request is internally
 * redirected without hitting the network (HSTS is one of such cases)
 */

add_task(async function testStackTraceForRedirects() {
  // This test explicitly checks http->https redirects and should not force https.
  await pushPref("dom.security.https_first", false);

  const EXPECTED_REQUESTS = [
    // Request to HTTP URL, redirects to HTTPS
    { status: 302 },
    // Serves HTTPS, sets the Strict-Transport-Security header
    // This request is the redirection caused by the first one
    { status: 200 },
    // Second request to HTTP redirects to HTTPS internally
    { status: 200 },
  ];

  const { tab, monitor } = await initNetMonitor(CUSTOM_GET_URL, {
    requestCount: 1,
  });
  const { store, windowRequire, connector } = monitor.panelWin;
  const Actions = windowRequire("devtools/client/netmonitor/src/actions/index");
  const { getSortedRequests } = windowRequire(
    "devtools/client/netmonitor/src/selectors/index"
  );

  store.dispatch(Actions.batchEnable(false));

  let onNetworkEvents = waitForNetworkEvents(monitor, EXPECTED_REQUESTS.length);
  await performRequests(tab, 2, HSTS_SJS);
  await onNetworkEvents;

  // Fetch stack-trace data from the backend and wait till
  // all packets are received.
  const requests = getSortedRequests(store.getState())
    .filter(req => !req.stacktrace)
    .map(req => connector.requestData(req.id, "stackTrace"));

  await Promise.all(requests);

  for (const [i, { status }] of EXPECTED_REQUESTS.entries()) {
    // Wait for the status data for the request to get updated
    await waitForRequestData(store, ["status"], null, i);
    const item = getSortedRequests(store.getState())[i];

    is(
      parseInt(item.status, 10),
      status,
      `Request #${i} has the expected status`
    );

    const { stacktrace } = item;
    const stackLen = stacktrace ? stacktrace.length : 0;

    ok(stacktrace, `Request #${i} has a stacktrace`);
    Assert.greater(
      stackLen,
      0,
      `Request #${i} has a stacktrace with ${stackLen} items`
    );
  }

  // Send a request to reset the HSTS policy to state before the test
  onNetworkEvents = waitForNetworkEvents(monitor, 1);
  await performRequests(tab, 1, HSTS_SJS + "?reset");
  await onNetworkEvents;

  await teardown(monitor);
});

/**
 * Asserts that no response content is displayed for redirect requests,
 * the size of the content is 0 and indicate that the request is a redirect.
 */

add_task(async function testResponseForRedirects() {
  const {
    L10N,
  } = require("resource://devtools/client/netmonitor/src/utils/l10n.js");
  // This test explicitly checks http->https redirects and should not force https.
  await pushPref("dom.security.https_first", false);

  const { tab, monitor } = await initNetMonitor(CUSTOM_GET_URL, {
    requestCount: 1,
  });
  const { store, windowRequire, document } = monitor.panelWin;
  const Actions = windowRequire("devtools/client/netmonitor/src/actions/index");

  store.dispatch(Actions.batchEnable(false));

  let onNetworkEvents = waitForNetworkEvents(monitor, 3);
  await performRequests(tab, 2, HSTS_SJS);
  await onNetworkEvents;

  await waitUntil(
    () => document.querySelectorAll(".request-list-item").length == 3
  );

  const requests = document.querySelectorAll(".request-list-item");
  is(
    requests[0].querySelector(".requests-list-type").innerText,
    L10N.getFormatStr("networkMenu.redirect", "plain"),
    "The type indicates that the request is a redirect"
  );

  is(
    requests[0].querySelector(".requests-list-size").innerText,
    "0 B",
    "The size in the column is 0 bytes"
  );

  info("Select the redirect request");
  clickOnRequestRow(requests[0]);

  info("Switch to response panel");
  const waitForRespPanel = waitForDOM(
    document,
    "#response-panel .panel-container"
  );
  const respPanelButton = document.querySelector("#response-tab");
  respPanelButton.click();
  await waitForRespPanel;

  const emptyNotice = document.querySelector(".empty-notice");
  is(
    emptyNotice.innerText,
    L10N.getStr("responseRedirectEmptyText"),
    "No response content should be displayed for the redirect request"
  );

  // Send a request to reset the HSTS policy to state before the test
  onNetworkEvents = waitForNetworkEvents(monitor, 1);
  await performRequests(tab, 1, HSTS_SJS + "?reset");
  await onNetworkEvents;

  await teardown(monitor);
});

function performRequests(tab, count, url) {
  return SpecialPowers.spawn(
    tab.linkedBrowser,
    [{ count, url }],
    async function (args) {
      content.wrappedJSObject.performRequests(args.count, args.url);
    }
  );
}
