/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Test that WS connection is established successfully and filtering messages using freetext works correctly.
 */

add_task(async function () {
  const { tab, monitor } = await initNetMonitor(WS_PAGE_URL, {
    requestCount: 1,
  });
  info("Starting test... ");

  const { document, store, windowRequire } = monitor.panelWin;
  const Actions = windowRequire("devtools/client/netmonitor/src/actions/index");
  const { getDisplayedMessages } = windowRequire(
    "devtools/client/netmonitor/src/selectors/messages"
  );

  store.dispatch(Actions.batchEnable(false));

  // Wait for WS connection(s) to be established + send messages
  const onNetworkEvents = waitForNetworkEvents(monitor, 2);
  await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
    await content.wrappedJSObject.openConnection(3);
    await content.wrappedJSObject.openConnection(1);
  });
  await onNetworkEvents;

  const requests = document.querySelectorAll(".request-list-item");
  is(requests.length, 2, "There should be two requests");

  // Wait for all sent/received messages to be displayed in DevTools
  const onDomUpdated = waitForDOM(
    document,
    "#messages-view .message-list-table .message-list-item",
    6
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
  is(frames.length, 6, "There should be six frames");

  // Fill filter input with text and check displayed messages
  const filterInput = document.querySelector(
    "#messages-view .devtools-filterinput"
  );
  filterInput.focus();
  typeInNetmonitor("Payload 2", monitor);

  // Wait till the text filter is applied.
  await waitUntil(() => getDisplayedMessages(store.getState()).length == 2);

  const filteredFrames = document.querySelectorAll(
    "#messages-view .message-list-table .message-list-item"
  );
  is(filteredFrames.length, 2, "There should be two frames");

  // Select the second request and check that the filter input is cleared
  clickOnRequestRow(requests[1]);
  // Wait till the text filter is applied. There should be two frames rendered
  await waitUntil(
    () =>
      document.querySelectorAll(
        "#messages-view .message-list-table .message-list-item"
      ).length == 2
  );
  const secondRequestFrames = document.querySelectorAll(
    "#messages-view .message-list-table .message-list-item"
  );
  is(secondRequestFrames.length, 2, "There should be two frames");
  is(filterInput.value, "", "The filter input is cleared");

  // Close WS connection
  await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
    await content.wrappedJSObject.closeConnection();
  });

  await teardown(monitor);
});
