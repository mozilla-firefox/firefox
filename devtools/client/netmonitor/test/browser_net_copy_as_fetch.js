/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Tests if Copy as Fetch works.
 */

add_task(async function testBasicCopyAsFetch() {
  const { tab, monitor } = await initNetMonitor(HTTPS_CURL_URL, {
    requestCount: 1,
  });
  info("Starting test... ");

  // GET request, no cookies (first request)
  await performRequest("GET");
  await testClipboardContent(
    monitor,
    `await fetch("https://example.com/browser/devtools/client/netmonitor/test/sjs_simple-test-server.sjs", {
    "credentials": "omit",
    "headers": {
        "User-Agent": "${navigator.userAgent}",
        "Accept": "*/*",
        "Accept-Language": "en-US",
        "X-Custom-Header-1": "Custom value",
        "X-Custom-Header-2": "8.8.8.8",
        "X-Custom-Header-3": "Mon, 3 Mar 2014 11:11:11 GMT",
        "Sec-Fetch-Dest": "empty",
        "Sec-Fetch-Mode": "cors",
        "Sec-Fetch-Site": "same-origin",
        "Priority": "i",
        "Pragma": "no-cache",
        "Cache-Control": "no-cache"
    },
    "referrer": "https://example.com/browser/devtools/client/netmonitor/test/html_copy-as-curl.html",
    "method": "GET",
    "mode": "cors"
});`
  );

  await teardown(monitor);

  async function performRequest(method, payload) {
    const waitRequest = waitForNetworkEvents(monitor, 1);
    await SpecialPowers.spawn(
      tab.linkedBrowser,
      [
        {
          url: HTTPS_SIMPLE_SJS,
          method_: method,
          payload_: payload,
        },
      ],
      async function ({ url, method_, payload_ }) {
        content.wrappedJSObject.performRequest(url, method_, payload_);
      }
    );
    await waitRequest;
  }
});

/**
 * Tests for Url escaping of copy as Fetch
 */
add_task(async function testUrlEscapeOfCopyAsFetch() {
  const { monitor } = await initNetMonitor(HTTPS_CURL_URL, {
    requestCount: 1,
  });
  info("Starting test... ");

  const waitRequest = waitForNetworkEvents(monitor, 1);
  await SpecialPowers.spawn(
    gBrowser.selectedBrowser,
    ['data:text/html,"+alert(document.domain)+"'],
    url => {
      content.fetch(url);
    }
  );
  await waitRequest;

  await testClipboardContent(
    monitor,
    `await fetch("data:text/html,\\"+alert(document.domain)+\\"", {
    "credentials": "omit",
    "headers": {},
    "method": "GET",
    "mode": "cors"
});`
  );

  await teardown(monitor);
});

async function testClipboardContent(monitor, expectedResult) {
  const { document } = monitor.panelWin;

  const items = document.querySelectorAll(".request-list-item");
  clickOnRequestRow(items[items.length - 1]);
  EventUtils.sendMouseEvent(
    { type: "contextmenu" },
    document.querySelectorAll(".request-list-item")[0]
  );

  /* Ensure that the copy as fetch option is always visible */
  is(
    !!getContextMenuItem(monitor, "request-list-context-copy-as-fetch"),
    true,
    'The "Copy as Fetch" context menu item should not be hidden.'
  );

  await waitForClipboardPromise(
    async function setup() {
      await selectContextMenuItem(
        monitor,
        "request-list-context-copy-as-fetch"
      );
    },
    function validate(result) {
      if (typeof result !== "string") {
        return false;
      }
      return expectedResult === result;
    }
  );

  info("Clipboard contains a fetch command for item " + (items.length - 1));
}
