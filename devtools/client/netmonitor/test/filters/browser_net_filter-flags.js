/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

requestLongerTimeout(3);

/**
 * Test different text filtering flags
 */
const REQUESTS = [
  {
    url: "sjs_content-type-test-server.sjs?fmt=html&res=undefined&text=Sample",
  },
  {
    url:
      "sjs_content-type-test-server.sjs?fmt=html&res=undefined&text=Sample" +
      "&cookies=1",
  },
  { url: "sjs_content-type-test-server.sjs?fmt=css&text=sample" },
  { url: "sjs_content-type-test-server.sjs?fmt=js&text=sample" },
  { url: "sjs_content-type-test-server.sjs?fmt=font" },
  { url: "sjs_content-type-test-server.sjs?fmt=image" },
  { url: "sjs_content-type-test-server.sjs?fmt=audio" },
  { url: "sjs_content-type-test-server.sjs?fmt=video" },
  { url: "sjs_content-type-test-server.sjs?fmt=gzip" },
  { url: "sjs_status-codes-test-server.sjs?sts=304" },
];

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
    url: CONTENT_TYPE_SJS + "?fmt=gzip",
    data: {
      fuzzyUrl: true,
      status: 200,
      statusText: "OK",
      displayedStatus: "200",
      type: "plain",
      fullMimeType: "text/plain",
    },
  },
  {
    method: "GET",
    url: STATUS_CODES_SJS + "?sts=304",
    data: {
      status: 304,
      statusText: "Not Modified",
      displayedStatus: "304",
      type: "plain",
      fullMimeType: "text/plain; charset=utf-8",
    },
  },
];

