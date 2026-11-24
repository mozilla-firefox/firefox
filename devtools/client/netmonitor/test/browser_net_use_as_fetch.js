/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Tests if Use as Fetch works.
 */

add_task(async function () {
  const { tab, monitor, toolbox } = await initNetMonitor(CURL_URL, {
    requestCount: 1,
  });
  info("Starting test... ");

  // GET request, no cookies (first request)
  await performRequest("GET");
  await testConsoleInput(`await fetch("http://example.com/browser/devtools/client/netmonitor/test/sjs_simple-test-server.sjs", {
    "credentials": "omit",
    "headers": {
        "User-Agent": "${navigator.userAgent}",
        "Accept": "*/*",
        "Accept-Language": "en-US",
        "X-Custom-Header-1": "Custom value",
        "X-Custom-Header-2": "8.8.8.8",
        "X-Custom-Header-3": "Mon, 3 Mar 2014 11:11:11 GMT",
        "Priority": "i",
        "Pragma": "no-cache",
        "Cache-Control": "no-cache"
    },
    "referrer": "http://example.com/browser/devtools/client/netmonitor/test/html_copy-as-curl.html",
    "method": "GET",
    "mode": "cors"
});`);

  await teardown(monitor);

  async function performRequest(method, payload) {
    const waitRequest = waitForNetworkEvents(monitor, 1);
    await SpecialPowers.spawn(
      tab.linkedBrowser,
      [
        {
          url: SIMPLE_SJS,
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

  async function testConsoleInput(expectedResult) {
    const { document } = monitor.panelWin;

    const items = document.querySelectorAll(".request-list-item");
    clickOnRequestRow(items[items.length - 1]);
    EventUtils.sendMouseEvent(
      { type: "contextmenu" },
      document.querySelectorAll(".request-list-item")[0]
    );

    /* Ensure that the use as fetch option is always visible */
    is(
      !!getContextMenuItem(monitor, "request-list-context-use-as-fetch"),
      true,
      'The "Use as Fetch" context menu item should not be hidden.'
    );

    const split = toolbox.once("split-console");
    await selectContextMenuItem(monitor, "request-list-context-use-as-fetch");

    await split;
    const hud = toolbox.getPanel("webconsole").hud;
    await hud.jsterm.once("set-input-value");

    is(
      hud.getInputValue(),
      expectedResult,
      "Console input contains fetch request for item " + (items.length - 1)
    );
  }
});
