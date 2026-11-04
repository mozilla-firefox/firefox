/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Test if custom request headers are not ignored (bug 1270096 and friends)
 */

add_task(async function () {
  const { monitor } = await initNetMonitor(HTTPS_SIMPLE_SJS, {
    requestCount: 1,
  });
  info("Starting test... ");

  const { store, windowRequire, connector } = monitor.panelWin;
  const Actions = windowRequire("devtools/client/netmonitor/src/actions/index");
  const { requestData } = connector;
  const { getSortedRequests } = windowRequire(
    "devtools/client/netmonitor/src/selectors/index"
  );

  store.dispatch(Actions.batchEnable(false));

  const requestUrl = HTTPS_SIMPLE_SJS;
  const requestHeaders = [
    { name: "Host", value: "fakehost.example.com" },
    { name: "User-Agent", value: "Testzilla" },
    { name: "Referer", value: "https://example.com/referrer" },
    { name: "Accept", value: "application/jarda" },
    { name: "Accept-Encoding", value: "compress, identity, funcoding" },
    { name: "Accept-Language", value: "cs-CZ" },
  ];

  const onNetworkEvents = waitForNetworkEvents(monitor, 1);
  connector.networkCommand.sendHTTPRequest({
    url: requestUrl,
    method: "POST",
    headers: requestHeaders,
    body: "Hello",
    cause: {
      loadingDocumentUri: "https://example.com",
      stacktraceAvailable: true,
      type: "xhr",
    },
  });
  await onNetworkEvents;

  let item = getSortedRequests(store.getState())[0];

  ok(item.requestHeadersAvailable, "headers are available for lazily fetching");

  if (item.requestHeadersAvailable && !item.requestHeaders) {
    requestData(item.id, "requestHeaders");
  }

  // Wait until requestHeaders packet gets updated.
  await waitForRequestData(store, ["requestHeaders"]);

  item = getSortedRequests(store.getState())[0];
  is(item.method, "POST", "The request has the right method");
  is(item.url, requestUrl, "The request has the right URL");

  for (const { name, value } of item.requestHeaders.headers) {
    info(`Request header: ${name}: ${value}`);
  }

  function hasRequestHeader(name, value) {
    const { headers } = item.requestHeaders;
    return headers.some(h => h.name === name && h.value === value);
  }

  function hasNotRequestHeader(name) {
    const { headers } = item.requestHeaders;
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
