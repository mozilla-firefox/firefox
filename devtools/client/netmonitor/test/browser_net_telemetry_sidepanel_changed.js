/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Test the sidepanel_changed telemetry event.
 */
add_task(async function () {
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

  // Click on a request and wait till the default "Headers" side panel is opened.
  info("Click on a request");
  const waitForHeaders = waitUntil(() =>
    document.querySelector(".headers-overview")
  );
  clickOnRequestRow(document.querySelectorAll(".request-list-item")[0]);
  await waitForHeaders;
  await waitForRequestData(monitor, ["requestHeaders", "responseHeaders"]);

  // Click on the Cookies panel and wait till it's opened.
  info("Click on the Cookies panel");
  clickOnSidebarTab(document, "cookies");
  await waitForRequestData(monitor, ["requestCookies", "responseCookies"]);

  const events = Glean.devtoolsMain.sidepanelChangedNetmonitor.testGetValue();
  is(1, events.length);
  is("headers", events[0].extra.oldpanel);
  is("cookies", events[0].extra.newpanel);

  return teardown(monitor);
});
