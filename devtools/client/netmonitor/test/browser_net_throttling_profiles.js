/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

// Test all the network throttling profiles

"use strict";

requestLongerTimeout(2);

const {
  profiles,
} = require("resource://devtools/client/shared/components/throttling/profiles.js");

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
  await pushPref("devtools.cache.disabled", true);

  const { monitor } = await initNetMonitor(TEST_URI, { requestCount: 1 });
  const { store, connector, windowRequire } = monitor.panelWin;
  const { updateNetworkThrottling } = connector;

  const { getSortedRequests } = windowRequire(
    "devtools/client/netmonitor/src/selectors/index"
  );

  for (const profile of profiles) {
    info(`Starting test for throttling profile ${JSON.stringify(profile)}`);

    info("sending throttle request");
    await updateNetworkThrottling(true, profile);

    const onRequest = waitForNetworkEvents(monitor, 1);
    await SpecialPowers.spawn(gBrowser.selectedBrowser, [profile], _profile => {
      // Size must be greater than the profile download cap.
      const size = _profile.download * 2;
      content.fetch("data?size=" + size);
    });
    await onRequest;
    const requestItem = getSortedRequests(store.getState()).at(-1);

    if (profile.id !== "Offline") {
      // There will be no event timing data for the Offline profile.
      info(`Wait for eventTimings for throttling profile ${profile.id}`);
      await waitForRequestData(monitor, ["eventTimings"], requestItem.id);

      if (requestItem.eventTimings) {
        Assert.greater(
          requestItem.eventTimings.timings.receive,
          1000,
          `Request was properly throttled for profile ${profile.id}`
        );
      }
    }
  }

  await teardown(monitor);
});
