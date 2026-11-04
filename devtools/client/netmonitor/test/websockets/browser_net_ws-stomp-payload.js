/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Test that WebSocket payloads containing a STOMP formatted message are parsed
 * correctly and displayed in a user friendly way
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
    content.wrappedJSObject.sendData(
      `SEND\nx-firefox-test:true\ncontent-length:17\n\n[{"key":"value"}]\u0000\n`
    );
  });
  await onNetworkEvents;

  const requests = document.querySelectorAll(".request-list-item");
  is(requests.length, 1, "There should be one request");

  // Wait for all sent/received messages to be displayed in DevTools
  let onDomUpdated = waitForDOM(
    document,
    "#messages-view .message-list-table .message-list-item",
    2
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
  is(frames.length, 2, "There should be two frames");

  // Wait for next tick to do async stuff (The MessagePayload component uses the async function getMessagePayload)
  await waitForTick();
  const waitForData = waitForDOM(document, "#messages-view .properties-view");
  const [requestFrame] = frames;
  clickInView(requestFrame);

  await waitForData;

  is(
    document.querySelector("#messages-view .data-label").innerText,
    "STOMP",
    "The STOMP payload panel should be displayed"
  );

  ok(
    document.querySelector("#messages-view .treeTable"),
    "A tree table should be used to display the formatted payload"
  );

  ok(
    document.getElementById("/command"),
    "The message 'command' should be displayed"
  );
  ok(
    document.getElementById("/headers"),
    "The message 'headers' should be displayed"
  );
  ok(
    document.getElementById("/body"),
    "The message 'body' should be displayed"
  );

  // Toggle raw data display
  onDomUpdated = waitForDOM(
    document,
    "#messages-view .message-rawData-payload"
  );
  const rawDataToggle = document.querySelector(
    "#messages-view .devtools-checkbox-toggle"
  );
  clickElement(rawDataToggle, monitor);
  await onDomUpdated;

  is(
    document.querySelector("#messages-view .data-label").innerText,
    "Raw Data (63 B)",
    "The raw data payload info should be displayed"
  );

  is(
    document.querySelector("#messages-view .message-rawData-payload").value,
    `SEND\nx-firefox-test:true\ncontent-length:17\n\n[{"key":"value"}]\u0000\n`,
    "The raw data must be shown correctly"
  );

  // Close WS connection
  await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
    await content.wrappedJSObject.closeConnection();
  });

  await teardown(monitor);
});
