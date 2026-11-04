/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Tests if the network monitor panes collapse properly.
 */

add_task(async function () {
  const { monitor } = await initNetMonitor(SIMPLE_URL, {
    requestCount: 1,
  });
  info("Starting test... ");

  const { document, store, windowRequire } = monitor.panelWin;
  const Actions = windowRequire("devtools/client/netmonitor/src/actions/index");
  const { Prefs } = windowRequire("devtools/client/netmonitor/src/utils/prefs");

  const onNetworkEvents = waitForNetworkEvents(monitor, 1);
  await reloadSelectedTab();
  await onNetworkEvents;

  ok(
    !document.querySelector(".network-details-bar") &&
      !document.querySelector(".sidebar-toggle"),
    "The details panel should initially be hidden."
  );

  store.dispatch(Actions.toggleNetworkDetails());

  is(
    ~~document.querySelector(".network-details-bar").clientWidth,
    Prefs.networkDetailsWidth,
    "The details panel has an incorrect width."
  );
  ok(
    document.querySelector(".network-details-bar") &&
      document.querySelector(".sidebar-toggle"),
    "The details panel should at this point be visible."
  );

  EventUtils.sendMouseEvent(
    { type: "click" },
    document.querySelector(".sidebar-toggle")
  );

  ok(
    !document.querySelector(".network-details-bar") &&
      !document.querySelector(".sidebar-toggle"),
    "The details panel should not be visible after collapsing."
  );

  store.dispatch(Actions.toggleNetworkDetails());

  is(
    ~~document.querySelector(".network-details-bar").clientWidth,
    Prefs.networkDetailsWidth,
    "The details panel has an incorrect width after uncollapsing."
  );
  ok(
    document.querySelector(".network-details-bar") &&
      document.querySelector(".sidebar-toggle"),
    "The details panel should be visible again after uncollapsing."
  );

  await teardown(monitor);
});
