/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Test the action of request details panel when filters are applied.
 * If there are any visible requests, the first request from the
 * list of visible requests should be displayed in the network
 * details panel
 * If there are no visible requests the panel should remain closed
 */

const REQUESTS_WITH_MEDIA_AND_FLASH_AND_WS = [
  {
    url: "sjs_content-type-test-server.sjs?fmt=html&res=undefined&text=Sample",
  },
  { url: "sjs_content-type-test-server.sjs?fmt=css&text=sample" },
  { url: "sjs_content-type-test-server.sjs?fmt=js&text=sample" },
  { url: "sjs_content-type-test-server.sjs?fmt=font" },
  { url: "sjs_content-type-test-server.sjs?fmt=image" },
  { url: "sjs_content-type-test-server.sjs?fmt=audio" },
  { url: "sjs_content-type-test-server.sjs?fmt=video" },
  { url: "sjs_content-type-test-server.sjs?fmt=flash" },
  { url: "sjs_content-type-test-server.sjs?fmt=ws" },
];

add_task(async function () {
  const { monitor } = await initNetMonitor(FILTERING_URL, { requestCount: 1 });
  const { document, store, windowRequire } = monitor.panelWin;
  const Actions = windowRequire("devtools/client/netmonitor/src/actions/index");
  const { getSelectedRequest, getSortedRequests } = windowRequire(
    "devtools/client/netmonitor/src/selectors/index"
  );

  store.dispatch(Actions.batchEnable(false));

  function setFreetextFilter(value) {
    store.dispatch(Actions.setRequestFilterText(value));
  }

  info("Starting test... ");

  const onNetworkEvents = waitForNetworkEvents(monitor, 9);
  await performRequestsInContent(REQUESTS_WITH_MEDIA_AND_FLASH_AND_WS);
  await onNetworkEvents;

  info("Test with the first request in the list visible");
  EventUtils.sendMouseEvent(
    { type: "click" },
    document.querySelector(".requests-list-filter-all-button")
  );
  testDetailsPanel(true, 0);

  info("Test with first request in the list not visible");
  EventUtils.sendMouseEvent(
    { type: "click" },
    document.querySelector(".requests-list-filter-all-button")
  );
  EventUtils.sendMouseEvent(
    { type: "click" },
    document.querySelector(".requests-list-filter-js-button")
  );
  testFilterButtons(monitor, "js");
  testDetailsPanel(true, 2);

  info(
    "Test with no request in the list visible i.e. no request match the filters"
  );
  EventUtils.sendMouseEvent(
    { type: "click" },
    document.querySelector(".requests-list-filter-all-button")
  );
  setFreetextFilter("foobar");
  // The network details panel should not open as there are no available requests visible
  testDetailsPanel(false);

  await teardown(monitor);

  function getSelectedIndex(state) {
    if (!state.requests.selectedId) {
      return -1;
    }
    return getSortedRequests(state).findIndex(
      r => r.id === state.requests.selectedId
    );
  }

  function testDetailsPanel(shouldPanelOpen, selectedItemIndex = 0) {
    // Expected default state should be panel closed
    ok(
      !document.querySelector(".sidebar-toggle"),
      "The pane toggle button should not be visible."
    );
    is(
      !!document.querySelector(".network-details-bar"),
      false,
      "The details pane should still be hidden."
    );
    is(
      getSelectedRequest(store.getState()),
      undefined,
      "There should still be no selected item in the requests menu."
    );

    // Trigger the details panel toggle action
    store.dispatch(Actions.toggleNetworkDetails());

    const toggleButton = document.querySelector(".sidebar-toggle");

    if (shouldPanelOpen) {
      is(
        toggleButton.classList.contains("pane-collapsed"),
        false,
        "The pane toggle button should now indicate that the details pane is " +
          "not collapsed anymore after being pressed."
      );
      is(
        !!document.querySelector(".network-details-bar"),
        true,
        "The details pane should not be hidden after toggle button was pressed."
      );
      isnot(
        getSelectedRequest(store.getState()),
        undefined,
        "There should be a selected item in the requests menu."
      );
      is(
        getSelectedIndex(store.getState()),
        selectedItemIndex,
        `The item index ${selectedItemIndex} should be selected in the requests menu.`
      );
      // Close the panel
      EventUtils.sendMouseEvent({ type: "click" }, toggleButton);
    } else {
      ok(!toggleButton, "The pane toggle button should be not visible.");
      is(
        !!document.querySelector(".network-details-bar"),
        false,
        "The details pane should still be hidden."
      );
      is(
        getSelectedRequest(store.getState()),
        undefined,
        "There should still be no selected item in the requests menu."
      );
    }
  }
});
