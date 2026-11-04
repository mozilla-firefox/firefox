/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Test that clicking on the security indicator opens the security details tab.
 */

add_task(async function () {
  const { tab, monitor } = await initNetMonitor(CUSTOM_GET_URL, {
    requestCount: 1,
  });
  const { document, store, windowRequire } = monitor.panelWin;
  const Actions = windowRequire("devtools/client/netmonitor/src/actions/index");

  store.dispatch(Actions.batchEnable(false));

  info("Requesting a resource over HTTPS.");
  await performRequestAndWait(
    "https://example.com" + CORS_SJS_PATH + "?request_2"
  );
  await performRequestAndWait(
    "https://example.com" + CORS_SJS_PATH + "?request_1"
  );

  is(store.getState().requests.requests.length, 2, "Two events event logged.");

  await clickAndTestSecurityIcon();

  info("Selecting headers panel again.");
  clickOnSidebarTab(document, "headers");

  info("Sorting the items by filename.");
  EventUtils.sendMouseEvent(
    { type: "click" },
    document.querySelector("#requests-list-file-button")
  );

  info(
    "Testing that security icon can be clicked after the items were sorted."
  );

  await clickAndTestSecurityIcon();

  return teardown(monitor);

  async function performRequestAndWait(url) {
    const onNetworkEvents = waitForNetworkEvents(monitor, 1);
    await SpecialPowers.spawn(
      tab.linkedBrowser,
      [{ url }],
      async function (args) {
        content.wrappedJSObject.performRequests(1, args.url);
      }
    );
    return onNetworkEvents;
  }

  async function clickAndTestSecurityIcon() {
    const icon = document.querySelector(".requests-security-state-icon");
    info(
      "Clicking security icon of the first request and waiting for panel update."
    );
    EventUtils.synthesizeMouseAtCenter(icon, {}, monitor.panelWin);
    await waitUntil(() =>
      document.querySelector("#security-panel .security-info-value")
    );

    ok(
      document.querySelector("#security-tab[aria-selected=true]"),
      "Security tab is selected."
    );
  }
});
