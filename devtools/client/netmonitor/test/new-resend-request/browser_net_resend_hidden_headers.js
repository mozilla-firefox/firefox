/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Test that custom request headers are sent even without clicking on the original request (bug 1583397)
 */

add_task(async function () {
  const { monitor } = await initNetMonitor(HTTPS_SIMPLE_URL, {
    requestCount: 1,
  });
  info("Starting test... ");

  const { store, windowRequire, connector } = monitor.panelWin;
  const Actions = windowRequire("devtools/client/netmonitor/src/actions/index");

  const { getSortedRequests } = windowRequire(
    "devtools/client/netmonitor/src/selectors/index"
  );

  store.dispatch(Actions.batchEnable(false));

  const requestUrl = HTTPS_SIMPLE_SJS;
  const requestHeaders = [
    { name: "Accept", value: "application/vnd.example+json" },
  ];

  const originalRequest = waitForNetworkEvents(monitor, 1);
  connector.networkCommand.sendHTTPRequest({
    url: requestUrl,
    method: "GET",
    headers: requestHeaders,
    cause: {
      loadingDocumentUri: "https://example.com",
      stacktraceAvailable: true,
      type: "xhr",
    },
  });
  await originalRequest;

  info("Sent original request");

  const originalItem = getSortedRequests(store.getState())[0];

  store.dispatch(Actions.cloneRequest(originalItem.id));

  const clonedRequest = waitForNetworkEvents(monitor, 1);

  store.dispatch(Actions.sendCustomRequest(originalItem.id));

  await clonedRequest;

  info("Resent request");

  let clonedItem = getSortedRequests(store.getState())[1];

  await waitForRequestData(monitor, ["requestHeaders"], clonedItem.id);

  clonedItem = getSortedRequests(store.getState())[1];

  for (const { name, value } of clonedItem.requestHeaders.headers) {
    info(`Request header: ${name}: ${value}`);
  }

  function hasRequestHeader(name, value) {
    const { headers } = clonedItem.requestHeaders;
    return headers.some(h => h.name === name && h.value === value);
  }

  function hasNotRequestHeader(name) {
    const { headers } = clonedItem.requestHeaders;
    return headers.every(h => h.name !== name);
  }

  for (const { name, value } of requestHeaders) {
    ok(hasRequestHeader(name, value), `The ${name} header has the right value`);
  }

  // Check that the Cookie header was not added silently (i.e., that the request is
  // anonymous.
  for (const name of ["Cookie"]) {
    ok(hasNotRequestHeader(name), `The ${name} header is not present`);
  }

  return teardown(monitor);
});
