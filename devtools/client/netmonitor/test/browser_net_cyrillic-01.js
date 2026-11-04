/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Tests if cyrillic text is rendered correctly in the source editor.
 */

add_task(async function () {
  const { tab, monitor } = await initNetMonitor(CYRILLIC_URL, {
    requestCount: 1,
  });
  info("Starting test... ");

  const { document, store, windowRequire } = monitor.panelWin;
  const Actions = windowRequire("devtools/client/netmonitor/src/actions/index");
  const { getDisplayedRequests, getSortedRequests } = windowRequire(
    "devtools/client/netmonitor/src/selectors/index"
  );

  store.dispatch(Actions.batchEnable(false));

  // Execute requests.
  await performRequests(monitor, tab, 1);

  const requestItem = document.querySelectorAll(".request-list-item")[0];
  const requestsListStatus = requestItem.querySelector(".status-code");
  requestItem.scrollIntoView();
  EventUtils.sendMouseEvent({ type: "mouseover" }, requestsListStatus);
  await waitUntil(() => requestsListStatus.title);
  await waitForDOM(requestItem, ".requests-list-timings-total");

  await verifyRequestItemTarget(
    document,
    getDisplayedRequests(store.getState()),
    getSortedRequests(store.getState())[0],
    "GET",
    CONTENT_TYPE_SJS + "?fmt=txt",
    {
      status: 200,
      statusText: "DA DA DA",
    }
  );

  let onDomUpdated = waitForDOM(document, "#headers-panel");
  clickOnRequestRow(document.querySelectorAll(".request-list-item")[0]);
  await onDomUpdated;
  onDomUpdated = waitForDOM(document, "#response-panel .cm-content");
  clickOnSidebarTab(document, "response");
  await onDomUpdated;

  ok(
    getCodeMirrorValue(monitor).includes(
      "\u0411\u0440\u0430\u0442\u0430\u043d"
    ),
    "The text shown in the source editor is correct."
  );

  return teardown(monitor);
});
