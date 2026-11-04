/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Test if filtering items in the network table works correctly with new requests.
 */

const BASIC_REQUESTS = [
  { url: "sjs_content-type-test-server.sjs?fmt=html&res=undefined" },
  { url: "sjs_content-type-test-server.sjs?fmt=xhtml" },
  { url: "sjs_content-type-test-server.sjs?fmt=css" },
  { url: "sjs_content-type-test-server.sjs?fmt=js" },
];

const REQUESTS_WITH_MEDIA = BASIC_REQUESTS.concat([
  { url: "sjs_content-type-test-server.sjs?fmt=font" },
  { url: "sjs_content-type-test-server.sjs?fmt=image" },
  { url: "sjs_content-type-test-server.sjs?fmt=audio" },
  { url: "sjs_content-type-test-server.sjs?fmt=video" },
]);

const REQUESTS_WITH_MEDIA_AND_FLASH = REQUESTS_WITH_MEDIA.concat([
  { url: "sjs_content-type-test-server.sjs?fmt=flash" },
]);

const REQUESTS_WITH_MEDIA_AND_FLASH_AND_WS =
  REQUESTS_WITH_MEDIA_AND_FLASH.concat([
    /* "Upgrade" is a reserved header and can not be set on XMLHttpRequest */
    { url: "sjs_content-type-test-server.sjs?fmt=ws" },
  ]);

const EXPECTED_REQUESTS = [
  {
    method: "GET",
    url: CONTENT_TYPE_SJS + "?fmt=html",
    data: {
      fuzzyUrl: true,
      status: 200,
      statusText: "OK",
      type: "html",
      fullMimeType: "text/html; charset=utf-8",
    },
  },
  {
    method: "GET",
    url: CONTENT_TYPE_SJS + "?fmt=xhtml",
    data: {
      fuzzyUrl: true,
      status: 200,
      statusText: "OK",
      type: "xhtml",
      fullMimeType: "application/xhtml+xml; charset=utf-8",
    },
  },
  {
    method: "GET",
    url: CONTENT_TYPE_SJS + "?fmt=css",
    data: {
      fuzzyUrl: true,
      status: 200,
      statusText: "OK",
      type: "css",
      fullMimeType: "text/css; charset=utf-8",
    },
  },
  {
    method: "GET",
    url: CONTENT_TYPE_SJS + "?fmt=js",
    data: {
      fuzzyUrl: true,
      status: 200,
      statusText: "OK",
      type: "js",
      fullMimeType: "application/javascript; charset=utf-8",
    },
  },
  {
    method: "GET",
    url: CONTENT_TYPE_SJS + "?fmt=font",
    data: {
      fuzzyUrl: true,
      status: 200,
      statusText: "OK",
      type: "woff",
      fullMimeType: "font/woff",
    },
  },
  {
    method: "GET",
    url: CONTENT_TYPE_SJS + "?fmt=image",
    data: {
      fuzzyUrl: true,
      status: 200,
      statusText: "OK",
      type: "png",
      fullMimeType: "image/png",
    },
  },
  {
    method: "GET",
    url: CONTENT_TYPE_SJS + "?fmt=audio",
    data: {
      fuzzyUrl: true,
      status: 200,
      statusText: "OK",
      type: "ogg",
      fullMimeType: "audio/ogg",
    },
  },
  {
    method: "GET",
    url: CONTENT_TYPE_SJS + "?fmt=video",
    data: {
      fuzzyUrl: true,
      status: 200,
      statusText: "OK",
      type: "webm",
      fullMimeType: "video/webm",
    },
  },
  {
    method: "GET",
    url: CONTENT_TYPE_SJS + "?fmt=flash",
    data: {
      fuzzyUrl: true,
      status: 200,
      statusText: "OK",
      type: "x-shockwave-flash",
      fullMimeType: "application/x-shockwave-flash",
    },
  },
  {
    method: "GET",
    url: CONTENT_TYPE_SJS + "?fmt=ws",
    data: {
      fuzzyUrl: true,
      status: 101,
      statusText: "Switching Protocols",
    },
  },
];

