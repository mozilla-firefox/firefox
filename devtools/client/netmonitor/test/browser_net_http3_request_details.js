/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Tests request details with HTTP/3
 */

add_task(async function () {
  const { monitor } = await initNetMonitor(HTTPS_SIMPLE_SJS, {
    requestCount: 1,
  });
  info("Starting test... ");

  const { document, store, windowRequire } = monitor.panelWin;
  const Actions = windowRequire("devtools/client/netmonitor/src/actions/index");

  store.dispatch(Actions.batchEnable(false));

  const onNetworkEvents = waitForNetworkEvents(monitor, 1);
  await reloadSelectedTab();
  await onNetworkEvents;

  const waitForHeaders = waitForDOM(document, ".headers-overview");
  store.dispatch(Actions.toggleNetworkDetails());
  await waitForHeaders;

  info("Assert the content of the headers");

  const tabpanel = document.querySelector("#headers-panel");
  // Request URL
  is(
    tabpanel.querySelector(".url-preview .url").innerText,
    HTTPS_SIMPLE_SJS,
    "The url summary value is incorrect."
  );

  // Request method
  is(
    tabpanel.querySelectorAll(".treeLabel")[0].innerText,
    "GET",
    "The method summary value is incorrect."
  );
  // Status code
  is(
    tabpanel.querySelector(".requests-list-status-code").innerText,
    "200",
    "The status summary code is incorrect."
  );
  is(
    tabpanel.querySelector(".status").childNodes[1].textContent,
    "", // HTTP/2 and 3 send no status text, only a code
    "The status summary value is incorrect."
  );
  // Version
  is(
    tabpanel.querySelectorAll(".tabpanel-summary-value")[1].innerText,
    "HTTP/3",
    "The HTTP version is incorrect."
  );

  await waitForRequestData(store, ["requestHeaders", "responseHeaders"]);

  is(
    tabpanel.querySelectorAll(".accordion-item").length,
    2,
    "There should be 2 header scopes displayed in this tabpanel."
  );

  const headers = [...tabpanel.querySelectorAll(".accordion .treeLabelCell")];

  is(
    // The Text-Encoding header is not consistently displayed, exclude it from
    // the assertion. See Bug 1830053.
    headers.filter(cell => cell.textContent != "TE").length,
    27,
    "There should be 27 header values displayed in this tabpanel."
  );

  const headersTable = tabpanel.querySelector(".accordion");
  const responseHeaders = headersTable.querySelectorAll(
    "tr[id^='/responseHeaders']"
  );

  const expectedHeaders = [
    {
      name: "cache-control",
      value: "no-cache, no-store, must-revalidate",
      index: 0,
    },
    {
      name: "content-length",
      value: "12",
      index: 1,
    },
    {
      name: "content-type",
      value: "text/plain; charset=utf-8",
      index: 2,
    },
    {
      name: "foo-bar",
      value: "baz",
      index: 6,
    },
  ];
  expectedHeaders.forEach(header => {
    is(
      responseHeaders[header.index].querySelector(".treeLabel").innerHTML,
      header.name,
      `The response header at index ${header.index} name was incorrect.`
    );
    is(
      responseHeaders[header.index].querySelector(".objectBox").innerHTML,
      `${header.value}`,
      `The response header at index ${header.index} value was incorrect.`
    );
  });

  info("Assert the content of the raw headers");

  // Click the 'Raw headers' toggle to show original headers source.
  document.querySelector("#raw-requestHeaders-checkbox").click();
  document.querySelector("#raw-responseHeaders-checkbox").click();

  let rawHeadersElements;
  await waitUntil(() => {
    rawHeadersElements = document.querySelectorAll("textarea.raw-headers");
    // Both raw headers must be present
    return rawHeadersElements.length > 1;
  });
  const requestHeadersText = rawHeadersElements[1].textContent;
  const rawRequestHeaderFirstLine = requestHeadersText.split(/\r\n|\n|\r/)[0];
  is(
    rawRequestHeaderFirstLine,
    "GET /browser/devtools/client/netmonitor/test/sjs_simple-test-server.sjs HTTP/3"
  );

  const responseHeadersText = rawHeadersElements[0].textContent;
  const rawResponseHeaderFirstLine = responseHeadersText.split(/\r\n|\n|\r/)[0];
  is(rawResponseHeaderFirstLine, "HTTP/3 200 "); // H2/3 send no status text

  info("Assert the content of the protocol column");
  const target = document.querySelectorAll(".request-list-item")[0];
  is(
    target.querySelector(".requests-list-protocol").textContent,
    "HTTP/3",
    "The displayed protocol is correct."
  );

  return teardown(monitor);
});
