/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Tests if the network monitor leaks on initialization and sudden destruction.
 * You can also use this initialization format as a template for other tests.
 */

add_task(async function () {
  const { monitor } = await initNetMonitor(SINGLE_GET_URL, {
    requestCount: 1,
  });
  info("Starting test... ");

  const { document, store, windowRequire } = monitor.panelWin;
  const Actions = windowRequire("devtools/client/netmonitor/src/actions/index");

  store.dispatch(Actions.batchEnable(false));

  await pushPref("devtools.netmonitor.persistlog", false);

  await reloadAndWait();

  // Using waitUntil in the test is necessary to ensure all requests are added correctly.
  // Because reloadAndWait call may catch early uncaught requests from initNetMonitor, so
  // the actual number of requests after reloadAndWait could be wrong since all requests
  // haven't finished.
  await waitUntil(
    () => document.querySelectorAll(".request-list-item").length === 2
  );
  is(
    document.querySelectorAll(".request-list-item").length,
    2,
    "The request list should have two items at this point."
  );

  await reloadAndWait();

  await waitUntil(
    () => document.querySelectorAll(".request-list-item").length === 2
  );
  // Since the reload clears the log, we still expect two requests in the log
  is(
    document.querySelectorAll(".request-list-item").length,
    2,
    "The request list should still have two items at this point."
  );

  // Now we toggle the persistence logs on
  await pushPref("devtools.netmonitor.persistlog", true);

  await reloadAndWait();

  await waitUntil(
    () => document.querySelectorAll(".request-list-item").length === 4
  );
  // Since we togged the persistence logs, we expect four items after the reload
  is(
    document.querySelectorAll(".request-list-item").length,
    4,
    "The request list should now have four items at this point."
  );

  return teardown(monitor);

  /**
   * Reload the page and wait for 2 GET requests.
   */
  async function reloadAndWait() {
    const onNetworkEvents = waitForNetworkEvents(monitor, 2);
    await reloadSelectedTab();
    return onNetworkEvents;
  }
});
