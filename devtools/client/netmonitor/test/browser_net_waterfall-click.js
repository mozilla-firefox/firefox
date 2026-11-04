/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Test that clicking on the waterfall opens the timing sidebar panel.
 */

add_task(async function () {
  const { tab, monitor } = await initNetMonitor(
    CONTENT_TYPE_WITHOUT_CACHE_URL,
    { requestCount: 1 }
  );
  const { document } = monitor.panelWin;

  // Execute requests.
  await performRequests(monitor, tab, CONTENT_TYPE_WITHOUT_CACHE_REQUESTS);

  info("Clicking waterfall and waiting for panel update.");
  const onDomUpdated = waitForDOM(document, "#timings-panel");

  clickInView(document.querySelectorAll(".requests-list-timings")[0]);

  await onDomUpdated;

  ok(
    document.querySelector("#timings-tab[aria-selected=true]"),
    "Timings tab is selected."
  );

  return teardown(monitor);
});
