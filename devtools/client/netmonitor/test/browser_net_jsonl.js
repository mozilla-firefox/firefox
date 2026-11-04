/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Tests if JSON Lines (JSONL/NDJSON) responses are displayed as a tree, with
 * one entry per non-blank line.
 */

const JSONL_LABEL = "JSON Lines";
const DEFAULT_RAW_RESPONSE_PREF = "devtools.netmonitor.ui.default-raw-response";

add_task(async function testJsonlContentType() {
  const { monitor, tabpanel } = await openResponsePanel("?fmt=jsonl");

  is(
    tabpanel.querySelector(".response-error-header"),
    null,
    "A valid JSON Lines document should not report a parsing error."
  );
  is(
    tabpanel.querySelector(".data-label").textContent,
    JSONL_LABEL,
    "The response payload should be labelled as JSON Lines."
  );
  is(
    tabpanel.querySelectorAll(".treeRow").length,
    3,
    "There should be one entry per line of the document."
  );
  Assert.deepEqual(
    getTreeLabels(tabpanel),
    ["0", "1", "2"],
    "Entries should be labelled with their index, in document order."
  );
  is(
    tabpanel.querySelectorAll("tr .treeValueCell .objectBox")[0].textContent,
    '{ greeting: "Hello JSON Lines!" }',
    "The first entry should show its record."
  );

  info("Toggling the raw response payload.");
  const onDomUpdated = waitForDOM(
    monitor.panelWin.document,
    "#response-panel .cm-content"
  );
  clickElement(
    tabpanel.querySelector(".raw-data-toggle-input .devtools-checkbox-toggle"),
    monitor
  );
  await onDomUpdated;

  ok(
    tabpanel.querySelector(".cm-content"),
    "The raw response payload should be displayed in the editor."
  );

  await teardown(monitor);
});

add_task(async function testNdjsonContentType() {
  const { monitor, tabpanel } = await openResponsePanel("?fmt=jsonl-ndjson");

  is(
    tabpanel.querySelector(".data-label").textContent,
    JSONL_LABEL,
    "application/x-ndjson should be recognized as JSON Lines."
  );
  is(
    tabpanel.querySelectorAll(".treeRow").length,
    2,
    "There should be one entry per line of the document."
  );

  await teardown(monitor);
});

add_task(async function testMalformedLine() {
  const { monitor, tabpanel } = await openResponsePanel(
    "?fmt=jsonl-malformed-line"
  );

  is(
    tabpanel.querySelector(".response-error-header"),
    null,
    "A line which isn't valid JSON should not fail the whole document."
  );
  is(
    tabpanel.querySelectorAll(".treeRow").length,
    3,
    "Blank lines should be skipped."
  );
  Assert.deepEqual(
    getTreeLabels(tabpanel),
    ["0", "1", "2"],
    "Entry indices should stay contiguous across the skipped blank line."
  );

  const lineErrors = tabpanel.querySelectorAll(".jsonl-line-error");
  is(
    lineErrors.length,
    1,
    "Only the invalid line should be shown as an error."
  );
  ok(
    lineErrors[0].textContent.includes("not json"),
    "The error should show the line's original text."
  );
  is(
    lineErrors[0].closest(".treeRow").querySelector(".treeLabel").textContent,
    "1",
    "The invalid line should keep its place in the document."
  );
  ok(
    !lineErrors[0].closest(".treeRow").classList.contains("hasChildren"),
    "An invalid line should be a leaf, with no expand toggle."
  );

  await teardown(monitor);
});

add_task(async function testJsonlFileExtension() {
  const { monitor, tabpanel } = await openResponsePanel(
    "?file=jsonl_text_plain.jsonl"
  );

  is(
    tabpanel.querySelector(".data-label").textContent,
    JSONL_LABEL,
    "A .jsonl file should be recognized even when served as text/plain."
  );
  is(
    tabpanel.querySelectorAll(".treeRow").length,
    2,
    "There should be one entry per line of the document."
  );

  await teardown(monitor);
});

/**
 * Opens the netmonitor on the JSON Lines test page, selects the only request
 * and switches to its Response panel.
 *
 * @param {string} query
 *        Query string appended to the test page url, forwarded to the server.
 * @returns {object} shape:
 *  {object} monitor: the netmonitor panel
 *  {Element} tabpanel: the response panel element
 */
async function openResponsePanel(query) {
  // Toggling the raw response payload persists in a preference, so make sure
  // each task starts from the formatted view.
  await pushPref(DEFAULT_RAW_RESPONSE_PREF, false);

  const { tab, monitor } = await initNetMonitor(JSONL_URL + query, {
    requestCount: 1,
  });

  const { document, store, windowRequire } = monitor.panelWin;
  const Actions = windowRequire("devtools/client/netmonitor/src/actions/index");
  store.dispatch(Actions.batchEnable(false));

  await performRequests(monitor, tab, 1);

  const onDomUpdated = waitForDOM(
    document,
    "#response-panel .properties-view",
    1
  );
  store.dispatch(Actions.toggleNetworkDetails());
  clickOnSidebarTab(document, "response");
  await onDomUpdated;

  return { monitor, tabpanel: document.querySelector("#response-panel") };
}

function getTreeLabels(tabpanel) {
  return Array.from(
    tabpanel.querySelectorAll("tr .treeLabelCell .treeLabel"),
    element => element.textContent
  );
}
