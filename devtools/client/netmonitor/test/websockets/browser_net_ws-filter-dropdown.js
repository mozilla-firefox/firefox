/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Test that WS connection is established successfully and filtered messages using the dropdown menu are correct.
 */

add_task(async function () {
  const { tab, monitor } = await initNetMonitor(WS_PAGE_URL, {
    requestCount: 1,
  });
  info("Starting test... ");

  const { document, store, windowRequire } = monitor.panelWin;
  const Actions = windowRequire("devtools/client/netmonitor/src/actions/index");

  store.dispatch(Actions.batchEnable(false));

  // Wait for WS connection to be established + send messages
  const onNetworkEvents = waitForNetworkEvents(monitor, 2);
  await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
    await content.wrappedJSObject.openConnection(2);
    await content.wrappedJSObject.openConnection(3);
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

  // Click on filter menu
  EventUtils.sendMouseEvent(
    { type: "click" },
    document.querySelector("#frame-filter-menu")
  );

  // Click on "sent" option and check
  await selectContextMenuItem(monitor, "message-list-context-filter-sent");

  const sentFrames = document.querySelectorAll(
    "#messages-view .message-list-table .message-list-item"
  );
  is(sentFrames.length, 2, "There should be two frames");
  is(
    sentFrames[0].classList.contains("sent"),
    true,
    "The payload type is 'Sent'"
  );
  is(
    sentFrames[1].classList.contains("sent"),
    true,
    "The payload type is 'Sent'"
  );

  // Click on filter menu
  EventUtils.sendMouseEvent(
    { type: "click" },
    document.querySelector("#frame-filter-menu")
  );

  // Click on "received" option and check
  await selectContextMenuItem(monitor, "message-list-context-filter-received");

  const receivedFrames = document.querySelectorAll(
    "#messages-view .message-list-table .message-list-item"
  );
  is(receivedFrames.length, 2, "There should be two frames");
  is(
    receivedFrames[0].classList.contains("received"),
    true,
    "The payload type is 'Received'"
  );
  is(
    receivedFrames[1].classList.contains("received"),
    true,
    "The payload type is 'Received'"
  );

  // Select the second request and check that the filter option is the same
  onDomUpdated = waitForDOM(
    document,
    "#messages-view .message-list-table .message-list-item",
    3
  );
  clickOnRequestRow(requests[1]);
  await onDomUpdated;
  const secondRequestFrames = document.querySelectorAll(
    "#messages-view .message-list-table .message-list-item"
  );
  is(secondRequestFrames.length, 3, "There should be three frames");
  is(
    secondRequestFrames[0].classList.contains("received"),
    true,
    "The payload type is 'Received'"
  );
  is(
    secondRequestFrames[1].classList.contains("received"),
    true,
    "The payload type is 'Received'"
  );
  is(
    secondRequestFrames[2].classList.contains("received"),
    true,
    "The payload type is 'Received'"
  );

  // Close WS connection
  await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
    await content.wrappedJSObject.closeConnection();
  });

  await teardown(monitor);
});
