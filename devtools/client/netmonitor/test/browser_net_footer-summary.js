/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Test if the summary text displayed in the network requests menu footer is correct.
 */

add_task(async function () {
  const {
    getFormattedSize,
    getFormattedTime,
  } = require("resource://devtools/client/netmonitor/src/utils/format-utils.js");

  requestLongerTimeout(2);

  const { tab, monitor } = await initNetMonitor(FILTERING_URL, {
    requestCount: 1,
  });
  info("Starting test... ");

  const { document, store, windowRequire } = monitor.panelWin;
  const Actions = windowRequire("devtools/client/netmonitor/src/actions/index");
  const { getDisplayedRequestsSummary } = windowRequire(
    "devtools/client/netmonitor/src/selectors/index"
  );
  const l10n = new Localization(["devtools/client/netmonitor.ftl"], true);

  store.dispatch(Actions.batchEnable(false));
  testStatus();

  for (let i = 0; i < 2; i++) {
    info(`Performing requests in batch #${i}`);
    const onNetworkEvents = waitForNetworkEvents(monitor, 8);
    await SpecialPowers.spawn(tab.linkedBrowser, [], async function () {
      content.wrappedJSObject.performRequests(
        '{ "getMedia": true, "getFlash": true }'
      );
    });
    await onNetworkEvents;

    testStatus();

    const buttons = ["html", "css", "js", "xhr", "fonts", "images", "media"];
    for (const button of buttons) {
      const buttonEl = document.querySelector(
        `.requests-list-filter-${button}-button`
      );
      EventUtils.sendMouseEvent({ type: "click" }, buttonEl);
      testStatus();
    }
  }

  await teardown(monitor);

  function testStatus() {
    const state = store.getState();
    const totalRequestsCount = state.requests.requests.length;
    const requestsSummary = getDisplayedRequestsSummary(state);
    info(
      `Current requests: ${requestsSummary.count} of ${totalRequestsCount}.`
    );

    const countEl = document.querySelector(
      ".requests-list-network-summary-count"
    );
    info(`Current summary count: ${countEl.textContent}`);

    const expectedCount = l10n.formatValueSync(
      "network-menu-summary-requests-count",
      { requestCount: requestsSummary.count }
    );

    is(
      countEl.textContent,
      expectedCount,
      "The current summary count is correct."
    );

    if (!totalRequestsCount || !requestsSummary.count) {
      return;
    }

    const transferEl = document.querySelector(
      ".requests-list-network-summary-transfer"
    );
    info(`Current summary transfer: ${transferEl.textContent}`);

    const expectedTransfer = l10n.formatValueSync(
      "network-menu-summary-transferred",
      {
        formattedContentSize: getFormattedSize(requestsSummary.contentSize),
        formattedTransferredSize: getFormattedSize(
          requestsSummary.transferredSize
        ),
      }
    );

    is(
      transferEl.textContent,
      expectedTransfer,
      "The current summary transfer is correct."
    );

    const finishEl = document.querySelector(
      ".requests-list-network-summary-finish"
    );
    info(`Current summary finish: ${finishEl.textContent}`);

    const expectedFinish = l10n.formatValueSync("network-menu-summary-finish", {
      formattedTime: getFormattedTime(requestsSummary.ms),
    });

    info(`Computed total bytes: ${requestsSummary.bytes}`);
    info(`Computed total ms: ${requestsSummary.ms}`);

    is(
      finishEl.textContent,
      expectedFinish,
      "The current summary finish is correct."
    );
  }
});
