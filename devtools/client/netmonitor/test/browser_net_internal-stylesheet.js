/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const TEST_URL = EXAMPLE_URL + "html_internal-stylesheet.html";

/**
 * Test for the stylesheet which is loaded as internal.
 */
add_task(async function () {
  const { monitor } = await initNetMonitor(TEST_URL, {
    requestCount: 2,
  });

  const onNetworkEvents = waitForNetworkEvents(monitor, 2);
  await reloadSelectedTab();
  await onNetworkEvents;

  const { store } = monitor.panelWin;
  const requests = store.getState().requests.requests;
  is(
    requests.length,
    2,
    "The number of requests state in the store is correct"
  );

  const styleSheetRequest = requests.find(
    r => r.urlDetails.baseNameWithQuery === "internal-loaded.css"
  );
  ok(
    styleSheetRequest,
    "The stylesheet which is loaded as internal is in the request"
  );

  return teardown(monitor);
});
