/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Test that requests blocked by CORS have a notification in the response panel
 * and that it is not present when requests are not blocked
 */

add_task(async function testCORSNotificationPresent() {
  info("Test that CORS notification is present");

  const { tab, monitor } = await initNetMonitor(HTTPS_CORS_URL, {
    requestCount: 1,
  });

  const { document, store, windowRequire } = monitor.panelWin;
  const Actions = windowRequire("devtools/client/netmonitor/src/actions/index");

  store.dispatch(Actions.batchEnable(false));

  const onNetworkEvents = waitForNetworkEvents(monitor, 1);

  info("making request to a origin that doesn't allow cross origin");
  const requestUrl = HTTPS_EXAMPLE_ORG_URL + "sjs_simple-test-server.sjs";
  await SpecialPowers.spawn(
    tab.linkedBrowser,
    [requestUrl],
    async function (url) {
      content.wrappedJSObject.performRequests(
        url,
        "triggering/preflight",
        "post-data"
      );
    }
  );

  info("Waiting until the requests appear in netmonitor");
  await onNetworkEvents;

  info("selecting preflight request at index 1");
  const firstItem = document.querySelectorAll(".request-list-item")[1];
  clickOnRequestRow(firstItem);

  const waitForRespPanel = waitForDOM(
    document,
    "#response-panel .notification"
  );

  info("switching to response panel");
  const respPanelButton = document.querySelector("#response-tab");
  EventUtils.sendMouseEvent({ type: "click" }, respPanelButton);
  await waitForRespPanel;

  info("selecting CORS notification");
  const CORSNotification = document.querySelector(
    '#response-panel .notification[data-key="CORS-error"] .messageText'
  );
  ok(CORSNotification, "CORS Notification Present");
  is(
    CORSNotification?.innerText,
    "Response body is not available to scripts (Reason: CORS Missing Allow Origin)",
    "Notification text is correct"
  );

  await teardown(monitor);
});

add_task(async function testCORSNotificationNotPresent() {
  info("Test that CORS notification is not present");
  const { tab, monitor } = await initNetMonitor(CORS_URL, {
    requestCount: 1,
  });

  const { document, store, windowRequire } = monitor.panelWin;
  const Actions = windowRequire("devtools/client/netmonitor/src/actions/index");

  store.dispatch(Actions.batchEnable(false));

  const onNetworkEvents = waitForNetworkEvents(monitor, 1);

  info("Making request to a origin that allows cross origin");
  const requestUrl = "https://test1.example.com" + CORS_SJS_PATH;
  await SpecialPowers.spawn(
    tab.linkedBrowser,
    [requestUrl],
    async function (url) {
      content.wrappedJSObject.performRequests(
        url,
        "triggering/preflight",
        "post-data"
      );
    }
  );
  info("waiting for requests to appear in netmonitor");
  await onNetworkEvents;

  info("selecting preflight request at index 1");
  const firstItem = document.querySelectorAll(".request-list-item")[1];
  clickOnRequestRow(firstItem);

  const waitForRespPanel = waitForDOM(document, "#response-panel");

  info("switching to response panel");
  const respPanelButton = document.querySelector("#response-tab");
  EventUtils.sendMouseEvent({ type: "click" }, respPanelButton);
  await waitForRespPanel;

  info("try to select CORS notification");
  const CORSNotification = document.querySelector(
    '#response-panel .notification[data-key="CORS-error"] .messageText'
  );
  ok(!CORSNotification, "CORS notification not present");

  await teardown(monitor);
});