add_task(async function () {
  const { monitor } = await initNetMonitor(FILTERING_URL, { requestCount: 1 });
  const { document, store, windowRequire } = monitor.panelWin;
  const Actions = windowRequire("devtools/client/netmonitor/src/actions/index");
  const { getDisplayedRequests, getSortedRequests } = windowRequire(
    "devtools/client/netmonitor/src/selectors/index"
  );

  store.dispatch(Actions.batchEnable(false));

  // Filtering network request will start fetching data lazily
  // (fetching requestHeaders & responseHeaders for filtering WS & XHR)
  // Lazy fetching will be executed when user focuses on filter box.
  function setFreetextFilter(value) {
    const filterBox = document.querySelector(".devtools-filterinput");
    filterBox.focus();
    filterBox.value = "";
    typeInNetmonitor(value, monitor);
  }

  info("Starting test... ");

  const waitNetwork = waitForNetworkEvents(monitor, REQUESTS.length);
  await performRequestsInContent(REQUESTS);
  await waitNetwork;

  info(" > Test running flag once requests finish running");
  setFreetextFilter("is:running");
  await testContents([0, 0, 0, 0, 0, 0, 0, 0, 0, 0]);

  info(" > Test cached flag");
  setFreetextFilter("is:from-cache");
  await testContents([0, 0, 0, 0, 0, 0, 0, 0, 0, 1]);

  setFreetextFilter("is:cached");
  await testContents([0, 0, 0, 0, 0, 0, 0, 0, 0, 1]);

  info(" > Test negative cached flag");
  setFreetextFilter("-is:from-cache");
  await testContents([1, 1, 1, 1, 1, 1, 1, 1, 1, 0]);

  setFreetextFilter("-is:cached");
  await testContents([1, 1, 1, 1, 1, 1, 1, 1, 1, 0]);

  info(" > Test status-code flag");
  setFreetextFilter("status-code:200");
  await testContents([1, 1, 1, 1, 1, 1, 1, 1, 1, 0]);

  info(" > Test status-code negative flag");
  setFreetextFilter("-status-code:200");
  await testContents([0, 0, 0, 0, 0, 0, 0, 0, 0, 1]);

  info(" > Test mime-type flag");
  setFreetextFilter("mime-type:HtmL");
  await testContents([1, 1, 0, 0, 0, 0, 0, 0, 0, 0]);

  info(" > Test mime-type negative flag");
  setFreetextFilter("-mime-type:HtmL");
  await testContents([0, 0, 1, 1, 1, 1, 1, 1, 1, 1]);

  info(" > Test method flag");
  setFreetextFilter("method:get");
  await testContents([1, 1, 1, 1, 1, 1, 1, 1, 1, 1]);

  info(" > Test unmatched method flag");
  setFreetextFilter("method:post");
  await testContents([0, 0, 0, 0, 0, 0, 0, 0, 0, 0]);

  info(" > Test scheme flag (all requests are http)");
  setFreetextFilter("scheme:http");
  await testContents([1, 1, 1, 1, 1, 1, 1, 1, 1, 1]);

  setFreetextFilter("scheme:https");
  await testContents([0, 0, 0, 0, 0, 0, 0, 0, 0, 0]);

  info(" > Test regex filter");
  setFreetextFilter("regexp:content.*?Sam");
  await testContents([1, 1, 0, 0, 0, 0, 0, 0, 0, 0]);

  info(" > Test set-cookie-name flag");
  setFreetextFilter("set-cookie-name:name2");
  await testContents([0, 1, 0, 0, 0, 0, 0, 0, 0, 0]);

  setFreetextFilter("set-cookie-name:not-existing");
  await testContents([0, 0, 0, 0, 0, 0, 0, 0, 0, 0]);

  info(" > Test set-cookie-value flag");
  setFreetextFilter("set-cookie-value:value2");
  await testContents([0, 1, 0, 0, 0, 0, 0, 0, 0, 0]);

  setFreetextFilter("set-cookie-value:not-existing");
  await testContents([0, 0, 0, 0, 0, 0, 0, 0, 0, 0]);

  info(" > Test set-cookie-domain flag");
  setFreetextFilter("set-cookie-domain:.example.com");
  await testContents([0, 1, 0, 0, 0, 0, 0, 0, 0, 0]);

  setFreetextFilter("set-cookie-domain:.foo.example.com");
  await testContents([0, 1, 0, 0, 0, 0, 0, 0, 0, 0]);

  setFreetextFilter("set-cookie-domain:.not-existing.example.com");
  await testContents([0, 0, 0, 0, 0, 0, 0, 0, 0, 0]);

  info(" > Test size");
  setFreetextFilter("size:-1");
  await testContents([0, 0, 0, 0, 0, 0, 0, 0, 0, 0]);

  setFreetextFilter("size:0");
  await testContents([0, 0, 0, 0, 1, 1, 1, 1, 0, 1]);

  setFreetextFilter("size:34");
  await testContents([0, 0, 1, 1, 0, 0, 0, 0, 0, 0]);

  setFreetextFilter("size:0kb");
  await testContents([0, 0, 0, 0, 0, 0, 0, 0, 0, 0]);

  info(" > Testing the lower bound");
  setFreetextFilter("size:9.659k");
  await testContents([0, 0, 0, 0, 0, 0, 0, 0, 0, 0]);

  info(" > Testing the actual value");
  setFreetextFilter("size:10989");
  await testContents([0, 0, 0, 0, 0, 0, 0, 0, 1, 0]);

  info(" > Testing the upper bound");
  setFreetextFilter("size:11.804k");
  await testContents([0, 0, 0, 0, 0, 0, 0, 0, 1, 0]);

  info(" > Test transferred");
  setFreetextFilter("transferred:200");
  await testContents([0, 0, 0, 0, 1, 1, 1, 1, 0, 0]);

  setFreetextFilter("transferred:234");
  await testContents([1, 0, 1, 0, 0, 0, 0, 0, 0, 1]);

  setFreetextFilter("transferred:248");
  await testContents([0, 0, 1, 1, 0, 0, 0, 0, 0, 1]);

  setFreetextFilter("transferred:0kb");
  await testContents([0, 0, 0, 0, 0, 0, 0, 0, 0, 0]);

  info(" > Test larger-than");
  setFreetextFilter("larger-than:-1");
  await testContents([1, 1, 1, 1, 1, 1, 1, 1, 1, 1]);

  setFreetextFilter("larger-than:0");
  await testContents([1, 1, 1, 1, 0, 0, 0, 0, 1, 0]);

  setFreetextFilter("larger-than:33");
  await testContents([0, 0, 1, 1, 0, 0, 0, 0, 1, 0]);

  setFreetextFilter("larger-than:34");
  await testContents([0, 0, 0, 0, 0, 0, 0, 0, 1, 0]);

  setFreetextFilter("larger-than:10.73k");
  await testContents([0, 0, 0, 0, 0, 0, 0, 0, 1, 0]);

  setFreetextFilter("larger-than:10.732k");
  await testContents([0, 0, 0, 0, 0, 0, 0, 0, 1, 0]);

  setFreetextFilter("larger-than:0kb");
  await testContents([0, 0, 0, 0, 0, 0, 0, 0, 0, 0]);

  info(" > Test transferred-larger-than");
  setFreetextFilter("transferred-larger-than:-1");
  await testContents([1, 1, 1, 1, 1, 1, 1, 1, 1, 1]);

  setFreetextFilter("transferred-larger-than:214");
  await testContents([1, 1, 1, 1, 0, 0, 0, 0, 1, 1]);

  setFreetextFilter("transferred-larger-than:247");
  await testContents([0, 1, 1, 1, 0, 0, 0, 0, 1, 0]);

  setFreetextFilter("transferred-larger-than:248");
  await testContents([0, 1, 0, 1, 0, 0, 0, 0, 1, 0]);

  setFreetextFilter("transferred-larger-than:10.73k");
  await testContents([0, 0, 0, 0, 0, 0, 0, 0, 0, 0]);

  setFreetextFilter("transferred-larger-than:0kb");
  await testContents([0, 0, 0, 0, 0, 0, 0, 0, 0, 0]);

  info(" > Test cause");
  setFreetextFilter("cause:xhr");
  await testContents([1, 1, 1, 1, 1, 1, 1, 1, 1, 1]);

  setFreetextFilter("cause:script");
  await testContents([0, 0, 0, 0, 0, 0, 0, 0, 0, 0]);

  info(" > Test has-response-header");
  setFreetextFilter("has-response-header:Content-Type");
  await testContents([1, 1, 1, 1, 1, 1, 1, 1, 1, 1], true);

  setFreetextFilter("has-response-header:Last-Modified");
  await testContents([0, 0, 0, 0, 0, 0, 0, 0, 0, 0]);

  info(" > Test remote-ip");
  setFreetextFilter("remote-ip:127.0.0.1");
  await testContents([1, 1, 1, 1, 1, 1, 1, 1, 1, 1]);

  setFreetextFilter("remote-ip:192.168.1.2");
  await testContents([0, 0, 0, 0, 0, 0, 0, 0, 0, 0]);

  info(" > Test domain");
  setFreetextFilter("domain:example.com");
  await testContents([1, 1, 1, 1, 1, 1, 1, 1, 1, 1]);

  setFreetextFilter("domain:wrongexample.com");
  await testContents([0, 0, 0, 0, 0, 0, 0, 0, 0, 0]);

  info(" > Test protocol");
  setFreetextFilter("protocol:http/1");
  await testContents([1, 1, 1, 1, 1, 1, 1, 1, 1, 1]);

  setFreetextFilter("protocol:http/2");
  await testContents([0, 0, 0, 0, 0, 0, 0, 0, 0, 0]);

  info(" > Test mixing flags");
  setFreetextFilter("-mime-type:HtmL status-code:200");
  await testContents([0, 0, 1, 1, 1, 1, 1, 1, 1, 0]);

  await teardown(monitor);

  async function testContents(visibility, waitForAllResponseHeaders = false) {
    if (waitForAllResponseHeaders) {
      await waitForRequestDataForAllRequests(monitor, ["responseHeaders"]);
    }

    const items = getSortedRequests(store.getState());
    let visibleItems = getDisplayedRequests(store.getState());

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
      let isThere = visibleItems.some(r => r.id == itemId);

      // Filter results will be updated asynchronously, so we should wait until
      // displayed requests reach final state.
      await waitUntil(() => {
        visibleItems = getDisplayedRequests(store.getState());
        isThere = visibleItems.some(r => r.id == itemId);
        return isThere === shouldBeVisible;
      });

      is(
        isThere,
        shouldBeVisible,
        `The item at index ${i} has visibility=${shouldBeVisible}`
      );
    }

    // Fake mouse over the status column only after the list is fully updated
    const requestItems = document.querySelectorAll(".request-list-item");
    for (const requestItem of requestItems) {
      requestItem.scrollIntoView();
      const requestsListStatus = requestItem.querySelector(".status-code");
      EventUtils.sendMouseEvent({ type: "mouseover" }, requestsListStatus);
      await waitUntil(() => requestsListStatus.title);
      await waitForDOM(requestItem, ".requests-list-timings-total");
    }

    for (let i = 0; i < visibility.length; i++) {
      const shouldBeVisible = !!visibility[i];

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

function waitForRequestDataForAllRequests(monitor, fields) {
  const { store, connector, windowRequire } = monitor.panelWin;
  const { getSortedRequests } = windowRequire(
    "devtools/client/netmonitor/src/selectors/index"
  );

  return waitUntil(async () => {
    const requests = getSortedRequests(store.getState());
    for (const request of requests) {
      if (!request) {
        return false;
      }
      for (const field of fields) {
        if (request[`${field}Available`] && !request[field]) {
          await connector.requestData(request.id, field);
        }
        if (request[field] == undefined) {
          return false;
        }
      }
    }
    return true;
  });
}
