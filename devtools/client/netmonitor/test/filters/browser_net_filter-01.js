/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Used to test filtering a unicode URI component
const UNICODE_IN_URI_COMPONENT = "\u6e2c";
const ENCODED_CHARS_IN_URI_COMP = encodeURIComponent(UNICODE_IN_URI_COMPONENT);

// Used to test filtering an international domain name with Unicode
const IDN = "xn--hxajbheg2az3al.xn--jxalpdlp";
const UNICODE_IN_IDN = "\u03c0\u03b1";

/**
 * Test if filtering items in the network table works correctly.
 */
const BASIC_REQUESTS = [
  {
    url: getSjsURLInUnicodeIdn() + "?fmt=html&res=undefined&text=Sample&cors=1",
  },
  { url: "sjs_content-type-test-server.sjs?fmt=css&text=sample" },
  { url: "sjs_content-type-test-server.sjs?fmt=js&text=sample" },
  {
    url: `sjs_content-type-test-server.sjs?fmt=html&text=${ENCODED_CHARS_IN_URI_COMP}`,
  },
  {
    url: `sjs_content-type-test-server.sjs?fmt=css&text=${ENCODED_CHARS_IN_URI_COMP}`,
  },
  {
    url: `sjs_content-type-test-server.sjs?fmt=js&text=${ENCODED_CHARS_IN_URI_COMP}`,
  },
];

const REQUESTS_WITH_MEDIA = BASIC_REQUESTS.concat([
  { url: getSjsURLInUnicodeIdn() + "?fmt=font&cors=1" },
  { url: "sjs_content-type-test-server.sjs?fmt=image" },
  { url: "sjs_content-type-test-server.sjs?fmt=audio" },
  { url: "sjs_content-type-test-server.sjs?fmt=application-ogg" },
  { url: "sjs_content-type-test-server.sjs?fmt=video" },
  { url: "sjs_content-type-test-server.sjs?fmt=hls-m3u8" },
  { url: "sjs_content-type-test-server.sjs?fmt=hls-m3u8-alt-mime-type" },
]);

const REQUESTS_WITH_MEDIA_AND_FLASH = REQUESTS_WITH_MEDIA.concat([
  { url: "sjs_content-type-test-server.sjs?fmt=flash" },
]);

const REQUESTS_WITH_MEDIA_AND_FLASH_AND_WS =
  REQUESTS_WITH_MEDIA_AND_FLASH.concat([
    /* Use new WebSocket() to mock native websocket request, then "Upgrade" will be added */
    { url: WS_WS_CONTENT_TYPE_SJS + "?fmt=ws", ws: true },
  ]);

