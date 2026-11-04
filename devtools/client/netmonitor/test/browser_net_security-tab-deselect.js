/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Test that security details tab is no longer selected if an insecure request
 * is selected.
 */

add_task(async function () {
  // This test needs to trigger http and https requests.
  // Disable https-first to avoid blocking the HTTP request due to mixed content.
  await pushPref("dom.security.https_first", false);

  const { tab, monitor } = await initNetMonitor(CUSTOM_GET_URL, {
    requestCount: 1,
  });
  const { document, store, windowRequire } = monitor.panelWin;
  const Actions = windowRequire("devtools/client/netmonitor/src/actions/index");

  store.dispatch(Actions.batchEnable(false));

  info("Performing requests.");
  const onNetworkEvents = waitForNetworkEvents(monitor, 2);
  const REQUEST_URLS = [
    "https://example.com" + CORS_SJS_PATH,
    "http://example.com" + CORS_SJS_PATH,
  ];
  await SpecialPowers.spawn(
    tab.linkedBrowser,
    [REQUEST_URLS],
    async function (urls) {
      for (const url of urls) {
        content.wrappedJSObject.performRequests(1, url);
      }
    }
  );
  await onNetworkEvents;

  info("Wait until the Security tab is visible");
  const onDomUpdated = waitForDOM(document, "#security-tab");

  info("Selecting secure request.");
  clickOnRequestRow(document.querySelectorAll(".request-list-item")[0]);
  await onDomUpdated;

  info("Selecting security tab.");
  clickInView(document.querySelector("#security-tab"));

  info("Selecting insecure request.");
  clickOnRequestRow(document.querySelectorAll(".request-list-item")[1]);

  ok(
    document.querySelector("#headers-tab[aria-selected=true]"),
    "Selected tab was reset when selected security tab was hidden."
  );

  return teardown(monitor);
});
