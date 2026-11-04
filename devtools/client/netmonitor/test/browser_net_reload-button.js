/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Tests if the empty-requests reload button works.
 */

add_task(async function () {
  const { monitor } = await initNetMonitor(SIMPLE_URL, { requestCount: 1 });
  info("Starting test... ");

  const { document } = monitor.panelWin;

  const onNetworkEvents = waitForNetworkEvents(monitor, 1);
  EventUtils.sendMouseEvent(
    { type: "click" },
    document.querySelector(".requests-list-reload-notice-button")
  );
  await onNetworkEvents;

  is(
    document.querySelectorAll(".request-list-item").length,
    1,
    "The request list should have one item after reloading"
  );

  return teardown(monitor);
});
