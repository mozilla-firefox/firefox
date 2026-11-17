/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Test the cache details panel for a revalidated document request.
 */
add_task(async function test_revalidated_document() {
  const body = "<html><body>Revalidated document</body></html>";
  const etag = '"cached-document"';
  const httpServer = createTestHTTPServer();
  httpServer.registerPathHandler("/", (request, response) => {
    response.setHeader("Content-Type", "text/html");
    response.write("<html>Cache test</html>");
  });
  httpServer.registerPathHandler("/cached-document", (request, response) => {
    response.setHeader("Content-Type", "text/html");
    response.setHeader("Cache-Control", "no-cache");
    response.setHeader("ETag", etag);
    if (request.hasHeader("If-None-Match")) {
      is(request.getHeader("If-None-Match"), etag, "The cached ETag is sent");
      response.setStatusLine(request.httpVersion, 304, "Not Modified");
    } else {
      response.setStatusLine(request.httpVersion, 200, "OK");
      response.write(body);
    }
  });

  const url = `http://localhost:${httpServer.identity.primaryPort}/`;
  const { monitor } = await initNetMonitor(url, {
    enableCache: true,
    requestCount: 1,
  });
  const { store, connector, windowRequire } = monitor.panelWin;
  const { getSortedRequests, getRequestById } = windowRequire(
    "devtools/client/netmonitor/src/selectors/index"
  );

  // Bug 2077785: Make sure that fetching the cache entry for the 304 request
  // several times does not timeout.
  for (const status of ["200", "304", "304", "304"]) {
    info(`Navigate to the cached document, expecting ${status}`);
    const onNetworkEvents = waitForNetworkEvents(monitor, 1);
    await navigateTo(url + "cached-document");
    await onNetworkEvents;

    const requests = getSortedRequests(store.getState());
    is(requests.length, 1, "One document request completed");
    const request = requests[0];
    is(request.status, status, "The document has the expected response status");

    await connector.requestData(request.id, "responseContent");
    await waitForRequestData(monitor, ["responseContent"], request.id);
    is(
      getRequestById(store.getState(), request.id).responseContent.content.text,
      body,
      "The response body is available after the document request completes"
    );

    if (status === "304") {
      const { cache } = await connector.requestData(
        request.id,
        "responseCache"
      );
      is(
        cache.storageDataSize,
        body.length,
        "Cache details describe the revalidated document"
      );
    }
  }
});
