/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Tests for timings columns. Note that the column
 * header is visible only if there are requests in the list.
 */
add_task(async function () {
  const { monitor } = await initNetMonitor(SIMPLE_URL, {
    requestCount: 1,
  });
  info("Starting test... ");

  const { document, store, windowRequire } = monitor.panelWin;
  const Actions = windowRequire("devtools/client/netmonitor/src/actions/index");
  store.dispatch(Actions.batchEnable(false));

  const visibleColumns = store.getState().ui.columns;

  let onNetworkEvents = waitForNetworkEvents(monitor, 1);
  await reloadSelectedTab();
  await onNetworkEvents;

  // Hide the waterfall column to make sure timing data are fetched
  // by the other timing columns ("endTime", "responseTime", "duration",
  // "latency").
  // Note that all these timing columns are based on the same
  // `RequestListColumnTime` component.
  if (visibleColumns.waterfall) {
    await hideColumn(monitor, "waterfall");
  }

  ["endTime", "responseTime", "duration", "latency"].forEach(async column => {
    if (!visibleColumns[column]) {
      await showColumn(monitor, column);
    }
  });

  onNetworkEvents = waitForNetworkEvents(monitor, 1);
  await reloadSelectedTab();
  await onNetworkEvents;

  // There should be one request in the list.
  const requestItems = document.querySelectorAll(".request-list-item");
  is(requestItems.length, 1, "There must be one visible item");

  const item = requestItems[0];
  const types = ["end", "response", "duration", "latency"];

  for (const t of types) {
    info("Check the timing column for type: " + t);
    await waitUntil(() => {
      const node = item.querySelector(".requests-list-" + t + "-time");
      const value = parseInt(node.textContent, 10);
      return value >= 0;
    });
  }

  await teardown(monitor);
});
