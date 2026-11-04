/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Test if filtering items in the network table works correctly with new requests
 * and while sorting is enabled.
 */
const BASIC_REQUESTS = [
  { url: "sjs_content-type-test-server.sjs?fmt=html&res=undefined" },
  { url: "sjs_content-type-test-server.sjs?fmt=css" },
  { url: "sjs_content-type-test-server.sjs?fmt=js" },
];

const REQUESTS_WITH_MEDIA = BASIC_REQUESTS.concat([
  { url: "sjs_content-type-test-server.sjs?fmt=font" },
  { url: "sjs_content-type-test-server.sjs?fmt=image" },
  { url: "sjs_content-type-test-server.sjs?fmt=audio" },
  { url: "sjs_content-type-test-server.sjs?fmt=video" },
]);

add_task(async function () {
  const { monitor } = await initNetMonitor(FILTERING_URL, { requestCount: 1 });
  info("Starting test... ");

  // It seems that this test may be slow on Ubuntu builds running on ec2.
  requestLongerTimeout(2);

  const { document, store, windowRequire } = monitor.panelWin;
  const Actions = windowRequire("devtools/client/netmonitor/src/actions/index");
  const { getDisplayedRequests, getSelectedRequest, getSortedRequests } =
    windowRequire("devtools/client/netmonitor/src/selectors/index");

  store.dispatch(Actions.batchEnable(false));

  // The test assumes that the first HTML request here has a longer response
  // body than the other HTML requests performed later during the test.
  const requests = Cu.cloneInto(REQUESTS_WITH_MEDIA, {});
  const newres = "res=<p>" + new Array(10).join(Math.random(10)) + "</p>";
  requests[0].url = requests[0].url.replace("res=undefined", newres);

  let onNetworkEvents = waitForNetworkEvents(monitor, 7);
  await performRequestsInContent(requests);
  await onNetworkEvents;

  clickOnRequestRow(document.querySelectorAll(".request-list-item")[0]);

  isnot(
    getSelectedRequest(store.getState()),
    null,
    "There should be a selected item in the requests menu."
  );
  is(
    getSelectedIndex(store.getState()),
    0,
    "The first item should be selected in the requests menu."
  );
  is(
    !!document.querySelector(".network-details-bar"),
    true,
    "The network details panel should be visible after toggle button was pressed."
  );

  testFilterButtons(monitor, "all");
  await testContents([0, 1, 2, 3, 4, 5, 6], 7, 0);

  info("Sorting by size, ascending.");
  EventUtils.sendMouseEvent(
    { type: "click" },
    document.querySelector("#requests-list-contentSize-button")
  );
  testFilterButtons(monitor, "all");
  await testContents([6, 4, 5, 0, 1, 2, 3], 7, 6);

  info("Testing html filtering.");
  EventUtils.sendMouseEvent(
    { type: "click" },
    document.querySelector(".requests-list-filter-html-button")
  );
  testFilterButtons(monitor, "html");
  await testContents([6, 4, 5, 0, 1, 2, 3], 1, 6);

  info("Performing more requests.");
  // As the view is filtered and there is only one request for which we fetch event timings
  onNetworkEvents = waitForNetworkEvents(monitor, 7, {
    expectedEventTimings: 1,
  });
  performRequestsInContent(REQUESTS_WITH_MEDIA);
  await onNetworkEvents;

  info("Testing html filtering again.");
  resetSorting();
  testFilterButtons(monitor, "html");
  await testContents([8, 13, 9, 11, 10, 12, 0, 4, 1, 5, 2, 6, 3, 7], 2, 13);

  info("Performing more requests.");
  // As the view is filtered and there is only one request for which we fetch event timings
  onNetworkEvents = waitForNetworkEvents(monitor, 7, {
    expectedEventTimings: 1,
  });
  performRequestsInContent(REQUESTS_WITH_MEDIA);
  await onNetworkEvents;

  info("Testing html filtering again.");
  resetSorting();
  testFilterButtons(monitor, "html");
  await testContents(
    [12, 13, 20, 14, 16, 18, 15, 17, 19, 0, 4, 8, 1, 5, 9, 2, 6, 10, 3, 7, 11],
    3,
    20
  );

  await teardown(monitor);

  function resetSorting() {
    EventUtils.sendMouseEvent(
      { type: "click" },
      document.querySelector("#requests-list-initiator-button")
    );
    EventUtils.sendMouseEvent(
      { type: "click" },
      document.querySelector("#requests-list-contentSize-button")
    );
  }

  function getSelectedIndex(state) {
    if (!state.requests.selectedId) {
      return -1;
    }
    return getSortedRequests(state).findIndex(
      r => r.id === state.requests.selectedId
    );
  }

  async function testContents(order, visible, selection) {
    await waitForAllNetworkUpdateEvents();

    isnot(
      getSelectedRequest(store.getState()),
      null,
      "There should still be a selected item after filtering."
    );
    is(
      getSelectedIndex(store.getState()),
      selection,
      "The first item should be still selected after filtering."
    );
    is(
      !!document.querySelector(".network-details-bar"),
      true,
      "The network details panel should still be visible after filtering."
    );

    is(
      getSortedRequests(store.getState()).length,
      order.length,
      "There should be a specific amount of items in the requests menu."
    );
    is(
      getDisplayedRequests(store.getState()).length,
      visible,
      "There should be a specific amount of visible items in the requests menu."
    );
  }
});