const EXPECTED_REQUESTS = [
  {
    method: "GET",
    url: getSjsURLInUnicodeIdn() + "?fmt=html&res=undefined&text=Sample&cors=1",
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
    url: CONTENT_TYPE_SJS + "?fmt=css&text=sample",
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
    url: CONTENT_TYPE_SJS + "?fmt=js&text=sample",
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
    url: CONTENT_TYPE_SJS + `?fmt=html&text=${ENCODED_CHARS_IN_URI_COMP}`,
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
    url: CONTENT_TYPE_SJS + `?fmt=css&text=${ENCODED_CHARS_IN_URI_COMP}`,
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
    url: CONTENT_TYPE_SJS + `?fmt=js&text=${ENCODED_CHARS_IN_URI_COMP}`,
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
    url: getSjsURLInUnicodeIdn() + "?fmt=font&cors=1",
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
    url: CONTENT_TYPE_SJS + "?fmt=application-ogg",
    data: {
      fuzzyUrl: true,
      status: 200,
      statusText: "OK",
      type: "ogg",
      fullMimeType: "application/ogg",
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
    url: CONTENT_TYPE_SJS + "?fmt=hls-m3u8",
    data: {
      fuzzyUrl: true,
      status: 200,
      statusText: "OK",
      type: "x-mpegurl",
      fullMimeType: "application/x-mpegurl",
    },
  },
  {
    method: "GET",
    url: CONTENT_TYPE_SJS + "?fmt=hls-m3u8-alt-mime-type",
    data: {
      fuzzyUrl: true,
      status: 200,
      statusText: "OK",
      type: "vnd.apple.mpegurl",
      fullMimeType: "application/vnd.apple.mpegurl",
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
    url: WS_WS_CONTENT_TYPE_SJS + "?fmt=ws",
    data: {
      fuzzyUrl: true,
      status: 101,
      statusText: "Switching Protocols",
    },
  },
];

add_task(async function () {
  const { monitor } = await initNetMonitor(FILTERING_URL, { requestCount: 1 });
  const { document, store, windowRequire } = monitor.panelWin;
  const Actions = windowRequire("devtools/client/netmonitor/src/actions/index");
  const { getDisplayedRequests, getSelectedRequest, getSortedRequests } =
    windowRequire("devtools/client/netmonitor/src/selectors/index");

  store.dispatch(Actions.batchEnable(false));

  function setFreetextFilter(value) {
    store.dispatch(Actions.setRequestFilterText(value));
  }

  info("Starting test... ");

  const onNetworkEvents = waitForNetworkEvents(
    monitor,
    REQUESTS_WITH_MEDIA_AND_FLASH_AND_WS.length
  );
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
    "The network details panel should render correctly."
  );

  // First test with single filters...
  testFilterButtons(monitor, "all");
  await testContents([1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1]);

  EventUtils.sendMouseEvent(
    { type: "click" },
    document.querySelector(".requests-list-filter-html-button")
  );
  testFilterButtons(monitor, "html");
  await testContents([1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]);

  // Reset filters
  EventUtils.sendMouseEvent(
    { type: "click" },
    document.querySelector(".requests-list-filter-all-button")
  );
  EventUtils.sendMouseEvent(
    { type: "click" },
    document.querySelector(".requests-list-filter-css-button")
  );
  testFilterButtons(monitor, "css");
  await testContents([0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]);

  EventUtils.sendMouseEvent(
    { type: "click" },
    document.querySelector(".requests-list-filter-all-button")
  );
  EventUtils.sendMouseEvent(
    { type: "click" },
    document.querySelector(".requests-list-filter-js-button")
  );
  testFilterButtons(monitor, "js");
  await testContents([0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0]);

  EventUtils.sendMouseEvent(
    { type: "click" },
    document.querySelector(".requests-list-filter-all-button")
  );
  EventUtils.sendMouseEvent(
    { type: "click" },
    document.querySelector(".requests-list-filter-xhr-button")
  );
  testFilterButtons(monitor, "xhr");
  await testContents([1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0]);

  EventUtils.sendMouseEvent(
    { type: "click" },
    document.querySelector(".requests-list-filter-all-button")
  );
  EventUtils.sendMouseEvent(
    { type: "click" },
    document.querySelector(".requests-list-filter-fonts-button")
  );
  testFilterButtons(monitor, "fonts");
  await testContents([0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0]);

  EventUtils.sendMouseEvent(
    { type: "click" },
    document.querySelector(".requests-list-filter-all-button")
  );
  EventUtils.sendMouseEvent(
    { type: "click" },
    document.querySelector(".requests-list-filter-images-button")
  );
  testFilterButtons(monitor, "images");
  await testContents([0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0]);

  EventUtils.sendMouseEvent(
    { type: "click" },
    document.querySelector(".requests-list-filter-all-button")
  );
  EventUtils.sendMouseEvent(
    { type: "click" },
    document.querySelector(".requests-list-filter-media-button")
  );
  testFilterButtons(monitor, "media");
  await testContents([0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 0, 0]);

  EventUtils.sendMouseEvent(
    { type: "click" },
    document.querySelector(".requests-list-filter-all-button")
  );
  EventUtils.sendMouseEvent(
    { type: "click" },
    document.querySelector(".requests-list-filter-ws-button")
  );
  testFilterButtons(monitor, "ws");
  await testContents([0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1]);

  EventUtils.sendMouseEvent(
    { type: "click" },
    document.querySelector(".requests-list-filter-all-button")
  );

  testFilterButtons(monitor, "all");
  await testContents([1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1]);

  // Text in filter box that matches nothing should hide all.
  EventUtils.sendMouseEvent(
    { type: "click" },
    document.querySelector(".requests-list-filter-all-button")
  );
  setFreetextFilter("foobar");
  await testContents([0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]);

  // ASCII text in filter box that matches should filter out everything else.
  EventUtils.sendMouseEvent(
    { type: "click" },
    document.querySelector(".requests-list-filter-all-button")
  );
  setFreetextFilter("sample");
  await testContents([1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]);

  // ASCII text in filter box that matches should filter out everything else.
  EventUtils.sendMouseEvent(
    { type: "click" },
    document.querySelector(".requests-list-filter-all-button")
  );
  setFreetextFilter("SAMPLE");
  await testContents([1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]);

  // Test negative filtering ASCII text(only show unmatched items)
  EventUtils.sendMouseEvent(
    { type: "click" },
    document.querySelector(".requests-list-filter-all-button")
  );
  setFreetextFilter("-sample");
  await testContents([0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1]);

  // Unicode text in filter box that matches should filter out everything else.
  EventUtils.sendMouseEvent(
    { type: "click" },
    document.querySelector(".requests-list-filter-all-button")
  );
  setFreetextFilter(UNICODE_IN_URI_COMPONENT);
  await testContents([0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0]);

  // Ditto, except the above is for a Unicode URI component, and this one is for
  // a Unicode domain name.
  EventUtils.sendMouseEvent(
    { type: "click" },
    document.querySelector(".requests-list-filter-all-button")
  );
  setFreetextFilter(UNICODE_IN_IDN);
  await testContents([1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0]);

  // Test negative filtering Unicode text(only show unmatched items)
  EventUtils.sendMouseEvent(
    { type: "click" },
    document.querySelector(".requests-list-filter-all-button")
  );
  setFreetextFilter(`-${UNICODE_IN_URI_COMPONENT}`);
  await testContents([1, 1, 1, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1]);

  // Ditto, except the above is for a Unicode URI component, and this one is for
  // a Unicode domain name.
  EventUtils.sendMouseEvent(
    { type: "click" },
    document.querySelector(".requests-list-filter-all-button")
  );
  setFreetextFilter(`-${UNICODE_IN_IDN}`);
  await testContents([0, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1]);

  // ...then combine multiple filters together.

  // Enable filtering for html and css; should show request of both type.
  setFreetextFilter("");
  EventUtils.sendMouseEvent(
    { type: "click" },
    document.querySelector(".requests-list-filter-html-button")
  );
  EventUtils.sendMouseEvent(
    { type: "click" },
    document.querySelector(".requests-list-filter-css-button")
  );
  testFilterButtonsCustom(monitor, [0, 1, 1, 0, 0, 0, 0, 0, 0, 0]);
  await testContents([1, 1, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]);

  // Html and css filter enabled and text filter should show just the html and css match.
  // Should not show both the items matching the button plus the items matching the text.
  setFreetextFilter("sample");
  await testContents([1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]);
  setFreetextFilter(UNICODE_IN_URI_COMPONENT);
  await testContents([0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]);
  setFreetextFilter("");
  testFilterButtonsCustom(monitor, [0, 1, 1, 0, 0, 0, 0, 0, 0, 0]);
  await testContents([1, 1, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]);

  // Disable some filters. Only one left active.
  EventUtils.sendMouseEvent(
    { type: "click" },
    document.querySelector(".requests-list-filter-css-button")
  );
  testFilterButtons(monitor, "html");
  await testContents([1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]);

  // Disable last active filter. Should toggle to all.
  EventUtils.sendMouseEvent(
    { type: "click" },
    document.querySelector(".requests-list-filter-html-button")
  );
  testFilterButtons(monitor, "all");
  await testContents([1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1]);

  // Enable few filters and click on all. Only "all" should be checked.
  EventUtils.sendMouseEvent(
    { type: "click" },
    document.querySelector(".requests-list-filter-html-button")
  );
  EventUtils.sendMouseEvent(
    { type: "click" },
    document.querySelector(".requests-list-filter-css-button")
  );
  EventUtils.sendMouseEvent(
    { type: "click" },
    document.querySelector(".requests-list-filter-ws-button")
  );
  testFilterButtonsCustom(monitor, [0, 1, 1, 0, 0, 0, 0, 0, 1, 0]);
  EventUtils.sendMouseEvent(
    { type: "click" },
    document.querySelector(".requests-list-filter-all-button")
  );
  testFilterButtons(monitor, "all");
  await testContents([1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1]);

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
    const requestItems = document.querySelectorAll(".request-list-item");
    for (const requestItem of requestItems) {
      requestItem.scrollIntoView();
      await waitUntil(() => requestItem.querySelector(".status-code"));
      const requestsListStatus = requestItem.querySelector(".status-code");
      EventUtils.sendMouseEvent({ type: "mouseover" }, requestsListStatus);
      await waitUntil(() => requestsListStatus.title);
    }

    const items = getSortedRequests(store.getState());
    let visibleItems;

    // Filter results will be updated asynchronously, so we should wait until
    // displayed requests reach final state.
    await waitUntil(() => {
      visibleItems = getDisplayedRequests(store.getState());
      return visibleItems.length === visibility.filter(e => e).length;
    });

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

      if (shouldBeVisible) {
        const { method, url, data } = EXPECTED_REQUESTS[i];
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
});

function getSjsURLInUnicodeIdn() {
  const { hostname } = new URL(CONTENT_TYPE_SJS);
  return CONTENT_TYPE_SJS.replace(hostname, IDN);
}
