/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Tests the filter of the Cookies panel, including when selecting other
 * requests.
 */
add_task(async function () {
  const { monitor } = await initNetMonitor(SIMPLE_URL, {
    requestCount: 1,
  });
  const { document, store, windowRequire } = monitor.panelWin;
  const Actions = windowRequire("devtools/client/netmonitor/src/actions/index");
  store.dispatch(Actions.batchEnable(false));

  const onNetworkEvents = waitForNetworkEvents(monitor, 3);
  await SpecialPowers.spawn(
    gBrowser.selectedBrowser,
    [SIMPLE_UNSORTED_COOKIES_SJS, SIMPLE_URL],
    async function (cookiesUrl, noCookiesUrl) {
      await content.fetch(cookiesUrl);
      await content.fetch(noCookiesUrl, { credentials: "omit" });
      await content.fetch(cookiesUrl);
    }
  );
  await onNetworkEvents;

  const requestItems = document.querySelectorAll(".request-list-item");

  clickOnRequestRow(requestItems[0]);
  await waitForDOM(document, ".network-details-bar");
  clickOnSidebarTab(document, "cookies");
  await waitForCookies(document, store, 0);

  document.querySelector("#cookies-panel .devtools-filterinput").focus();
  EventUtils.sendString("tom");
  await waitFor(() => document.querySelector("#cookies-panel .treeRow.hidden"));
  checkVisibleCookies(document);

  info("Select a request without cookies");
  clickOnRequestRow(requestItems[1]);
  await waitForDOM(document, "#cookies-panel .empty-notice");

  info("Select another request with cookies");
  clickOnRequestRow(requestItems[2]);
  await waitForCookies(document, store, 2);

  is(
    document.querySelector("#cookies-panel .devtools-filterinput").value,
    "tom",
    "The filter input still shows the filter"
  );
  checkVisibleCookies(document);

  await teardown(monitor);
});

async function waitForCookies(document, store, index) {
  await waitForRequestData(
    store,
    ["requestCookies", "responseCookies"],
    undefined,
    index
  );
  await waitFor(() => document.querySelector("#cookies-panel .treeRow"));
}

function checkVisibleCookies(document) {
  const visibleLabels = [
    ...document.querySelectorAll(
      "#cookies-panel .treeRow:not(.hidden) .treeLabelCell"
    ),
  ].map(cell => cell.innerText);
  ok(visibleLabels.includes("tom"), "The matching cookie is visible");
  ok(!visibleLabels.includes("bob"), "Other cookies are hidden");
}
