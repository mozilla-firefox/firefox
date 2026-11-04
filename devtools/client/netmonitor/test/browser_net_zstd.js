/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const ZSTD_URL = HTTPS_EXAMPLE_URL + "html_zstd-test-page.html";
const ZSTD_REQUESTS = 1;

/**
 * Test zstd encoded response is handled correctly on HTTPS.
 */

add_task(async function () {
  const {
    L10N,
  } = require("resource://devtools/client/netmonitor/src/utils/l10n.js");

  const { tab, monitor } = await initNetMonitor(ZSTD_URL, {
    requestCount: 1,
  });
  info("Starting test... ");

  const { document, store, windowRequire } = monitor.panelWin;
  const Actions = windowRequire("devtools/client/netmonitor/src/actions/index");
  const { getDisplayedRequests, getSortedRequests } = windowRequire(
    "devtools/client/netmonitor/src/selectors/index"
  );

  store.dispatch(Actions.batchEnable(false));

  // Execute requests.
  await performRequests(monitor, tab, ZSTD_REQUESTS);

  const requestItem = document.querySelector(".request-list-item");
  // Status code title is generated on hover
  const requestsListStatus = requestItem.querySelector(".status-code");
  EventUtils.sendMouseEvent({ type: "mouseover" }, requestsListStatus);
  await waitUntil(() => requestsListStatus.title);
  await waitForDOM(requestItem, ".requests-list-timings-total");

  await verifyRequestItemTarget(
    document,
    getDisplayedRequests(store.getState()),
    getSortedRequests(store.getState())[0],
    "GET",
    HTTPS_CONTENT_TYPE_SJS + "?fmt=zstd",
    {
      status: 200,
      statusText: "OK",
      type: "json",
      fullMimeType: "text/json",
      transferred: L10N.getFormatStrWithNumbers("networkMenu.sizeB", 261),
      size: L10N.getFormatStrWithNumbers("networkMenu.sizeB", 64),
      time: true,
    }
  );

  const onDomUpdated = waitForDOM(document, ".cm-content");
  const onResponseContent = monitor.panelWin.api.once(
    TEST_EVENTS.RECEIVED_RESPONSE_CONTENT
  );
  store.dispatch(Actions.toggleNetworkDetails());
  clickOnSidebarTab(document, "response");
  await onDomUpdated;
  await onResponseContent;
  await testResponse("zstd");
  await teardown(monitor);

  function testResponse(type) {
    switch (type) {
      case "zstd": {
        is(
          getCodeMirrorValue(monitor),
          "X".repeat(64),
          "The text shown in the source editor is incorrect for the zstd request."
        );
        break;
      }
    }
  }
});
