/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Tests the filter of the Request panel, including when selecting other
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
    [SIMPLE_SJS],
    async function (url) {
      const postOptions = {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ foo: 1, bar: 2 }),
      };
      await content.fetch(url, postOptions);
      await content.fetch(url);
      await content.fetch(url, postOptions);
    }
  );
  await onNetworkEvents;

  const requestItems = document.querySelectorAll(".request-list-item");

  clickOnRequestRow(requestItems[0]);
  await waitForDOM(document, ".network-details-bar");
  clickOnSidebarTab(document, "request");
  await waitForPostData(document, store, 0);

  document.querySelector("#request-panel .devtools-filterinput").focus();
  EventUtils.sendString("foo");
  await waitFor(() => document.querySelector("#request-panel .treeRow.hidden"));
  checkVisibleParams(document);

  info("Select a request without post data");
  clickOnRequestRow(requestItems[1]);
  await waitForDOM(document, "#request-panel .empty-notice");

  info("Select another request with post data");
  clickOnRequestRow(requestItems[2]);
  await waitForPostData(document, store, 2);

  is(
    document.querySelector("#request-panel .devtools-filterinput").value,
    "foo",
    "The filter input still shows the filter"
  );
  checkVisibleParams(document);

  await teardown(monitor);
});

async function waitForPostData(document, store, index) {
  await waitForRequestData(store, ["requestPostData"], undefined, index);
  await waitFor(() => document.querySelector("#request-panel .treeRow"));
}

function checkVisibleParams(document) {
  const visibleLabels = [
    ...document.querySelectorAll(
      "#request-panel .treeRow:not(.hidden) .treeLabelCell"
    ),
  ].map(cell => cell.innerText);
  ok(visibleLabels.includes("foo"), "The matching property is visible");
  ok(!visibleLabels.includes("bar"), "Other properties are hidden");
}
