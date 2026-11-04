/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Test the stylesheet caches in the network monitor.
add_task(async function () {
  const { monitor } = await initNetMonitor(STYLESHEET_CACHE_URL, {
    enableCache: true,
    requestCount: 1,
  });
  info("Starting test... ");

  const { document, store, windowRequire } = monitor.panelWin;
  const { getDisplayedRequests, getSortedRequests } = windowRequire(
    "devtools/client/netmonitor/src/selectors/index"
  );
  const Actions = windowRequire("devtools/client/netmonitor/src/actions/index");
  store.dispatch(Actions.batchEnable(false));

  const waitForEvents = waitForNetworkEvents(monitor, 2);

  BrowserTestUtils.startLoadingURIString(
    gBrowser.selectedBrowser,
    STYLESHEET_CACHE_URL
  );
  await waitForEvents;

  const requests = document.querySelectorAll(".request-list-item");

  is(requests.length, 2, "There should be 2 requests");

  const requestData = {
    uri: HTTPS_EXAMPLE_URL + "test-stylesheet.css",
    details: {
      status: 200,
      statusText: "OK (cached)",
      displayedStatus: "cached",
      type: "css",
      fullMimeType: "text/css",
    },
  };

  const cssIndex = 1;
  const request = requests[cssIndex];

  const requestsListStatus = request.querySelector(".status-code");
  EventUtils.sendMouseEvent({ type: "mouseover" }, requestsListStatus);
  await waitUntil(() => requestsListStatus.title);

  await verifyRequestItemTarget(
    document,
    getDisplayedRequests(store.getState()),
    getSortedRequests(store.getState())[cssIndex],
    "GET",
    requestData.uri,
    requestData.details
  );

  clickOnRequestRow(request);

  const onDomUpdated = waitForDOM(document, "#responseHeaders");
  clickOnSidebarTab(document, "headers");
  await onDomUpdated;

  const responseScope = document.querySelectorAll(
    "#headers-panel .accordion tr[id^='/responseHeaders']"
  );

  const responseHeaders = [
    {
      name: "connection",
      value: "close",
      index: 1,
    },
    {
      name: "content-length",
      value: "37",
      index: 2,
    },
    {
      name: "content-type",
      value: "text/css",
      index: 3,
    },
    {
      name: "server",
      value: "httpd.js",
      index: 6,
    },
  ];
  responseHeaders.forEach(header => {
    is(
      responseScope[header.index - 1].querySelector(".treeLabel").innerHTML,
      header.name,
      `${header.name} label`
    );
    is(
      responseScope[header.index - 1].querySelector(".objectBox").innerHTML,
      `${header.value}`,
      `${header.name} value`
    );
  });

  await teardown(monitor);
});
