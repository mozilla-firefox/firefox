/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Test that WS connection is established successfully and clearing messages works correctly.
 */

add_task(async function () {
  const { tab, monitor } = await initNetMonitor(WS_PAGE_URL, {
    requestCount: 1,
  });
  info("Starting test... ");

  const { document, store, windowRequire } = monitor.panelWin;
  const Actions = windowRequire("devtools/client/netmonitor/src/actions/index");

  store.dispatch(Actions.batchEnable(false));

  // Wait for WS connection(s) to be established + send messages
  const onNetworkEvents = waitForNetworkEvents(monitor, 2);
  await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
    await content.wrappedJSObject.openConnection(2);
    await content.wrappedJSObject.openConnection(1);
  });
  await onNetworkEvents;

  const requests = document.querySelectorAll(".request-list-item");
  is(requests.length, 2, "There should be two requests");

  // Wait for all sent/received messages to be displayed in DevTools
  let onDomUpdated = waitForDOM(
    document,
    "#messages-view .message-list-table .message-list-item",
    4
  );

  // Select the first request
  clickOnRequestRow(requests[0]);

  // Click on the "Response" panel
  clickOnSidebarTab(document, "response");
  await onDomUpdated;

  // Get all messages present in the "Response" panel
  const frames = document.querySelectorAll(
    "#messages-view .message-list-table .message-list-item"
  );

  // Check expected results
  is(frames.length, 4, "There should be four frames");

  // Clear messages
  const clearButton = document.querySelector(
    "#messages-view .message-list-clear-button"
  );
  clearButton.click();
  is(
    document.querySelectorAll(".message-list-empty-notice").length,
    1,
    "Empty notice visible"
  );

  // Select the second request and check that the messages are not cleared
  onDomUpdated = waitForDOM(
    document,
    "#messages-view .message-list-table .message-list-item",
    2
  );
  clickOnRequestRow(requests[1]);
  await onDomUpdated;
  const secondRequestFrames = document.querySelectorAll(
    "#messages-view .message-list-table .message-list-item"
  );
  is(secondRequestFrames.length, 2, "There should be two frames");

  // Close WS connection
  await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
    await content.wrappedJSObject.closeConnection();
  });

  await teardown(monitor);
});
