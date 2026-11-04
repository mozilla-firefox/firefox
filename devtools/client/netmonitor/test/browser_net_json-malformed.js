/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Tests if malformed JSON responses are handled correctly.
 */

add_task(async function () {
  const {
    L10N,
  } = require("resource://devtools/client/netmonitor/src/utils/l10n.js");
  const { tab, monitor } = await initNetMonitor(JSON_MALFORMED_URL, {
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

  const requestItem = document.querySelector(".request-list-item");
  const requestsListStatus = requestItem.querySelector(".status-code");
  EventUtils.sendMouseEvent({ type: "mouseover" }, requestsListStatus);
  await waitUntil(() => requestsListStatus.title);
  await waitForDOM(requestItem, ".requests-list-timings-total");

  await verifyRequestItemTarget(
    document,
    getDisplayedRequests(store.getState()),
    getSortedRequests(store.getState())[0],
    "GET",
    CONTENT_TYPE_SJS + "?fmt=json-malformed",
    {
      status: 200,
      statusText: "OK",
      type: "json",
      fullMimeType: "text/json; charset=utf-8",
    }
  );

  const onDomUpdated = waitForDOM(document, "#response-panel .cm-content");
  store.dispatch(Actions.toggleNetworkDetails());
  clickOnSidebarTab(document, "response");
  await onDomUpdated;

  const tabpanel = document.querySelector("#response-panel");
  is(
    tabpanel.querySelector(".response-error-header") === null,
    false,
    "The response error header doesn't have the intended visibility."
  );
  is(
    tabpanel.querySelector(".response-error-header").textContent,
    "SyntaxError: JSON.parse: unexpected non-whitespace character after JSON data" +
      " at line 1 column 40 of the JSON data",
    "The response error header doesn't have the intended text content."
  );
  is(
    tabpanel.querySelector(".response-error-header").getAttribute("title"),
    "SyntaxError: JSON.parse: unexpected non-whitespace character after JSON data" +
      " at line 1 column 40 of the JSON data",
    "The response error header doesn't have the intended tooltiptext attribute."
  );
  const jsonView = tabpanel.querySelector(".tree-section .treeLabel") || {};
  is(
    jsonView.textContent === L10N.getStr("jsonScopeName"),
    false,
    "The response json view doesn't have the intended visibility."
  );
  is(
    tabpanel.querySelector(".cm-content") === null,
    false,
    "The response editor has the intended visibility."
  );
  is(
    tabpanel.querySelector(".response-image-box") === null,
    true,
    "The response image box doesn't have the intended visibility."
  );

  is(
    getCodeMirrorValue(monitor),
    '{ "greeting": "Hello malformed JSON!" },',
    "The text shown in the source editor is incorrect."
  );

  await teardown(monitor);
});
