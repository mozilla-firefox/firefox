/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Tests that the slow request indicator is visible for slow requests.
 */

add_task(async function () {
  // The script sjs_slow-script-server.sjs takes about 2s which is
  // definately above the slow threshold set here.
  const SLOW_THRESHOLD = 450;

  await pushPref("dom.security.https_first", false);

  Services.prefs.setIntPref("devtools.netmonitor.audits.slow", SLOW_THRESHOLD);

  const { monitor } = await initNetMonitor(SLOW_REQUESTS_URL, {
    requestCount: 2,
  });
  info("Starting test... ");

  const { document, store, windowRequire } = monitor.panelWin;
  const Actions = windowRequire("devtools/client/netmonitor/src/actions/index");
  store.dispatch(Actions.batchEnable(false));

  const onNetworkEvents = waitForNetworkEvents(monitor, 2);
  await reloadSelectedTab();
  await onNetworkEvents;

  const requestList = document.querySelectorAll(
    ".network-monitor .request-list-item"
  );

  info("Checking the html document request");
  is(
    requestList[0].querySelector(".requests-list-file div:first-child")
      .textContent,
    "html_slow-requests-test-page.html",
    "The html document is the first request"
  );
  is(
    !!requestList[0].querySelector(".requests-list-slow-button"),
    false,
    "The request is not slow"
  );

  info("Checking the slow script request");
  is(
    requestList[1].querySelector(".requests-list-file div:first-child")
      .textContent,
    "sjs_slow-script-server.sjs",
    "The slow test script is the second request"
  );
  is(
    !!requestList[1].querySelector(".requests-list-slow-button"),
    true,
    "The request is slow"
  );

  is(
    requestList[1]
      .querySelector(".requests-list-slow-button")
      .title.includes(`The recommended limit is ${SLOW_THRESHOLD} ms.`),
    true,
    "The tooltip text is correct"
  );

  return teardown(monitor);
});
