/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Tests if beacons are handled correctly.
 */

const IFRAME_URL = EXAMPLE_URL + "html_send-beacon-late-iframe-request.html";

add_task(async function () {
  const { tab, monitor } = await initNetMonitor(SEND_BEACON_URL, {
    requestCount: 1,
  });
  const { store, windowRequire } = monitor.panelWin;
  const Actions = windowRequire("devtools/client/netmonitor/src/actions/index");
  const { getSortedRequests } = windowRequire(
    "devtools/client/netmonitor/src/selectors/index"
  );

  store.dispatch(Actions.batchEnable(false));

  is(
    store.getState().requests.requests.length,
    0,
    "The requests menu should be empty."
  );

  // Execute requests.
  await performRequests(monitor, tab, 1);

  is(
    store.getState().requests.requests.length,
    1,
    "The beacon should be recorded."
  );

  // Wait for updates of status, blockedReason fields
  await waitForRequestData(monitor, ["status", "blockedReason"], null, 0);
  const request = getSortedRequests(store.getState())[0];
  is(request.method, "POST", "The method is correct.");
  ok(request.url.endsWith("beacon_request"), "The URL is correct.");
  is(request.status, "404", "The status is correct.");
  is(request.blockedReason, 0, "The request is not blocked");

  const onNetworkEvents = waitForNetworkEvents(monitor, 2);
  await SpecialPowers.spawn(
    tab.linkedBrowser,
    [IFRAME_URL],
    async function (url) {
      const iframe = content.document.createElement("iframe");
      iframe.src = url;
      content.document.body.appendChild(iframe);
      await new Promise(resolve => (iframe.onload = resolve));
      iframe.remove();
    }
  );
  await onNetworkEvents;

  await waitForRequestData(monitor, ["status", "blockedReason"], null, 2);
  const lateRequest = getSortedRequests(store.getState())[2];
  is(lateRequest.method, "POST", "The method is correct.");
  ok(lateRequest.url.endsWith("beacon_late_request"), "The URL is correct.");
  is(lateRequest.status, "404", "The status is correct.");
  is(lateRequest.blockedReason, 0, "The request is not blocked");

  return teardown(monitor);
});
