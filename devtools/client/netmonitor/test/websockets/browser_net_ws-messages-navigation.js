/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Test that WS messages can be navigated between using the keyboard.
 */

add_task(async function () {
  const { tab, monitor } = await initNetMonitor(WS_PAGE_URL, {
    requestCount: 1,
  });
  info("Starting test... ");

  const { document, store, windowRequire } = monitor.panelWin;
  const Actions = windowRequire("devtools/client/netmonitor/src/actions/index");

  store.dispatch(Actions.batchEnable(false));

  // Wait for WS connections to be established + send messages
  const onNetworkEvents = waitForNetworkEvents(monitor, 1);
  await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
    await content.wrappedJSObject.openConnection(0);
    // Send 5 WS messages
    Array(5)
      .fill(undefined)
      .forEach((_, index) => {
        content.wrappedJSObject.sendData(index);
      });
  });
  await onNetworkEvents;

  const requests = document.querySelectorAll(".request-list-item");
  is(requests.length, 1, "There should be one request");

  // Wait for all sent/received messages to be displayed in DevTools
  const onDomUpdated = waitForDOM(
    document,
    "#messages-view .message-list-table .message-list-item",
    10
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
  is(frames.length, 10, "There should be ten frames");
  // Wait for next tick to do async stuff (The MessagePayload component uses the async function getMessagePayload)
  await waitForTick();

  const waitForSelected = waitForDOM(
    document,
    // The first message is actually the second child, there is a hidden row.
    `.message-list-item:nth-child(${2}).selected`
  );
  clickInView(frames[0]);
  await waitForSelected;

  const checkSelected = messageRowNumber => {
    is(
      Array.from(frames).findIndex(el => el.matches(".selected")),
      messageRowNumber - 1,
      `Message ${messageRowNumber} should be selected.`
    );
  };

  // Need focus for keyboard shortcuts to work
  frames[0].focus();

  checkSelected(1);

  EventUtils.sendKey("DOWN", window);
  checkSelected(2);

  EventUtils.sendKey("UP", window);
  checkSelected(1);

  EventUtils.sendKey("PAGE_DOWN", window);
  checkSelected(3);

  EventUtils.sendKey("PAGE_UP", window);
  checkSelected(1);

  EventUtils.sendKey("END", window);
  checkSelected(10);

  EventUtils.sendKey("HOME", window);
  checkSelected(1);

  // Close WS connection
  await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
    await content.wrappedJSObject.closeConnection();
  });

  await teardown(monitor);
});
