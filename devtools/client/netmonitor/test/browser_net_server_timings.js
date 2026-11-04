/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Tests if server side timings are displayed
 */
add_task(async function () {
  const { tab, monitor } = await initNetMonitor(HTTPS_CUSTOM_GET_URL, {
    requestCount: 1,
  });

  const { document, store, windowRequire } = monitor.panelWin;
  const Actions = windowRequire("devtools/client/netmonitor/src/actions/index");
  store.dispatch(Actions.batchEnable(false));

  const onNetworkEvents = waitForNetworkEvents(monitor, 1);
  await SpecialPowers.spawn(
    tab.linkedBrowser,
    [SERVER_TIMINGS_TYPE_SJS],
    async function (url) {
      content.wrappedJSObject.performRequests(1, url);
    }
  );
  await onNetworkEvents;

  // There must be 4 timing values (including server side timings).
  const timingsSelector = "#timings-panel .tabpanel-summary-container.server";
  const onDomUpdated = waitForDOM(document, timingsSelector, 4);

  EventUtils.sendMouseEvent(
    { type: "click" },
    document.querySelectorAll(".request-list-item")[0]
  );

  clickOnSidebarTab(document, "timings");
  await onDomUpdated;

  // Check the UI contains server side timings and correct values
  const timings = document.querySelectorAll(timingsSelector, 4);
  is(
    timings[0].textContent,
    "time1123 ms",
    "The first server-timing must be correct"
  );
  is(
    timings[1].textContent,
    "time20 ms",
    "The second server-timing must be correct"
  );
  is(
    timings[2].textContent,
    "time31.66 min",
    "The third server-timing must be correct"
  );
  is(
    timings[3].textContent,
    "time41.11 s",
    "The fourth server-timing must be correct"
  );

  await teardown(monitor);
});
