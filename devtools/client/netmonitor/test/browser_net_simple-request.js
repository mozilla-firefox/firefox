/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Test whether the UI state properly reflects existence of requests
 * displayed in the Net panel. The following parts of the UI are
 * tested:
 * 1) Side panel visibility
 * 2) Side panel toggle button
 * 3) Empty user message visibility
 * 4) Number of requests displayed
 */
add_task(async function () {
  const { monitor } = await initNetMonitor(SIMPLE_URL, {
    requestCount: 1,
  });
  info("Starting test... ");

  const { document, store, windowRequire } = monitor.panelWin;
  const Actions = windowRequire("devtools/client/netmonitor/src/actions/index");

  store.dispatch(Actions.batchEnable(false));

  ok(
    document.querySelector(".request-list-empty-notice"),
    "An empty notice should be displayed when the frontend is opened."
  );
  is(
    store.getState().requests.requests.length,
    0,
    "The requests menu should be empty when the frontend is opened."
  );
  is(
    !!document.querySelector(".network-details-bar"),
    false,
    "The network details panel should be hidden when the frontend is opened."
  );

  await reloadAndWait();

  ok(
    !document.querySelector(".request-list-empty-notice"),
    "The empty notice should be hidden after the first request."
  );
  is(
    store.getState().requests.requests.length,
    1,
    "The requests menu should not be empty after the first request."
  );
  is(
    !!document.querySelector(".network-details-bar"),
    false,
    "The network details panel should still be hidden after the first request."
  );

  await reloadAndWait();

  ok(
    !document.querySelector(".request-list-empty-notice"),
    "The empty notice should be still hidden after a reload."
  );
  is(
    store.getState().requests.requests.length,
    1,
    "The requests menu should not be empty after a reload."
  );
  is(
    !!document.querySelector(".network-details-bar"),
    false,
    "The network details panel should still be hidden after a reload."
  );

  await clearNetworkEvents(monitor);

  ok(
    document.querySelector(".request-list-empty-notice"),
    "An empty notice should be displayed again after clear."
  );
  is(
    store.getState().requests.requests.length,
    0,
    "The requests menu should be empty after clear."
  );
  is(
    !!document.querySelector(".network-details-bar"),
    false,
    "The network details panel should still be hidden after clear."
  );

  return teardown(monitor);

  async function reloadAndWait() {
    const onNetworkEvents = waitForNetworkEvents(monitor, 1);
    await reloadSelectedTab();
    return onNetworkEvents;
  }
});
