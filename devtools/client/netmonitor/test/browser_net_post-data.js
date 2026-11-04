/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Tests if the POST requests display the correct information in the UI.
 */
add_task(async function () {
  const {
    L10N,
  } = require("resource://devtools/client/netmonitor/src/utils/l10n.js");

  // Set a higher panel height in order to get full CodeMirror content
  Services.prefs.setIntPref("devtools.toolbox.footer.height", 600);

  const { tab, monitor } = await initNetMonitor(POST_DATA_URL, {
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
  await performRequests(monitor, tab, 2);

  const requestItems = document.querySelectorAll(".request-list-item");
  for (const requestItem of requestItems) {
    requestItem.scrollIntoView();
    const requestsListStatus = requestItem.querySelector(".status-code");
    EventUtils.sendMouseEvent({ type: "mouseover" }, requestsListStatus);
    await waitUntil(() => requestsListStatus.title);
    await waitForDOM(requestItem, ".requests-list-timings-total");
  }

  await verifyRequestItemTarget(
    document,
    getDisplayedRequests(store.getState()),
    getSortedRequests(store.getState())[0],
    "POST",
    SIMPLE_SJS +
      "?foo=bar&baz=42&valueWithEqualSign=hijk=123=mnop&type=urlencoded",
    {
      status: 200,
      statusText: "Och Aye",
      type: "plain",
      fullMimeType: "text/plain; charset=utf-8",
      size: L10N.getFormatStrWithNumbers("networkMenu.sizeB", 12),
      time: true,
    }
  );
  await verifyRequestItemTarget(
    document,
    getDisplayedRequests(store.getState()),
    getSortedRequests(store.getState())[1],
    "POST",
    SIMPLE_SJS + "?foo=bar&baz=42&type=multipart",
    {
      status: 200,
      statusText: "Och Aye",
      type: "plain",
      fullMimeType: "text/plain; charset=utf-8",
      size: L10N.getFormatStrWithNumbers("networkMenu.sizeB", 12),
      time: true,
    }
  );

  // Wait for raw data toggle to be displayed
  const onDomUpdated = waitForDOM(
    document,
    "#request-panel .raw-data-toggle-input .devtools-checkbox-toggle"
  );
  clickOnRequestRow(document.querySelectorAll(".request-list-item")[0]);
  clickOnSidebarTab(document, "request");
  await onDomUpdated;
  await testParamsTab("urlencoded");

  // Wait for header and CodeMirror editor to be displayed
  const waitForHeader = waitForDOM(document, "#request-panel .data-header");
  const waitForSourceEditor = waitForDOM(
    document,
    "#request-panel .cm-content"
  );
  clickOnRequestRow(document.querySelectorAll(".request-list-item")[1]);
  await Promise.all([waitForHeader, waitForSourceEditor]);
  await testParamsTab("multipart");

  return teardown(monitor);

  async function testParamsTab(type) {
    const tabpanel = document.querySelector("#request-panel");

    function checkVisibility(box) {
      is(
        tabpanel.querySelector(".cm-content") === null,
        !box.includes("editor"),
        "The request post data doesn't have the intended visibility."
      );
    }

    is(
      tabpanel.querySelectorAll(".raw-data-toggle").length,
      type == "urlencoded" ? 1 : 0,
      "The display of the raw request data toggle must be correct."
    );
    is(
      tabpanel.querySelectorAll(".empty-notice").length,
      0,
      "The empty notice should not be displayed in this tabpanel."
    );

    is(
      tabpanel.querySelector(".data-label").textContent,
      L10N.getStr(
        type == "urlencoded" ? "paramsFormData" : "paramsPostPayload"
      ),
      "The post section doesn't have the correct title."
    );

    const labels = tabpanel.querySelectorAll("tr .treeLabelCell .treeLabel");
    const values = tabpanel.querySelectorAll("tr .treeValueCell .objectBox");

    if (type == "urlencoded") {
      checkVisibility("request");
      is(
        labels.length,
        4,
        "There should be 4 param values displayed in this tabpanel."
      );
      is(
        labels[0].textContent,
        "foo",
        "The first post param name was incorrect."
      );
      is(
        values[0].textContent,
        `"bar"`,
        "The first post param value was incorrect."
      );
      is(
        labels[1].textContent,
        "baz",
        "The second post param name was incorrect."
      );
      is(
        values[1].textContent,
        `"123"`,
        "The second post param value was incorrect."
      );
      is(
        labels[2].textContent,
        "valueWithEqualSign",
        "The third post param name was incorrect."
      );
      is(
        values[2].textContent,
        `"xyz=abc=123"`,
        "The third post param value was incorrect."
      );
      is(
        labels[3].textContent,
        "valueWithAmpersand",
        "The fourth post param name was incorrect."
      );
      is(
        values[3].textContent,
        `"abcd&1234"`,
        "The fourth post param value was incorrect."
      );
    } else {
      checkVisibility("request editor");

      const text = getCodeMirrorValue(monitor);

      ok(
        text.includes('Content-Disposition: form-data; name="text"'),
        "The text shown in the source editor is incorrect (1.1)."
      );
      ok(
        text.includes('Content-Disposition: form-data; name="email"'),
        "The text shown in the source editor is incorrect (2.1)."
      );
      ok(
        text.includes('Content-Disposition: form-data; name="range"'),
        "The text shown in the source editor is incorrect (3.1)."
      );
      ok(
        text.includes('Content-Disposition: form-data; name="Custom field"'),
        "The text shown in the source editor is incorrect (4.1)."
      );
      ok(
        text.includes("Some text..."),
        "The text shown in the source editor is incorrect (2.2)."
      );
      ok(
        text.includes("42"),
        "The text shown in the source editor is incorrect (3.2)."
      );
      ok(
        text.includes("Extra data"),
        "The text shown in the source editor is incorrect (4.2)."
      );
    }
  }
});
