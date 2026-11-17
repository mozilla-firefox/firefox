/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Test the edit_resend telemetry event.
 */
add_task(async function () {
  if (
    Services.prefs.getBoolPref(
      "devtools.netmonitor.features.newEditAndResend",
      true
    )
  ) {
    ok(
      true,
      "Skip this test when pref is true, because this panel won't be default when that is the case."
    );
    return;
  }
  const { monitor } = await initNetMonitor(HTTPS_SIMPLE_URL, {
    requestCount: 1,
  });
  info("Starting test... ");

  const { document, store, windowRequire } = monitor.panelWin;
  const Actions = windowRequire("devtools/client/netmonitor/src/actions/index");
  store.dispatch(Actions.batchEnable(false));

  // Remove all events (you can check about:glean).
  Services.fog.testResetFOG();

  // Reload to have one request in the list.
  const waitForEvents = waitForNetworkEvents(monitor, 1);
  await navigateTo(HTTPS_SIMPLE_URL);
  await waitForEvents;

  // Open context menu and execute "Edit & Resend".
  const firstRequest = document.querySelectorAll(".request-list-item")[0];
  const waitForHeaders = waitUntil(() =>
    document.querySelector(".headers-overview")
  );
  clickOnRequestRow(firstRequest);
  await waitForHeaders;
  await waitForRequestData(monitor, ["requestHeaders", "responseHeaders"]);
  EventUtils.sendMouseEvent({ type: "contextmenu" }, firstRequest);

  // Open "New Request" form and resend.
  await selectContextMenuItem(monitor, "request-list-context-edit-resend");
  await waitUntil(() => document.querySelector("#custom-request-send-button"));
  document.querySelector("#custom-request-send-button").click();

  await waitForNetworkEvents(monitor, 1);

  is(1, Glean.devtoolsMain.editResendNetmonitor.testGetValue());

  await teardown(monitor);
});
