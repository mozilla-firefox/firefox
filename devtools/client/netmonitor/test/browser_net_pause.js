/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Tests if the pause/resume button works.
 */
add_task(async function () {
  const { tab, monitor, toolbox } = await initNetMonitor(PAUSE_URL, {
    requestCount: 1,
  });
  info("Starting test... ");

  const { document, store, windowRequire } = monitor.panelWin;
  const Actions = windowRequire("devtools/client/netmonitor/src/actions/index");
  const pauseButton = document.querySelector(".requests-list-pause-button");

  store.dispatch(Actions.batchEnable(false));

  // Make sure we start in a sane state.
  assertRequestCount(store, 0);

  // Load one request and assert it shows up in the list.
  await performRequestAndWait(tab, monitor, SIMPLE_URL + "?id=1");
  assertRequestCount(store, 1);

  let noRequest = true;
  monitor.panelWin.api.once(TEST_EVENTS.NETWORK_EVENT, () => {
    noRequest = false;
  });

  monitor.panelWin.api.on(EVENTS.PAYLOAD_READY, () => {
    noRequest = false;
  });

  // Click pause, load second request and make sure they don't show up.
  EventUtils.sendMouseEvent({ type: "click" }, pauseButton);
  await waitForPauseButtonToChange(document, true);

  await performPausedRequest(tab, monitor, toolbox);

  ok(noRequest, "There should be no activity when paused.");
  assertRequestCount(store, 1);

  // Click pause again to resume monitoring. Load a third request
  // and make sure they will show up.
  EventUtils.sendMouseEvent({ type: "click" }, pauseButton);
  await waitForPauseButtonToChange(document, false);

  await performRequestAndWait(tab, monitor, SIMPLE_URL + "?id=2");

  ok(!noRequest, "There should be activity when resumed.");
  assertRequestCount(store, 2);

  // Click pause, reload the page and check that there are
  // some requests.
  EventUtils.sendMouseEvent({ type: "click" }, pauseButton);
  await waitForPauseButtonToChange(document, true);

  await waitForAllNetworkUpdateEvents();
  // Page reload should auto-resume
  await reloadSelectedTab();
  await waitForPauseButtonToChange(document, false);
  await performRequestAndWait(tab, monitor, SIMPLE_URL + "?id=3");

  ok(!noRequest, "There should be activity when resumed.");

  return teardown(monitor);
});

/**
 * Wait until a request is visible in the request list
 */
function waitForRequest(doc, url) {
  return waitUntil(() =>
    [...doc.querySelectorAll(".request-list-item .requests-list-file")].some(
      columns => columns.title.includes(url)
    )
  );
}

/**
 * Waits for the state of the paused/resume button to change.
 */
async function waitForPauseButtonToChange(doc, isPaused) {
  await waitUntil(
    () =>
      !!doc.querySelector(
        `.requests-list-pause-button[aria-pressed="${
          isPaused ? "true" : "false"
        }"]`
      )
  );
  ok(
    true,
    `The pause button is correctly in the ${
      isPaused ? "paused" : "resumed"
    } state`
  );
}

/**
 * Asserts the number of requests in the network monitor.
 */
function assertRequestCount(store, count) {
  is(
    store.getState().requests.requests.length,
    count,
    "There should be correct number of requests"
  );
}

/**
 * Execute simple GET request and wait till it's done.
 */
async function performRequestAndWait(tab, monitor, requestURL) {
  const onRequest = waitForRequest(monitor.panelWin.document, requestURL);
  await SpecialPowers.spawn(
    tab.linkedBrowser,
    [requestURL],
    async function (url) {
      await content.wrappedJSObject.performRequests(url);
    }
  );
  await onRequest;
}

/**
 * Execute simple GET request, and uses a one time listener to
 * know when the resource is available.
 */
async function performPausedRequest(tab, monitor, toolbox) {
  const { onResource: waitForEventWhenPaused } =
    await toolbox.commands.resourceCommand.waitForNextResource(
      toolbox.commands.resourceCommand.TYPES.NETWORK_EVENT,
      {
        ignoreExistingResources: true,
      }
    );
  await SpecialPowers.spawn(
    tab.linkedBrowser,
    [SIMPLE_URL],
    async function (url) {
      await content.wrappedJSObject.performRequests(url);
    }
  );
  // Wait for NETWORK_EVENT resources to be fetched, in order to ensure
  // that there is no pending request related to their processing.
  await waitForEventWhenPaused;
}
