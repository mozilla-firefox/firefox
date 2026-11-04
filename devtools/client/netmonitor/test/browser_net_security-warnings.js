/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Test that warning indicators are shown when appropriate.
 */

const TEST_CASES = [
  {
    desc: "no warnings",
    uri: "https://example.com" + CORS_SJS_PATH,
    warnCipher: null,
  },
];

add_task(async function () {
  const { tab, monitor } = await initNetMonitor(CUSTOM_GET_URL, {
    requestCount: 1,
  });
  const { document, store, windowRequire } = monitor.panelWin;
  const Actions = windowRequire("devtools/client/netmonitor/src/actions/index");

  store.dispatch(Actions.batchEnable(false));

  for (const test of TEST_CASES) {
    info("Testing site with " + test.desc);

    info("Performing request to " + test.uri);
    const onNetworkEvents = waitForNetworkEvents(monitor, 1);
    await SpecialPowers.spawn(
      tab.linkedBrowser,
      [test.uri],
      async function (url) {
        content.wrappedJSObject.performRequests(1, url);
      }
    );
    await onNetworkEvents;

    info("Wait until the Security Tab is visible");
    let onDomUpdated = waitForDOM(document, "#security-tab");

    info("Selecting the request.");
    clickOnRequestRow(document.querySelectorAll(".request-list-item")[0]);
    await onDomUpdated;

    if (!document.querySelector("#security-tab[aria-selected=true]")) {
      info("Selecting the Security Tab");
      onDomUpdated = waitForDOM(document, "#security-panel .properties-view");
      clickOnSidebarTab(document, "security");
      await onDomUpdated;
    }

    is(
      document.querySelector("#security-warning-cipher"),
      test.warnCipher,
      "Cipher suite warning is hidden."
    );

    await clearNetworkEvents(monitor);
  }

  return teardown(monitor);
});
