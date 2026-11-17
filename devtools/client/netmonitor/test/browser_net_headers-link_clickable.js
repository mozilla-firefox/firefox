/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Test if links in headers panel are clickable.
 */

add_task(async function () {
  const { tab, monitor } = await initNetMonitor(JSON_LONG_URL, {
    requestCount: 1,
  });
  info("Starting test... ");

  const { document, store, windowRequire } = monitor.panelWin;
  const Actions = windowRequire("devtools/client/netmonitor/src/actions/index");

  store.dispatch(Actions.batchEnable(false));

  await performRequests(monitor, tab, 1);

  info("Selecting first request");
  clickOnRequestRow(document.querySelectorAll(".request-list-item")[0]);

  info("Waiting for request and response headers");
  await waitForRequestData(monitor, ["requestHeaders", "responseHeaders"]);

  const headerLink = document.querySelector(".objectBox-string .url");
  const expectedURL =
    "http://example.com/browser/devtools/client/netmonitor/test/html_json-long-test-page.html";
  const onTabOpen = BrowserTestUtils.waitForNewTab(gBrowser, expectedURL, true);

  info("Click on a first link in Headers panel");
  headerLink.click();
  await onTabOpen;

  ok(onTabOpen, "New tab opened from link");

  return teardown(monitor);
});
