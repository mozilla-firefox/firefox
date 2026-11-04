/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Test that requests blocked by Connection-Allowlist display in the netmonitor
 */

add_task(async function () {
  await pushPref("security.connection_allowlists.enabled", true);

  const { monitor } = await initNetMonitor(
    HTTPS_EXAMPLE_URL + "html_connection-allowlist-test-page.html",
    { requestCount: 3 }
  );

  const { document, store, windowRequire } = monitor.panelWin;
  const Actions = windowRequire("devtools/client/netmonitor/src/actions/index");
  const { getDisplayedRequests } = windowRequire(
    "devtools/client/netmonitor/src/selectors/index"
  );

  store.dispatch(Actions.batchEnable(false));

  const onNetworkEvents = waitForNetworkEvents(monitor, 3);
  await reloadSelectedTab();
  await onNetworkEvents;

  const displayedRequests = getDisplayedRequests(store.getState());

  const allowedUrl = HTTPS_EXAMPLE_URL + "test-image.png";
  const allowedRequest = displayedRequests.find(r => r.url == allowedUrl);
  ok(!allowedRequest.blockedReason, "Same-origin request is not blocked");

  const blockedUrl = HTTPS_EXAMPLE_ORG_URL + "test-image.png";
  const blockedRequest = displayedRequests.find(r => r.url == blockedUrl);
  is(
    blockedRequest.blockedReason,
    Ci.nsILoadInfo.BLOCKING_REASON_CONNECTION_ALLOWLIST,
    "Cross-origin request is blocked by Connection-Allowlist"
  );

  await verifyRequestItemTarget(
    document,
    displayedRequests,
    blockedRequest,
    "GET",
    blockedUrl,
    {
      transferred: "Connection Allowlist",
      cause: { type: "img" },
      type: "",
    }
  );

  await teardown(monitor);
});
