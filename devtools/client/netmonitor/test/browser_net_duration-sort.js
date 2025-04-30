/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Test if initial and pending requests are sorted correctly in the Duration column.
 */

add_task(async function () {
  const { monitor } = await initNetMonitor(HTTPS_SIMPLE_URL, {
    requestCount: 1,
  });
  const { document, store, windowRequire } = monitor.panelWin;
  const Actions = windowRequire("devtools/client/netmonitor/src/actions/index");
  store.dispatch(Actions.batchEnable(false));

  const delay1 = 100;
  const delay2 = 500;
  const delay3 = 1500;
  const delay4 = 2000;

  info("Starting test... ");
  info("Sending initial requests.");
  const initialRequests = [
    `sjs_delay-test-server.sjs?delay=${delay1}`,
    `sjs_delay-test-server.sjs?delay=${delay2}`,
    `sjs_delay-test-server.sjs?delay=${delay3}`,
  ];
  let waitForResponse = waitForNetworkEvents(monitor, 3);
  sendRequests(initialRequests);
  await waitForResponse;

  info("Testing initial items duration sort, ascending.");
  EventUtils.sendMouseEvent(
    { type: "click" },
    document.querySelector("#requests-list-duration-button")
  );
  await testInitialContents([0, 1, 2]);

  info("Testing initial items duration sort, descending.");
  EventUtils.sendMouseEvent(
    { type: "click" },
    document.querySelector("#requests-list-duration-button")
  );
  await testInitialContents([2, 1, 0]);

  info("Sending new requests.");
  const newRequests = [
    "sjs_long-polling-server.sjs",
    `sjs_delay-test-server.sjs?delay=${delay4}`,
  ];
  waitForResponse = waitForNetworkEvents(monitor, 1, {
    expectedEventTimings: 1,
  });
  sendRequests(newRequests);
  await waitForResponse;

  info("Testing pending items duration sort, ascending.");
  EventUtils.sendMouseEvent(
    { type: "click" },
    document.querySelector("#requests-list-duration-button")
  );
  await testPendingContents([0, 1, 2, 3, 4]);

  info("Testing pending items duration sort, descending.");
  EventUtils.sendMouseEvent(
    { type: "click" },
    document.querySelector("#requests-list-duration-button")
  );
  await testPendingContents([4, 3, 2, 1, 0]);

  info("Complete pending request.");
  waitForResponse = waitForNetworkEvents(monitor, 1, {
    expectedPayloadReady: 2,
  });
  sendRequests(["sjs_long-polling-server.sjs?unblock"]);
  await waitForResponse;

  info("Testing resolved items duration sort, ascending.");
  EventUtils.sendMouseEvent(
    { type: "click" },
    document.querySelector("#requests-list-duration-button")
  );
  await testResolvedContents([0, 1, 2, 3, 4, 5]);

  info("Testing resolved items duration sort, descending.");
  EventUtils.sendMouseEvent(
    { type: "click" },
    document.querySelector("#requests-list-duration-button")
  );
  await testResolvedContents([5, 4, 3, 2, 1, 0]);

  return teardown(monitor);

  async function sendRequests(urls) {
    await SpecialPowers.spawn(gBrowser.selectedBrowser, [urls], _urls => {
      _urls.forEach(u => content.fetch(u));
    });
  }

  async function testInitialContents([a, b, c]) {
    const requestItems = [
      ...document.querySelectorAll(".request-list-item .requests-list-url"),
    ].map(el => el.innerText);

    is(
      requestItems[a],
      DELAY_SJS + `?delay=${delay1}`,
      `${delay1}ms request sorted correctly.`
    );
    is(
      requestItems[b],
      DELAY_SJS + `?delay=${delay2}`,
      `${delay2}ms request sorted correctly.`
    );
    is(
      requestItems[c],
      DELAY_SJS + `?delay=${delay3}`,
      `${delay3}ms request sorted correctly.`
    );
  }

  async function testPendingContents([a, b, c, d, e]) {
    const requestItems = [
      ...document.querySelectorAll(".request-list-item .requests-list-url"),
    ].map(el => el.innerText);

    is(
      requestItems[a],
      DELAY_SJS + `?delay=${delay1}`,
      `${delay1}ms request sorted correctly.`
    );
    is(
      requestItems[b],
      DELAY_SJS + `?delay=${delay2}`,
      `${delay2}ms request sorted correctly.`
    );
    is(
      requestItems[c],
      DELAY_SJS + `?delay=${delay3}`,
      `${delay3}ms request sorted correctly.`
    );
    is(
      requestItems[d],
      DELAY_SJS + `?delay=${delay4}`,
      `${delay4}ms request sorted correctly.`
    );
    is(
      requestItems[e],
      "https://example.com/browser/devtools/client/netmonitor/test/sjs_long-polling-server.sjs",
      "Pending request sorted correctly."
    );
  }

  async function testResolvedContents([a, b, c, d, e, f]) {
    const requestItems = [
      ...document.querySelectorAll(".request-list-item .requests-list-url"),
    ].map(el => el.innerText);

    is(
      requestItems[a],
      "https://example.com/browser/devtools/client/netmonitor/test/sjs_long-polling-server.sjs?unblock",
      "Unblock request sorted correctly."
    );
    is(
      requestItems[b],
      DELAY_SJS + `?delay=${delay1}`,
      `${delay1}ms request sorted correctly.`
    );
    is(
      requestItems[c],
      DELAY_SJS + `?delay=${delay2}`,
      `${delay2}ms request sorted correctly.`
    );
    is(
      requestItems[d],
      DELAY_SJS + `?delay=${delay3}`,
      `${delay3}ms request sorted correctly.`
    );
    is(
      requestItems[e],
      DELAY_SJS + `?delay=${delay4}`,
      `${delay4}ms request sorted correctly.`
    );
    is(
      requestItems[f],
      "https://example.com/browser/devtools/client/netmonitor/test/sjs_long-polling-server.sjs",
      "Long polling request sorted correctly."
    );
  }
});
