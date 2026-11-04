/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Tests if very large response contents are just displayed as plain text.
 */

const HTML_LONG_URL = CONTENT_TYPE_SJS + "?fmt=html-long";

add_task(async function () {
  const { tab, monitor } = await initNetMonitor(CUSTOM_GET_URL, {
    requestCount: 1,
  });
  info("Starting test... ");

  // This test could potentially be slow because over 100 KB of stuff
  // is going to be requested and displayed in the source editor.
  requestLongerTimeout(2);

  const { document, store, windowRequire } = monitor.panelWin;
  const Actions = windowRequire("devtools/client/netmonitor/src/actions/index");
  const { getDisplayedRequests, getSortedRequests } = windowRequire(
    "devtools/client/netmonitor/src/selectors/index"
  );

  store.dispatch(Actions.batchEnable(false));

  const onNetworkEvents = waitForNetworkEvents(monitor, 1);
  await SpecialPowers.spawn(
    tab.linkedBrowser,
    [HTML_LONG_URL],
    async function (url) {
      content.wrappedJSObject.performRequests(1, url);
    }
  );
  await onNetworkEvents;

  const requestItem = document.querySelector(".request-list-item");
  requestItem.scrollIntoView();
  const requestsListStatus = requestItem.querySelector(".status-code");
  EventUtils.sendMouseEvent({ type: "mouseover" }, requestsListStatus);
  await waitUntil(() => requestsListStatus.title);
  await waitForDOM(requestItem, ".requests-list-timings-total");

  await verifyRequestItemTarget(
    document,
    getDisplayedRequests(store.getState()),
    getSortedRequests(store.getState())[0],
    "GET",
    CONTENT_TYPE_SJS + "?fmt=html-long",
    {
      status: 200,
      statusText: "OK",
    }
  );

  let onDomUpdated = waitForDOM(document, "#response-panel .data-header");
  store.dispatch(Actions.toggleNetworkDetails());
  clickOnSidebarTab(document, "response");
  await onDomUpdated;

  onDomUpdated = waitForDOM(document, "#response-panel .cm-content");
  const payloadHeader = document.querySelector(
    "#response-panel .raw-data-toggle-input .devtools-checkbox-toggle"
  );
  clickElement(payloadHeader, monitor);
  await onDomUpdated;

  ok(
    getCodeMirrorValue(monitor).match(/^<p>/),
    "The text shown in the source editor is incorrect."
  );

  info("Check that search input can be displayed");
  getCMEditor(monitor).focus();
  synthesizeKeyShortcut("CmdOrCtrl+F");
  const searchInput = await waitFor(() =>
    document.querySelector(".search-field input")
  );
  Assert.equal(
    searchInput.ownerDocument.activeElement,
    searchInput,
    "search input is focused"
  );

  await teardown(monitor);

  // This test uses a lot of memory, so force a GC to help fragmentation.
  info("Forcing GC after netmonitor test.");
  Cu.forceGC();
});
