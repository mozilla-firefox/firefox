/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Tests that the request for a 404 where no content is sent.
 */

function setupServer() {
  const httpServer = createTestHTTPServer();
  httpServer.registerContentType("html", "text/html");

  httpServer.registerPathHandler("/status", function (request, response) {
    response.setStatusLine(request.httpVersion, 404, "Not Found");
  });
  return httpServer;
}
add_task(async function () {
  Services.prefs.setBoolPref(
    "browser.http.blank_page_with_error_response.enabled",
    false
  );
  const httpServer = setupServer();
  const port = httpServer.identity.primaryPort;
  const URL = `http://localhost:${port}/status`;

  const { monitor } = await initNetMonitor(URL, {
    requestCount: 1,
    waitForLoad: false,
  });
  info("Starting test... ");

  const { document, store, windowRequire } = monitor.panelWin;
  const Actions = windowRequire("devtools/client/netmonitor/src/actions/index");

  store.dispatch(Actions.batchEnable(false));

  const onNetworkEvents = waitForNetworkEvents(monitor, 1, {
    expectedEventTimings: 1,
  });
  reloadSelectedTab({ waitForLoad: false });
  await onNetworkEvents;

  const firstItem = document.querySelectorAll(".request-list-item")[0];
  await waitUntil(
    () => firstItem.querySelector(".requests-list-status").innerText !== ""
  );
  is(
    firstItem.querySelector(".requests-list-url").innerText,
    URL,
    "The url in the displayed request is correct"
  );
  is(
    firstItem.querySelector(".requests-list-status").innerText,
    "404",
    "The request status code is correct"
  );

  await teardown(monitor);
});
