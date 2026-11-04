/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Tests if the POST requests display the correct information in the UI,
 * for JSON payloads.
 */

add_task(async function () {
  const {
    L10N,
  } = require("resource://devtools/client/netmonitor/src/utils/l10n.js");

  const { tab, monitor } = await initNetMonitor(POST_JSON_URL, {
    requestCount: 1,
  });
  info("Starting test... ");

  const { document, store, windowRequire } = monitor.panelWin;
  const Actions = windowRequire("devtools/client/netmonitor/src/actions/index");

  store.dispatch(Actions.batchEnable(false));

  // Execute requests.
  await performRequests(monitor, tab, 1);

  // Wait for header and properties view to be displayed
  let onDomUpdated = Promise.all([
    waitForDOM(document, "#request-panel .data-header"),
    waitForDOM(document, "#request-panel .properties-view"),
  ]);
  store.dispatch(Actions.toggleNetworkDetails());
  clickOnSidebarTab(document, "request");
  await onDomUpdated;

  const tabpanel = document.querySelector("#request-panel");

  ok(
    tabpanel.querySelector(".treeTable"),
    "The request params doesn't have the indended visibility."
  );
  is(
    tabpanel.querySelector(".cm-content") === null,
    true,
    "The request post data doesn't have the indended visibility."
  );
  is(
    tabpanel.querySelectorAll(".raw-data-toggle") !== null,
    true,
    "The raw request data toggle should be displayed in this tabpanel."
  );
  is(
    tabpanel.querySelectorAll(".empty-notice").length,
    0,
    "The empty notice should not be displayed in this tabpanel."
  );

  is(
    tabpanel.querySelector(".data-label").textContent,
    L10N.getStr("jsonScopeName"),
    "The post section doesn't have the correct title."
  );

  const labels = tabpanel.querySelectorAll("tr .treeLabelCell .treeLabel");
  const values = tabpanel.querySelectorAll("tr .treeValueCell .objectBox");

  is(labels[0].textContent, "a", "The JSON var name was incorrect.");
  is(values[0].textContent, "1", "The JSON var value was incorrect.");

  // Toggle the raw data display. This should hide the formatted display.
  onDomUpdated = waitForDOM(document, "#request-panel .cm-content");
  const rawDataToggle = document.querySelector(
    "#request-panel .raw-data-toggle-input .devtools-checkbox-toggle"
  );
  clickElement(rawDataToggle, monitor);
  await onDomUpdated;

  is(
    tabpanel.querySelector(".data-label").textContent,
    L10N.getStr("paramsPostPayload"),
    "The post section doesn't have the correct title."
  );
  is(
    tabpanel.querySelector(".raw-data-toggle-input .devtools-checkbox-toggle")
      .checked,
    true,
    "The raw request toggle should be on."
  );
  is(
    tabpanel.querySelector(".properties-view") === null,
    true,
    "The formatted display should be hidden."
  );
  // Bug 1514750 - Show JSON request in plain text view also
  ok(
    tabpanel.querySelector(".cm-content"),
    "The request post data doesn't have the indended visibility."
  );
  ok(
    getCodeMirrorValue(monitor).includes('{"a":1}'),
    "The text shown in the source editor is incorrect."
  );

  return teardown(monitor);
});
