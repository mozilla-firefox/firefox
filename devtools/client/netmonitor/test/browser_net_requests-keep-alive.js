/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Tests that keep alive requests are displayed in the netmonitor

function setupServer() {
  const httpServer = createTestHTTPServer();
  httpServer.registerContentType("html", "text/html");
  httpServer.registerPathHandler("/keep-alive", function (request, response) {
    response.setStatusLine(request.httpVersion, 200, "OK");
    response.write(
      `<!DOCTYPE html>
      <html>
        <body>Test keep alive requests</body>
      </html>
    `
    );
  });
  return httpServer;
}

add_task(async function () {
  const httpServer = setupServer();
  const port = httpServer.identity.primaryPort;
  const ORIGIN = `http://localhost:${port}/`;
  const TEST_URL = `${ORIGIN}/keep-alive`;
  const { monitor, tab } = await initNetMonitor(ORIGIN, {
    requestCount: 1,
  });
  info("Starting test... ");

  const { document, store, windowRequire } = monitor.panelWin;
  const Actions = windowRequire("devtools/client/netmonitor/src/actions/index");

  store.dispatch(Actions.batchEnable(false));

  const onNetworkEvents = waitForNetworkEvents(monitor, 1);
  await SpecialPowers.spawn(
    tab.linkedBrowser,
    [`${ORIGIN}/keep-alive`],
    async url => {
      await content.wrappedJSObject.fetch(url, {
        method: "POST",
        keepalive: true,
      });
    }
  );
  await onNetworkEvents;

  const firstItem = document.querySelectorAll(".request-list-item")[0];
  is(
    firstItem.querySelector(".requests-list-url").innerText,
    TEST_URL,
    "The url in the displayed request is correct"
  );

  info("Open the headers panel for the keep alive request");
  const onDomUpdated = waitForDOM(document, ".headers-overview");
  clickOnRequestRow(firstItem);
  await onDomUpdated;
  await waitForRequestData(store, ["requestHeaders"]);

  info("Check the connection header value");
  const requestHeaders = [
    ...document.querySelectorAll("#requestHeaders .treeRow.stringRow"),
  ];
  const connectionHeader = requestHeaders.find(
    el =>
      el.querySelector(".treeLabelCell span.treeLabel").innerText ==
      "Connection"
  );

  is(
    connectionHeader.querySelector(".treeValueCell span.objectBox-string")
      .innerText,
    "keep-alive",
    "The connection header value is keep-alive"
  );

  await teardown(monitor);
});
