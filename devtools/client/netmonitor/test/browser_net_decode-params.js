/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Tests if "+" is replaces with spaces in the headers panel.
 */
add_task(async function () {
  const { tab, monitor } = await initNetMonitor(POST_RAW_URL_WITH_HASH, {
    requestCount: 1,
  });
  info("Starting test... ");

  const { document, store, windowRequire } = monitor.panelWin;
  const Actions = windowRequire("devtools/client/netmonitor/src/actions/index");
  store.dispatch(Actions.batchEnable(false));

  // Execute request.
  await performRequests(monitor, tab, 1);

  // Wait until the tab panel summary is displayed
  const onSummaryLabel = waitUntil(
    () => document.querySelectorAll(".tabpanel-summary-label")[0]
  );
  clickOnRequestRow(document.querySelectorAll(".request-list-item")[0]);
  await onSummaryLabel;

  clickOnSidebarTab(document, "request");

  return teardown(monitor);
});
