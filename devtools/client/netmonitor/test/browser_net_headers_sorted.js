/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Tests if Request-Headers and Response-Headers are sorted in Headers tab.
 * The test also verifies that headers with the same name and headers
 * with an empty value are also displayed.
 *
 * The test also checks that raw headers are displayed in the original
 * order and not sorted.
 */
add_task(async function () {
  const { monitor } = await initNetMonitor(HTTPS_SIMPLE_SJS, {
    requestCount: 1,
  });
  info("Starting test... ");

  const { store, windowRequire } = monitor.panelWin;
  const Actions = windowRequire("devtools/client/netmonitor/src/actions/index");
  store.dispatch(Actions.batchEnable(false));

  const onNetworkEvents = waitForNetworkEvents(monitor, 1);
  await reloadSelectedTab();
  await onNetworkEvents;

  // Verify request and response headers.
  await verifyHeaders(monitor);
  await verifyRawHeaders(monitor);

  // Clean up
  await teardown(monitor);
});

async function verifyHeaders(monitor) {
  const { document, store } = monitor.panelWin;

  info("Check if Request-Headers and Response-Headers are sorted");

  const onDomUpdated = waitForDOM(document, ".headers-overview");
  clickOnRequestRow(document.querySelectorAll(".request-list-item")[0]);
  await onDomUpdated;

  await waitForRequestData(store, ["requestHeaders", "responseHeaders"]);

  const expectedResponseHeaders = [
    "cache-control",
    "connection",
    "content-length",
    "content-type",
    "date",
    "expires",
    "foo-bar",
    "foo-bar",
    "foo-bar",
    "no-vary-search",
    "pragma",
    "server",
    "set-cookie",
    "set-cookie",
  ];
  const expectedRequestHeaders = [
    "Accept",
    "Accept-Encoding",
    "Accept-Language",
    "Cache-Control",
    "Connection",
    "Cookie",
    "Host",
    "Pragma",
    "Priority",
    "Sec-Fetch-Dest",
    "Sec-Fetch-Mode",
    "Sec-Fetch-Site",
    "Upgrade-Insecure-Requests",
    "User-Agent",
  ];

  const responseLabelCells = document.querySelectorAll(
    "#responseHeaders .treeLabelCell"
  );
  const requestLabelCells = document.querySelectorAll(
    "#requestHeaders .treeLabelCell"
  );
  const actualResponseHeaders = [];
  const actualRequestHeaders = [];

  for (let i = 0; i < responseLabelCells.length; i++) {
    actualResponseHeaders.push(responseLabelCells[i].innerText);
  }

  for (let i = 0; i < requestLabelCells.length; i++) {
    actualRequestHeaders.push(requestLabelCells[i].innerText);
  }

  is(
    actualResponseHeaders.toString(),
    expectedResponseHeaders.toString(),
    "Response Headers are sorted"
  );

  is(
    actualRequestHeaders.toString(),
    expectedRequestHeaders.toString(),
    "Request Headers are sorted"
  );
}

async function verifyRawHeaders(monitor) {
  const { document } = monitor.panelWin;

  info("Check if raw Request-Headers and raw Response-Headers are not sorted");

  const actualResponseHeaders = [];
  const actualRequestHeaders = [];

  const expectedResponseHeaders = [
    "cache-control",
    "pragma",
    "expires",
    "no-vary-search",
    "set-cookie",
    "set-cookie",
    "content-type",
    "foo-bar",
    "foo-bar",
    "foo-bar",
    "connection",
    "server",
    "date",
    "content-length",
  ];

  const expectedRequestHeaders = [
    "Host",
    "User-Agent",
    "Accept",
    "Accept-Language",
    "Accept-Encoding",
    "Connection",
    "Cookie",
    "Upgrade-Insecure-Requests",
    "Sec-Fetch-Dest",
    "Sec-Fetch-Mode",
    "Sec-Fetch-Site",
    "Priority",
    "Pragma",
    "Cache-Control",
  ];

  // Click the 'Raw headers' toggle to show original headers source.
  for (const rawToggleInput of document.querySelectorAll(
    ".devtools-checkbox-toggle"
  )) {
    rawToggleInput.click();
  }

  // Wait till raw headers are available.
  let rawArr;
  await waitUntil(() => {
    rawArr = document.querySelectorAll("textarea.raw-headers");
    // Both raw headers must be present
    return rawArr.length > 1;
  });

  // Request headers are rendered first, so it is element with index 1
  const requestHeadersText = rawArr[1].textContent;
  // Response headers are rendered first, so it is element with index 0
  const responseHeadersText = rawArr[0].textContent;

  const rawRequestHeadersArray = requestHeadersText.split("\n");
  for (let i = 1; i < rawRequestHeadersArray.length; i++) {
    const header = rawRequestHeadersArray[i];
    actualRequestHeaders.push(header.split(":")[0]);
  }

  const rawResponseHeadersArray = responseHeadersText.split("\n");
  for (let i = 1; i < rawResponseHeadersArray.length; i++) {
    const header = rawResponseHeadersArray[i];
    actualResponseHeaders.push(header.split(":")[0]);
  }

  is(
    actualResponseHeaders.toString(),
    expectedResponseHeaders.toString(),
    "Raw Response Headers are not sorted"
  );

  is(
    actualRequestHeaders.toString(),
    expectedRequestHeaders.toString(),
    "Raw Request Headers are not sorted"
  );

  // Use keyboard to uncheck the toggle so we test Bug 1917296
  for (const rawToggleInput of document.querySelectorAll(
    ".devtools-checkbox-toggle"
  )) {
    ok(rawToggleInput.checked, "Toggle is checked");
    rawToggleInput.focus();
    EventUtils.synthesizeKey("VK_SPACE", {}, rawToggleInput.documentGlobal);
  }

  // Wait till raw headers are not available anymore.
  await waitUntil(() => !document.querySelector("textarea.raw-headers"));
  ok(true, "Raw headers are hidden");
  for (const rawToggleInput of document.querySelectorAll(
    ".devtools-checkbox-toggle"
  )) {
    ok(!rawToggleInput.checked, "Toggle is unchecked");
  }
}
