/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Tests if the POST requests display the correct information in the UI,
 * for raw payloads with attached content-type headers.
 */
add_task(async function () {
  const {
    L10N,
  } = require("resource://devtools/client/netmonitor/src/utils/l10n.js");

  const { tab, monitor } = await initNetMonitor(POST_RAW_URL, {
    requestCount: 1,
  });
  info("Starting test... ");

  const { document, store, windowRequire } = monitor.panelWin;
  const Actions = windowRequire("devtools/client/netmonitor/src/actions/index");

  store.dispatch(Actions.batchEnable(false));

  // Execute requests.
  await performRequests(monitor, tab, 1);

  // Wait for raw data toggle to be displayed
  const onDomUpdated = waitForDOM(
    document,
    "#request-panel .raw-data-toggle-input .devtools-checkbox-toggle"
  );
  clickOnRequestRow(document.querySelectorAll(".request-list-item")[0]);
  clickOnSidebarTab(document, "request");
  await onDomUpdated;

  const tabpanel = document.querySelector("#request-panel");

  ok(
    tabpanel.querySelector(".treeTable"),
    "The request params doesn't have the intended visibility."
  );
  Assert.strictEqual(
    tabpanel.querySelector(".editor-mount"),
    null,
    "The request post data doesn't have the indented visibility."
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
    L10N.getStr("paramsFormData"),
    "The post section doesn't have the correct title."
  );

  const labels = tabpanel.querySelectorAll("tr .treeLabelCell .treeLabel");
  const values = tabpanel.querySelectorAll("tr .treeValueCell .objectBox");

  is(labels[0].textContent, "foo", "The first query param name was incorrect.");
  is(
    values[0].textContent,
    `"bar"`,
    "The first query param value was incorrect."
  );
  is(
    labels[1].textContent,
    "baz",
    "The second query param name was incorrect."
  );
  is(
    values[1].textContent,
    `"123"`,
    "The second query param value was incorrect."
  );

  return teardown(monitor);
});
