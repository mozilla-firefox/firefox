/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Tests if beacons from other tabs are properly ignored.
 */

add_task(async function () {
  const { monitor, tab } = await initNetMonitor(SIMPLE_URL, {
    requestCount: 1,
  });
  const { store, windowRequire } = monitor.panelWin;
  const Actions = windowRequire("devtools/client/netmonitor/src/actions/index");
  const { getSortedRequests } = windowRequire(
    "devtools/client/netmonitor/src/selectors/index"
  );

  store.dispatch(Actions.batchEnable(false));

  const beaconTab = await addTab(SEND_BEACON_URL, { background: true });
  info("Beacon tab added successfully.");

  is(
    store.getState().requests.requests.length,
    0,
    "The requests menu should be empty."
  );

  const onNetworkEvents = waitForNetworkEvents(monitor, 1);
  await SpecialPowers.spawn(beaconTab.linkedBrowser, [], async function () {
    content.wrappedJSObject.performRequests();
  });
  is(gBrowser.selectedTab, tab, "The test page is still the selected tab");
  await reloadSelectedTab();
  await onNetworkEvents;

  is(
    store.getState().requests.requests.length,
    1,
    "Only the reload should be recorded."
  );
  const request = getSortedRequests(store.getState())[0];
  is(request.method, "GET", "The method is correct.");
  is(request.status, "200", "The status is correct.");

  await removeTab(beaconTab);
  return teardown(monitor);
});
