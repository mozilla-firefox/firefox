/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Test that sent and received WebSocket text frames display, copy, and filter as
 * Unicode, including long payloads and a leading U+FEFF. Frame sizes and totals
 * should still count UTF-8 bytes.
 */
add_task(async function () {
  const { tab, monitor } = await initNetMonitor(WS_PAGE_URL, {
    requestCount: 1,
  });
  const { document, store, windowRequire } = monitor.panelWin;
  const Actions = windowRequire("devtools/client/netmonitor/src/actions/index");
  const { getDisplayedMessagesSummary } = windowRequire(
    "devtools/client/netmonitor/src/selectors/messages"
  );
  const { getFormattedSize } = windowRequire(
    "devtools/client/netmonitor/src/utils/format-utils"
  );
  store.dispatch(Actions.batchEnable(false));

  const onNetworkEvents = waitForNetworkEvents(monitor, 1);
  await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
    await content.wrappedJSObject.openConnection(0);
  });
  await onNetworkEvents;

  clickOnRequestRow(document.querySelector(".request-list-item"));
  clickOnSidebarTab(document, "response");
  store.dispatch(Actions.toggleMessageColumn("size"));

  const cases = [
    { payload: "\u2665", preview: "\u2665", byteLength: 3 },
    { payload: "\ufeffa", preview: "\ufeffa", byteLength: 4 },
    {
      payload: "caf\u00e9 \u{1f30d}",
      preview: "caf\u00e9 \u{1f30d}",
      byteLength: 10,
    },
    {
      payload: "\u2665".repeat(10001),
      preview: "\u2665".repeat(1000),
      byteLength: 30003,
    },
  ];
  let frameCount = 0;
  let totalBytes = 0;
  for (const { payload, preview, byteLength } of cases) {
    const onFrames = waitForDOM(document, ".message-list-item", frameCount + 2);
    await SpecialPowers.spawn(tab.linkedBrowser, [payload], async text => {
      content.wrappedJSObject.sendData(text);
    });
    await onFrames;
    const frames = document.querySelectorAll(".message-list-item");
    for (const frame of [...frames].slice(frameCount)) {
      is(
        frame.querySelector(".message-list-payload").textContent,
        " " + preview,
        "The UTF-8 preview is displayed"
      );
      is(
        frame.querySelector(".message-list-size").textContent,
        getFormattedSize(byteLength),
        "Frame size counts UTF-8 bytes"
      );

      EventUtils.sendMouseEvent({ type: "click" }, frame);
      await TestUtils.waitForCondition(
        () =>
          document.querySelector(".message-rawData-payload")?.value === payload,
        "The selected message details contain the decoded payload"
      );
      is(
        document.querySelector(".message-rawData-payload").value,
        payload,
        "The full UTF-8 payload is displayed"
      );
      ok(
        document
          .querySelector(".message-payload .data-label")
          .textContent.includes(getFormattedSize(byteLength)),
        "Payload details show the UTF-8 byte size"
      );

      EventUtils.sendMouseEvent({ type: "contextmenu" }, frame);
      await waitForClipboardPromise(
        () =>
          selectContextMenuItem(monitor, "message-list-context-copy-message"),
        payload
      );
    }
    frameCount += 2;
    totalBytes += byteLength;
    const summary = getDisplayedMessagesSummary(store.getState());
    is(summary.sentSize, totalBytes, "Sent total counts UTF-8 bytes");
    is(summary.receivedSize, totalBytes, "Received total counts UTF-8 bytes");
    is(summary.totalSize, totalBytes * 2, "Combined total counts UTF-8 bytes");
  }

  store.dispatch(Actions.setMessageFilterText("caf\u00e9"));
  await waitUntil(
    () => document.querySelectorAll(".message-list-item").length === 2
  );
  for (const frame of document.querySelectorAll(".message-list-item")) {
    is(
      frame.querySelector(".message-list-payload").textContent,
      " caf\u00e9 \u{1f30d}",
      "Filter matches decoded text"
    );
  }

  await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
    await content.wrappedJSObject.closeConnection();
  });
  await teardown(monitor);
});
