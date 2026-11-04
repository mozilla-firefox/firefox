/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Tests that the request for the view-source page is displayed.
 */

add_task(async function () {
  const URL = "https://example.com/";
  const { monitor } = await initNetMonitor("view-source:" + URL, {
    requestCount: 1,
    waitForLoad: false,
  });
  info("Starting test... ");

  const { document, store, windowRequire } = monitor.panelWin;
  const Actions = windowRequire("devtools/client/netmonitor/src/actions/index");

  store.dispatch(Actions.batchEnable(false));

  const onNetworkEvents = waitForNetworkEvents(monitor, 1, {
    expectedEventTimings: 1,
  });
  await reloadSelectedTab();
  await onNetworkEvents;

  is(
    document
      .querySelectorAll(".request-list-item")[0]
      .querySelector(".requests-list-url").innerText,
    URL,
    "The url in the displayed request is correct"
  );

  await teardown(monitor);
});
