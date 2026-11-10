/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Tests if cached requests have the correct status code
 */

add_task(async function () {
  // performing http to https redirects, hence we do not
  // want https-first to interfere with that test
  await pushPref("dom.security.https_first", false);

  // The first batch of requests expects uncached responses, so don't leave
  // ours in the HTTP cache for a later run of this test.
  registerCleanupFunction(() => Services.cache2.clear());

  const { tab, monitor } = await initNetMonitor(STATUS_CODES_URL, {
    enableCache: true,
    requestCount: 1,
  });
  info("Starting test... ");

  const { document, store, windowRequire } = monitor.panelWin;
  const Actions = windowRequire("devtools/client/netmonitor/src/actions/index");
  const { getDisplayedRequests, getSortedRequests } = windowRequire(
    "devtools/client/netmonitor/src/selectors/index"
  );

  store.dispatch(Actions.batchEnable(false));

  const REQUEST_DATA = [
    {
      method: "GET",
      uri: STATUS_CODES_SJS + "?sts=ok&cached",
      details: {
        status: 200,
        statusText: "OK",
        type: "plain",
        fullMimeType: "text/plain; charset=utf-8",
      },
    },
    {
      method: "GET",
      uri: STATUS_CODES_SJS + "?sts=redirect&cached",
      details: {
        status: 301,
        statusText: "Moved Permanently",
        type: "html (redirect)",
        fullMimeType: "text/html; charset=utf-8",
      },
    },
    {
      method: "GET",
      uri: "http://example.com/redirected",
      details: {
        status: 404,
        statusText: "Not Found",
        type: "html",
        fullMimeType: "text/html; charset=utf-8",
      },
    },
    {
      method: "GET",
      uri: STATUS_CODES_SJS + "?sts=ok&cached",
      details: {
        status: 200,
        statusText: "OK (cached)",
        displayedStatus: "cached",
        type: "plain",
        fullMimeType: "text/plain; charset=utf-8",
      },
    },
    {
      method: "GET",
      uri: STATUS_CODES_SJS + "?sts=redirect&cached",
      details: {
        status: 301,
        statusText: "Moved Permanently (cached)",
        displayedStatus: "cached",
        type: "html (redirect)",
        fullMimeType: "text/html; charset=utf-8",
      },
    },
    {
      method: "GET",
      uri: "http://example.com/redirected",
      details: {
        status: 404,
        statusText: "Not Found",
        type: "html",
        fullMimeType: "text/html; charset=utf-8",
      },
    },
  ];

  // Cancel the 200 cached request, so that the test can also assert
  // that the NS_BINDING_ABORTED status is never displayed for cached requests.
  const observer = {
    QueryInterface: ChromeUtils.generateQI(["nsIObserver"]),
    observe(subject) {
      subject = subject.QueryInterface(Ci.nsIHttpChannel);
      if (subject.URI.spec == STATUS_CODES_SJS + "?sts=ok&cached") {
        subject.cancel(Cr.NS_BINDING_ABORTED);
        Services.obs.removeObserver(
          observer,
          "http-on-examine-cached-response"
        );
      }
    },
  };
  Services.obs.addObserver(observer, "http-on-examine-cached-response");

  info("Performing requests #1...");
  await performRequestsAndWait();

  info("Performing requests #2...");
  await performRequestsAndWait();

  await waitForAllNetworkUpdateEvents();

  let index = 0;
  for (const request of REQUEST_DATA) {
    const requestItem = document.querySelectorAll(".request-list-item")[index];
    requestItem.scrollIntoView();
    const requestsListStatus = requestItem.querySelector(".status-code");
    EventUtils.sendMouseEvent({ type: "mouseover" }, requestsListStatus);
    await waitUntil(() => requestsListStatus.title);
    await waitForDOM(requestItem, ".requests-list-timings-total");

    info("Verifying request #" + index);
    await verifyRequestItemTarget(
      document,
      getDisplayedRequests(store.getState()),
      getSortedRequests(store.getState())[index],
      request.method,
      request.uri,
      request.details
    );

    index++;
  }

  await teardown(monitor);

  async function performRequestsAndWait() {
    const onNetworkEvents = waitForNetworkEvents(monitor, 3);
    await SpecialPowers.spawn(tab.linkedBrowser, [], async function () {
      content.wrappedJSObject.performCachedRequests();
    });
    await onNetworkEvents;
  }
});
