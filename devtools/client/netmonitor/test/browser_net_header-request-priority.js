/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Tests if "Request Priority" is displayed in the header panel.
 */
add_task(async function () {
  const { monitor } = await initNetMonitor(POST_RAW_URL, {
    requestCount: 1,
  });

  const { document } = monitor.panelWin;

  const waitReq = waitForNetworkEvents(monitor, 1);
  EventUtils.sendMouseEvent(
    { type: "click" },
    document.querySelector(".requests-list-reload-notice-button")
  );
  await waitReq;

  // Wait until the tab panel summary is displayed
  const onSummaryLabel = waitUntil(
    () => document.querySelectorAll(".tabpanel-summary-label")[0]
  );
  clickOnRequestRow(document.querySelectorAll(".request-list-item")[0]);
  await onSummaryLabel;

  const requestPriorityHeaderExists = Array.from(
    document.querySelectorAll(".tabpanel-summary-label")
  ).some(header => header.textContent === "Request Priority");
  is(
    requestPriorityHeaderExists,
    true,
    '"Request Priority" header is displayed in the header panel.'
  );

  return teardown(monitor);
});
