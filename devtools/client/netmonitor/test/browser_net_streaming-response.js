/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Tests if reponses from streaming content types (MPEG-DASH, HLS) are
 * displayed as XML or plain text
 */

add_task(async function () {
  const { tab, monitor } = await initNetMonitor(CUSTOM_GET_URL, {
    requestCount: 1,
  });

  info("Starting test... ");
  const { document, store, windowRequire } = monitor.panelWin;
  const Actions = windowRequire("devtools/client/netmonitor/src/actions/index");
  const { getDisplayedRequests, getSortedRequests } = windowRequire(
    "devtools/client/netmonitor/src/selectors/index"
  );

  store.dispatch(Actions.batchEnable(false));

  const REQUESTS = [
    ["hls-m3u8", /^#EXTM3U/],
    ["mpeg-dash", /^<\?xml/],
  ];

  const onNetworkEvents = waitForNetworkEvents(monitor, REQUESTS.length);
  for (const [fmt] of REQUESTS) {
    const url = CONTENT_TYPE_SJS + "?fmt=" + fmt;
    await SpecialPowers.spawn(
      tab.linkedBrowser,
      [{ url }],
      async function (args) {
        content.wrappedJSObject.performRequests(1, args.url);
      }
    );
  }
  await onNetworkEvents;

  const requestItems = document.querySelectorAll(".request-list-item");
  for (const requestItem of requestItems) {
    requestItem.scrollIntoView();
    const requestsListStatus = requestItem.querySelector(".status-code");
    EventUtils.sendMouseEvent({ type: "mouseover" }, requestsListStatus);
    await waitUntil(() => requestsListStatus.title);
    await waitForDOM(requestItem, ".requests-list-timings-total");
  }

  for (const [i, [fmt]] of REQUESTS.entries()) {
    await verifyRequestItemTarget(
      document,
      getDisplayedRequests(store.getState()),
      getSortedRequests(store.getState())[i],
      "GET",
      CONTENT_TYPE_SJS + "?fmt=" + fmt,
      {
        status: 200,
        statusText: "OK",
      }
    );
  }

  const onDomUpdated = waitForDOM(document, "#response-panel");
  store.dispatch(Actions.toggleNetworkDetails());
  clickOnSidebarTab(document, "response");
  await onDomUpdated;

  store.dispatch(Actions.selectRequest(null));

  await selectIndexAndWaitForSourceEditor(monitor, 0);
  // the hls-m3u8 part
  testEditorContent(REQUESTS[0]);

  await selectIndexAndWaitForSourceEditor(monitor, 1);
  // the mpeg-dash part
  testEditorContent(REQUESTS[1]);

  return teardown(monitor);

  function testEditorContent([fmt, textRe]) {
    ok(
      getCodeMirrorValue(monitor).match(textRe),
      "The text shown in the source editor for " + fmt + " is correct."
    );
  }
});