add_task(async function () {
  const { monitor } = await initNetMonitor(FILTERING_URL, { requestCount: 1 });
  info("Starting test... ");

  // It seems that this test may be slow on Ubuntu builds running on ec2.
  requestLongerTimeout(2);

  const { document, store, windowRequire } = monitor.panelWin;
  const Actions = windowRequire("devtools/client/netmonitor/src/actions/index");
  const { getDisplayedRequests, getSelectedRequest, getSortedRequests } =
    windowRequire("devtools/client/netmonitor/src/selectors/index");

  store.dispatch(Actions.batchEnable(false));

  let onNetworkEvents = waitForNetworkEvents(monitor, 10);
  await performRequestsInContent(REQUESTS_WITH_MEDIA_AND_FLASH_AND_WS);
  await onNetworkEvents;

  clickOnRequestRow(document.querySelectorAll(".request-list-item")[0]);

  isnot(
    getSelectedRequest(store.getState()),
    null,
    "There should be a selected item in the requests menu."
  );
  is(
    getSelectedIndex(store.getState()),
    0,
    "The first item should be selected in the requests menu."
  );
  is(
    !!document.querySelector(".network-details-bar"),
    true,
    "The network details panel should be visible after toggle button was pressed."
  );

  testFilterButtons(monitor, "all");
  await testContents([1, 1, 1, 1, 1, 1, 1, 1, 1, 1]);

  info("Testing html filtering.");
  EventUtils.sendMouseEvent(
    { type: "click" },
    document.querySelector(".requests-list-filter-html-button")
  );
  testFilterButtons(monitor, "html");
  await testContents([1, 1, 0, 0, 0, 0, 0, 0, 0, 0]);

  info("Performing more requests.");
  // As the view is filtered and there is only one request for which we fetch event timings
  onNetworkEvents = waitForNetworkEvents(monitor, 10, {
    expectedEventTimings: 1,
  });
  await performRequestsInContent(REQUESTS_WITH_MEDIA_AND_FLASH_AND_WS);
  await onNetworkEvents;

  info("Testing html filtering again.");
  testFilterButtons(monitor, "html");
  await testContents([
    1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0,
  ]);

  info("Performing more requests.");
  onNetworkEvents = waitForNetworkEvents(monitor, 10, {
    expectedEventTimings: 1,
  });
  await performRequestsInContent(REQUESTS_WITH_MEDIA_AND_FLASH_AND_WS);
  await onNetworkEvents;

  info("Testing html filtering again.");
  testFilterButtons(monitor, "html");
  await testContents([
    1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0,
    0, 0, 0, 0, 0,
  ]);

  info("Resetting filters.");
  EventUtils.sendMouseEvent(
    { type: "click" },
    document.querySelector(".requests-list-filter-all-button")
  );
  testFilterButtons(monitor, "all");
  await testContents([
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1,
  ]);

  await teardown(monitor);

  function getSelectedIndex(state) {
    if (!state.requests.selectedId) {
      return -1;
    }
    return getSortedRequests(state).findIndex(
      r => r.id === state.requests.selectedId
    );
  }

  async function testContents(visibility) {
    await waitForAllNetworkUpdateEvents();
    const requestItems = document.querySelectorAll(".request-list-item");
    for (const requestItem of requestItems) {
      requestItem.scrollIntoView();
      const requestsListStatus = requestItem.querySelector(".status-code");
      EventUtils.sendMouseEvent({ type: "mouseover" }, requestsListStatus);
      await waitUntil(() => requestsListStatus.title);
    }

    isnot(
      getSelectedRequest(store.getState()),
      null,
      "There should still be a selected item after filtering."
    );
    is(
      getSelectedIndex(store.getState()),
      0,
      "The first item should be still selected after filtering."
    );
    is(
      !!document.querySelector(".network-details-bar"),
      true,
      "The network details panel should still be visible after filtering."
    );

    const items = getSortedRequests(store.getState());
    const visibleItems = getDisplayedRequests(store.getState());

    is(
      items.length,
      visibility.length,
      "There should be a specific amount of items in the requests menu."
    );
    is(
      visibleItems.length,
      visibility.filter(e => e).length,
      "There should be a specific amount of visible items in the requests menu."
    );

    for (let i = 0; i < visibility.length; i++) {
      const itemId = items[i].id;
      const shouldBeVisible = !!visibility[i];
      const isThere = visibleItems.some(r => r.id == itemId);
      is(
        isThere,
        shouldBeVisible,
        `The item at index ${i} has visibility=${shouldBeVisible}`
      );
    }

    for (let i = 0; i < EXPECTED_REQUESTS.length; i++) {
      const { method, url, data } = EXPECTED_REQUESTS[i];
      for (let j = i; j < visibility.length; j += EXPECTED_REQUESTS.length) {
        if (visibility[j]) {
          await verifyRequestItemTarget(
            document,
            getDisplayedRequests(store.getState()),
            getSortedRequests(store.getState())[i],
            method,
            url,
            data
          );
        }
      }
    }
  }
});
