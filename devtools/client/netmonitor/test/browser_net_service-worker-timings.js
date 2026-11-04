"use strict";

/**
 * Tests that timings for requests from the service workers are displayed correctly.
 */
add_task(async function testServiceWorkerTimings() {
  // Service workers only work on https
  const TEST_URL = HTTPS_EXAMPLE_URL + "service-workers/status-codes.html";
  const { tab, monitor } = await initNetMonitor(TEST_URL, {
    enableCache: true,
    requestCount: 1,
  });
  info("Starting test... ");

  const { document, store, windowRequire } = monitor.panelWin;
  const Actions = windowRequire("devtools/client/netmonitor/src/actions/index");

  store.dispatch(Actions.batchEnable(false));

  info("Registering the service worker...");
  await SpecialPowers.spawn(tab.linkedBrowser, [], async function () {
    await content.wrappedJSObject.registerServiceWorker();
  });

  info("Performing requests which are intercepted by service worker...");
  await performRequests(monitor, tab, 1);

  const timingsSelector =
    "#timings-panel .tabpanel-summary-container.service-worker";
  const onDomUpdated = waitForDOM(document, timingsSelector, 3);

  EventUtils.sendMouseEvent(
    { type: "click" },
    document.querySelectorAll(".request-list-item")[0]
  );

  clickOnSidebarTab(document, "timings");
  await onDomUpdated;

  const timings = document.querySelectorAll(timingsSelector, 3);
  // Note: The specific timing numbers change so this only asserts that the
  // timings are visible
  ok(
    timings[0].textContent.includes("Startup"),
    "The service worker timing for launch of the service worker is visible"
  );
  ok(
    timings[1].textContent.includes("Dispatch fetch:"),
    "The service worker timing for dispatching the fetch event is visible"
  );
  ok(
    timings[2].textContent.includes("Handle fetch:"),
    "The service worker timing for handling the fetch event is visible"
  );

  info("Unregistering the service worker...");
  await SpecialPowers.spawn(tab.linkedBrowser, [], async function () {
    await content.wrappedJSObject.unregisterServiceWorker();
  });

  await teardown(monitor);
});
