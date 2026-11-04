/* Any copyright is dedicated to the Public Domain.
  http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Test basic request blocking functionality for patterns
 * Ensures that request blocking unblocks a relevant pattern and not just
 * an exact URL match
 */

add_task(async function () {
  await pushPref("devtools.netmonitor.features.requestBlocking", true);

  const { tab, monitor } = await initNetMonitor(HTTPS_CUSTOM_GET_URL, {
    requestCount: 1,
  });
  info("Starting test... ");

  const { document, store, windowRequire } = monitor.panelWin;

  // Action should be processed synchronously in tests
  const Actions = windowRequire("devtools/client/netmonitor/src/actions/index");
  store.dispatch(Actions.batchEnable(false));

  // Open the request blocking panel
  store.dispatch(Actions.toggleRequestBlockingPanel());

  // Add patterns which should block some of the requests
  await addBlockedRequest("test1", monitor);
  await addBlockedRequest("test/*/test3", monitor);

  // Close the blocking panel to ensure it's opened by the context menu later
  store.dispatch(Actions.toggleRequestBlockingPanel());

  // Execute two XHRs (the same URL) and wait till they're finished
  const TEST_URL_1 = HTTPS_SEARCH_SJS + "?value=test1";
  const TEST_URL_2 = HTTPS_SEARCH_SJS + "?value=test2";
  const TEST_URL_3 = HTTPS_SEARCH_SJS + "test/something/test3";
  const TEST_URL_4 = HTTPS_SEARCH_SJS + "test/something/test4";

  let onNetworkEvents = waitForNetworkEvents(monitor, 4);
  await SpecialPowers.spawn(
    tab.linkedBrowser,
    [TEST_URL_1],
    async function (url) {
      content.wrappedJSObject.performRequests(1, url);
    }
  );
  await SpecialPowers.spawn(
    tab.linkedBrowser,
    [TEST_URL_2],
    async function (url) {
      content.wrappedJSObject.performRequests(1, url);
    }
  );
  await SpecialPowers.spawn(
    tab.linkedBrowser,
    [TEST_URL_3],
    async function (url) {
      content.wrappedJSObject.performRequests(1, url);
    }
  );
  await SpecialPowers.spawn(
    tab.linkedBrowser,
    [TEST_URL_4],
    async function (url) {
      content.wrappedJSObject.performRequests(1, url);
    }
  );
  await onNetworkEvents;

  // Wait till there are four resources rendered in the results
  await waitForDOM(document, ".request-list-item", 4);

  let requestItems = document.querySelectorAll(".request-list-item");
  // Ensure that test1 item was blocked and test2 item wasn't
  ok(
    checkRequestListItemBlocked(requestItems[0]),
    "The first request was blocked"
  );
  ok(
    !checkRequestListItemBlocked(requestItems[1]),
    "The second request was not blocked"
  );
  // Ensure that test3 item was blocked and test4 item wasn't
  ok(
    checkRequestListItemBlocked(requestItems[2]),
    "The third request was blocked"
  );
  ok(
    !checkRequestListItemBlocked(requestItems[3]),
    "The fourth request was not blocked"
  );

  clickOnRequestRow(requestItems[0]);
  // Right-click test1, select "Unblock URL" from its context menu
  await toggleBlockedUrl(requestItems[0], monitor, store, "unblock");

  // Ensure that the request blocking panel is now open and the item is unchecked
  ok(
    !document.querySelector(".request-blocking-list .devtools-checkbox")
      .checked,
    "The blocking pattern is disabled from context menu"
  );

  // Request the unblocked URL again, ensure the URL was not blocked
  onNetworkEvents = waitForNetworkEvents(monitor, 1);
  await SpecialPowers.spawn(
    tab.linkedBrowser,
    [TEST_URL_1],
    async function (url) {
      content.wrappedJSObject.performRequests(1, url);
    }
  );
  await onNetworkEvents;

  await waitForDOM(document, ".request-list-item", 5);
  requestItems = document.querySelectorAll(".request-list-item");
  ok(
    !checkRequestListItemBlocked(requestItems[4]),
    "The fifth request was not blocked"
  );

  await teardown(monitor);
});
