/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

Services.scriptloader.loadSubScript(
  "chrome://mochitests/content/browser/devtools/client/webconsole/test/browser/shared-head.js",
  this
);

/**
 * Tests if the clear button empties the request menu.
 */

add_task(async function () {
  Services.prefs.setBoolPref("devtools.webconsole.filter.net", true);

  const { monitor, toolbox } = await initNetMonitor(HTTPS_SIMPLE_URL, {
    requestCount: 1,
  });
  info("Starting test... ");

  const { document, store, windowRequire } = monitor.panelWin;
  const Actions = windowRequire("devtools/client/netmonitor/src/actions/index");
  const clearButton = document.querySelector(".requests-list-clear-button");

  store.dispatch(Actions.batchEnable(false));

  // Make sure we start in a sane state
  assertNoRequestState();

  // Load one request and assert it shows up in the list
  let onNetworkEvents = waitForNetworkEvents(monitor, 1);
  await reloadSelectedTab();
  await onNetworkEvents;

  assertSingleRequestState();
  assertNetworkEventResourceState(1);

  info("Swith to the webconsole and wait for network logs");
  const onWebConsole = monitor.toolbox.once("webconsole-selected");
  const { hud } = await monitor.toolbox.selectTool("webconsole");
  await onWebConsole;

  info("Wait for request");
  await waitFor(() => findMessageByType(hud, HTTPS_SIMPLE_URL, ".network"));

  info("Switch back the the netmonitor");
  await monitor.toolbox.selectTool("netmonitor");

  // Click clear and make sure the requests are gone
  let waitRequestListCleared = waitForEmptyRequestList(document);
  EventUtils.sendMouseEvent({ type: "click" }, clearButton);
  await waitRequestListCleared;

  assertNoRequestState();
  assertNetworkEventResourceState(0);

  info(
    "Swith back to the webconsole to assert that the cleared request gets disabled"
  );
  await monitor.toolbox.selectTool("webconsole");

  info("Wait for network request to show and that its disabled");

  await waitFor(() =>
    findMessageByType(hud, HTTPS_SIMPLE_URL, ".network.disabled")
  );

  // Switch back to the netmonitor.
  await monitor.toolbox.selectTool("netmonitor");

  // Load a second request and make sure they still show up
  onNetworkEvents = waitForNetworkEvents(monitor, 1);
  await reloadSelectedTab();
  await onNetworkEvents;

  assertSingleRequestState();
  assertNetworkEventResourceState(1);

  // Make sure we can now open the network details panel
  store.dispatch(Actions.toggleNetworkDetails());
  const detailsPanelToggleButton = document.querySelector(".sidebar-toggle");
  // Wait for the details panel to finish fetching the headers information
  await waitForRequestData(store, ["requestHeaders", "responseHeaders"]);

  ok(
    detailsPanelToggleButton &&
      !detailsPanelToggleButton.classList.contains("pane-collapsed"),
    "The details pane should be visible."
  );

  // Click clear and make sure the details pane closes
  waitRequestListCleared = waitForEmptyRequestList(document);
  EventUtils.sendMouseEvent({ type: "click" }, clearButton);
  await waitRequestListCleared;

  assertNoRequestState();
  assertNetworkEventResourceState(0);

  ok(
    !document.querySelector(".network-details-bar"),
    "The details pane should not be visible clicking 'clear'."
  );

  return teardown(monitor);

  /**
   * Asserts the state of the network monitor when one request has loaded
   */
  function assertSingleRequestState() {
    is(
      store.getState().requests.requests.length,
      1,
      "The request menu should have one item at this point."
    );
  }

  /**
   * Asserts the state of the network monitor when no requests have loaded
   */
  function assertNoRequestState() {
    is(
      store.getState().requests.requests.length,
      0,
      "The request menu should be empty at this point."
    );
  }

  function assertNetworkEventResourceState(expectedNoOfNetworkEventResources) {
    const actualNoOfNetworkEventResources =
      toolbox.commands.resourceCommand.getAllResources(
        toolbox.commands.resourceCommand.TYPES.NETWORK_EVENT
      ).length;

    is(
      actualNoOfNetworkEventResources,
      expectedNoOfNetworkEventResources,
      `The expected number of network resources is correctly ${actualNoOfNetworkEventResources}`
    );
  }

  function waitForEmptyRequestList(doc) {
    info("Wait for request list to clear");
    return waitFor(() => !!doc.querySelector(".request-list-empty-notice"));
  }
});
