/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

// Network throttling test: check that disabling throttling allows previously
// blocked requests to go back to unthrottled and complete quickly.

"use strict";

const httpServer = createTestHTTPServer();
httpServer.registerPathHandler(`/`, function (request, response) {
  response.setStatusLine(request.httpVersion, 200, "OK");
  response.write(`<meta charset=utf8><h1>Test throttling profiles</h1>`);
});

// The "data" path takes a size query parameter and will return a body of the
// requested size.
httpServer.registerPathHandler("/data", function (request, response) {
  const size = request.queryString.match(/size=(\d+)/)[1];
  response.setHeader("Content-Type", "text/plain");

  response.setStatusLine(request.httpVersion, 200, "OK");
  const body = new Array(size * 1).join("a");
  response.bodyOutputStream.write(body, body.length);
});

const TEST_URI = `http://localhost:${httpServer.identity.primaryPort}/`;

add_task(async function () {
  const { monitor } = await initNetMonitor(TEST_URI, { requestCount: 1 });
  const { store, windowRequire, connector } = monitor.panelWin;
  const { updateNetworkThrottling } = connector;
  const { getSortedRequests } = windowRequire(
    "devtools/client/netmonitor/src/selectors/index"
  );

  const throttleProfile = {
    latency: 100,
    download: 1,
    upload: 10000,
  };

  info("Enable very slow throttling");
  await updateNetworkThrottling(true, throttleProfile);

  // Start waiting for 2 network events.
  const onNetworkEvents = waitForNetworkEvents(monitor, 2);

  await SpecialPowers.spawn(gBrowser.selectedBrowser, [], () => {
    content.fetch("data?size=100");
  });

  info("Wait until the request is visible in the UI and then wait for 100ms");
  const throttledRequest = await waitFor(
    () => getSortedRequests(store.getState())[0]
  );
  await wait(100);
  ok(!throttledRequest.eventTimings, "Request is still not finished");

  info("Disable network throttling");
  await updateNetworkThrottling(false);
  await SpecialPowers.spawn(gBrowser.selectedBrowser, [], () => {
    content.fetch("data?size=100");
  });

  await waitForRequestData(monitor, ["eventTimings"]);
  // The throttled request should be unblocked after disabling throttling.
  await onNetworkEvents;

  const requestItem = getSortedRequests(store.getState())[1];
  Assert.less(
    requestItem.eventTimings.timings.receive,
    1000,
    "download reported as taking less than one second"
  );

  await teardown(monitor);
});
