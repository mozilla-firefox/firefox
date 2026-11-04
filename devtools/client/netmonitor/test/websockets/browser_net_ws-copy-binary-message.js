/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Test that WS binary messages can be copied.
 */

add_task(async function () {
  const { tab, monitor } = await initNetMonitor(WS_PAGE_URL, {
    requestCount: 1,
  });
  info("Starting test... ");

  const { document, store, windowRequire } = monitor.panelWin;
  const Actions = windowRequire("devtools/client/netmonitor/src/actions/index");

  store.dispatch(Actions.batchEnable(false));

  // Wait for WS connections to be established + send message
  const onNetworkEvents = waitForNetworkEvents(monitor, 1);
  const data = {
    text: "something",
    hex: "736f6d657468696e67",
    base64: "c29tZXRoaW5n",
  };
  await SpecialPowers.spawn(tab.linkedBrowser, [data.text], async text => {
    await content.wrappedJSObject.openConnection(0);
    content.wrappedJSObject.sendData(text, true);
  });
  await onNetworkEvents;

  const requests = document.querySelectorAll(".request-list-item");

  // Wait for all sent/received messages to be displayed in DevTools
  const onDomUpdated = waitForDOM(
    document,
    "#messages-view .message-list-table .message-list-item",
    2
  );

  // Select the websocket request
  clickOnRequestRow(requests[0]);

  // Test that 'Save Response As' is not in the context menu
  EventUtils.sendMouseEvent({ type: "contextmenu" }, requests[0]);

  ok(
    !getContextMenuItem(monitor, "request-list-context-save-response-as"),
    "The 'Save Response As' context menu item should be hidden"
  );

  // Close context menu.
  const contextMenu = monitor.toolbox.topDoc.querySelector(
    'popupset menupopup[menu-api="true"]'
  );
  const popupHiddenPromise = BrowserTestUtils.waitForEvent(
    contextMenu,
    "popuphidden"
  );
  contextMenu.hidePopup();
  await popupHiddenPromise;

  // Click on the "Response" panel
  clickOnSidebarTab(document, "response");
  await onDomUpdated;

  // Get all messages present in the "Response" panel
  const messages = document.querySelectorAll(
    "#messages-view .message-list-table .message-list-item"
  );

  // Test all types for both the sent and received message.
  for (const message of messages) {
    for (const [type, expectedValue] of Object.entries(data)) {
      const menuItemId = `message-list-context-copy-message-${type}`;
      EventUtils.sendMouseEvent({ type: "contextmenu" }, message);
      is(
        !!getContextMenuItem(monitor, menuItemId),
        true,
        `Could not find "${type}" copy option.`
      );

      await waitForClipboardPromise(
        async function setup() {
          await selectContextMenuItem(monitor, menuItemId);
        },
        function validate(result) {
          return result === expectedValue;
        }
      );
    }
  }

  // Close WS connection
  await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
    await content.wrappedJSObject.closeConnection();
  });

  await teardown(monitor);
});
