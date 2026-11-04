/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

/**
 * Test that a WebSocket payload holding several JSON values, one per line, is
 * displayed as a JSON Lines tree, and that ordinary multi-line text is not.
 */

"use strict";

add_task(async function testJsonlPayload() {
  const { monitor, messagesView } = await sendAndSelectFrame(
    `{"id":1}\n{"id":2}\n{"id":3}\n`
  );

  await waitFor(
    () => messagesView.querySelector(".properties-view"),
    "Wait for the JSON Lines tree to be displayed"
  );

  is(
    messagesView.querySelector(".data-label").innerText,
    "JSON Lines",
    "The JSON Lines payload panel should be displayed"
  );
  // The messages panel auto-expands the tree, so only look at the top level.
  is(
    messagesView.querySelectorAll('.treeLabel[data-level="0"]').length,
    3,
    "There should be one entry per line of the payload"
  );
  ok(
    messagesView.querySelector("#\\/0"),
    "The first entry should be displayed"
  );
  ok(messagesView.querySelector("#\\/2"), "The last entry should be displayed");

  await teardown(monitor);
});

add_task(async function testPlainMultilineText() {
  const { monitor, messagesView } = await sendAndSelectFrame(`1\n2\n3`);

  await waitFor(
    () => messagesView.querySelector(".message-rawData-payload"),
    "Wait for the raw payload to be displayed"
  );

  is(
    messagesView.querySelector(".properties-view"),
    null,
    "Lines which aren't JSON objects should not be treated as JSON Lines"
  );

  await teardown(monitor);
});

/**
 * Opens the netmonitor, sends the given payload over a WebSocket and selects
 * the frame it was sent in.
 *
 * @param {string} payload
 * @returns {object} shape:
 *  {object} monitor: the netmonitor panel
 *  {Element} messagesView: the messages view element
 */
async function sendAndSelectFrame(payload) {
  const { tab, monitor } = await initNetMonitor(WS_PAGE_URL, {
    requestCount: 1,
  });

  const { document, store, windowRequire } = monitor.panelWin;
  const Actions = windowRequire("devtools/client/netmonitor/src/actions/index");
  store.dispatch(Actions.batchEnable(false));

  const onNetworkEvents = waitForNetworkEvents(monitor, 1);
  await SpecialPowers.spawn(tab.linkedBrowser, [payload], async data => {
    await content.wrappedJSObject.openConnection(0);
    content.wrappedJSObject.sendData(data);
  });
  await onNetworkEvents;

  const requests = document.querySelectorAll(".request-list-item");
  is(requests.length, 1, "There should be one request");

  const onDomUpdated = waitForDOM(
    document,
    "#messages-view .message-list-table .message-list-item",
    2
  );
  clickOnRequestRow(requests[0]);
  await clickOnSidebarTab(document, "response");
  await onDomUpdated;

  const frames = document.querySelectorAll(
    "#messages-view .message-list-table .message-list-item"
  );
  is(frames.length, 2, "There should be two frames");

  // The MessagePayload component parses the payload asynchronously.
  await waitForTick();
  clickInView(frames[0]);

  return { monitor, messagesView: document.querySelector("#messages-view") };
}
