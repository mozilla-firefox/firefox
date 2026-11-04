/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

// Network throttling integration test.

"use strict";

requestLongerTimeout(2);

add_task(async function () {
  await throttleTest({ throttle: true, addLatency: true });
  await throttleTest({ throttle: true, addLatency: false });
  await throttleTest({ throttle: false, addLatency: false });
});

async function throttleTest(options) {
  const { throttle, addLatency } = options;
  const { monitor } = await initNetMonitor(SIMPLE_URL, { requestCount: 1 });
  const { store, windowRequire, connector } = monitor.panelWin;
  const { ACTIVITY_TYPE } = windowRequire(
    "devtools/client/netmonitor/src/constants"
  );
  const { updateNetworkThrottling, triggerActivity } = connector;
  const { getSortedRequests } = windowRequire(
    "devtools/client/netmonitor/src/selectors/index"
  );

  info(`Starting test... (throttle = ${throttle}, addLatency = ${addLatency})`);

  // When throttling, must be smaller than the length of the content
  // of SIMPLE_URL in bytes.
  const size = throttle ? 200 : 0;
  const latency = addLatency ? 100 : 0;

  const throttleProfile = {
    latency,
    download: size,
    upload: 10000,
  };

  info("sending throttle request");

  await updateNetworkThrottling(true, throttleProfile);

  const onNetworkEvents = waitForNetworkEvents(monitor, 1);
  await triggerActivity(ACTIVITY_TYPE.RELOAD.WITH_CACHE_DISABLED);
  await onNetworkEvents;

  await waitForRequestData(store, ["eventTimings"]);

  const requestItem = getSortedRequests(store.getState())[0];
  const reportedOneSecond = requestItem.eventTimings.timings.receive > 1000;
  if (throttle) {
    ok(reportedOneSecond, "download reported as taking more than one second");
  } else {
    ok(!reportedOneSecond, "download reported as taking less than one second");
  }

  await teardown(monitor);
}
