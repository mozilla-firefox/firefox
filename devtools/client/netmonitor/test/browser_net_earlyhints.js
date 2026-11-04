/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Tests that early hint status appears to indicate the presence
 * of early hint notifications by the request.
 */

add_task(async function testEarlyHintStatusCodeAndHeaders() {
  const { tab, monitor } = await initNetMonitor(STATUS_CODES_URL, {
    requestCount: 1,
  });

  const { document, store, windowRequire } = monitor.panelWin;
  const Actions = windowRequire("devtools/client/netmonitor/src/actions/index");
  store.dispatch(Actions.batchEnable(false));

  await performEarlyHintRequest(monitor, tab);
  const EARLYHINT_REQUEST_URL = `sjs_early-hint-test-server.sjs?early-hint-pixel.sjs=5ecccd01-dd3f-4bbd-bd3e-0491d7dd78a1`;

  const earlyRequestItem = document.querySelector(".request-list-item");

  const statusCodes = earlyRequestItem.querySelectorAll(
    ".requests-list-status .status-code"
  );
  is(
    statusCodes.length,
    2,
    "There are two status codes displayed in the status column"
  );

  is(statusCodes[0].innerText, "103", "The first status is 103 early hint");
  is(statusCodes[1].innerText, "200", "The second status is the 200 OK");

  is(
    earlyRequestItem.querySelector(".requests-list-file").innerText,
    EARLYHINT_REQUEST_URL,
    "The url in the displayed request is correct"
  );

  clickOnRequestRow(earlyRequestItem);

  // Wait till all the summary section is loaded
  await waitUntil(() =>
    document.querySelector("#headers-panel .tabpanel-summary-value")
  );
  const panel = document.querySelector("#headers-panel");
  const earlyHintsStatusCode = panel.querySelector(
    ".headers-earlyhint-status .status-code"
  );

  EventUtils.sendMouseEvent({ type: "mouseover" }, earlyHintsStatusCode);

  is(
    parseInt(earlyHintsStatusCode.dataset.code, 10),
    103,
    "The status summary code is correct."
  );

  info("Wait for all the Headers sections to render");
  await waitUntil(
    () =>
      document.querySelectorAll("#headers-panel .panel-container .accordion li")
        .length == 3
  );

  info("Check that the early hint response headers are visible");
  const firstHeaderPanel = document.querySelector(
    "#headers-panel .panel-container .accordion li"
  );

  is(
    firstHeaderPanel.querySelector(".accordion-header-label").innerText,
    "Early Hints Response Headers (117 B)",
    "The early hints response headers are visible and is the first panel displayed from the top"
  );

  const expectedHeaders = [
    {
      label: "Link",
      value:
        " <early-hint-pixel.sjs?5ecccd01-dd3f-4bbd-bd3e-0491d7dd78a1>; rel=preload; as=image",
    },
  ];

  const labels = firstHeaderPanel.querySelectorAll(
    ".accordion-content tr .treeLabelCell .treeLabel"
  );
  const values = firstHeaderPanel.querySelectorAll(
    ".accordion-content tr .treeValueCell .objectBox"
  );

  for (let i = 0; i < labels.length; i++) {
    is(
      labels[i].textContent,
      expectedHeaders[i].label,
      "The early hint header name was incorrect."
    );
    is(
      values[i].textContent,
      expectedHeaders[i].value,
      "The early hint header value was incorrect."
    );
  }

  await teardown(monitor);
});

add_task(async function testEarlyHintFilter() {
  Services.prefs.setBoolPref("devtools.netmonitor.persistlog", true);
  const { tab, monitor } = await initNetMonitor(STATUS_CODES_URL, {
    requestCount: 1,
  });

  const { document, store, windowRequire } = monitor.panelWin;
  const Actions = windowRequire("devtools/client/netmonitor/src/actions/index");
  store.dispatch(Actions.batchEnable(false));

  const waitForEvents = waitForNetworkEvents(monitor, 1);
  tab.linkedBrowser.reload();
  await waitForEvents;

  await performEarlyHintRequest(monitor, tab);
  document.querySelector(".devtools-filterinput").focus();

  EventUtils.sendString("103");
  ok(
    document.querySelector(".devtools-autocomplete-popup"),
    "Autocomplete Popup Created"
  );

  testAutocompleteContents(["status-code:103"], document);
  EventUtils.synthesizeKey("KEY_Enter");
  is(
    document.querySelector(".devtools-filterinput").value,
    "status-code:103",
    "Value correctly set after Enter"
  );

  await waitUntil(
    () => document.querySelectorAll(".request-list-item").length == 1
  );

  const statusCode = document.querySelector(
    ".request-list-item .requests-list-status .status-code"
  );
  is(statusCode.innerText, "103", "The first status is 103 early hint");

  await teardown(monitor);
  Services.prefs.setBoolPref("devtools.netmonitor.persistlog", false);
});

async function performEarlyHintRequest(monitor, tab) {
  const onNetworkEvents = waitForNetworkEvents(monitor, 1);
  await SpecialPowers.spawn(tab.linkedBrowser, [], async function () {
    content.wrappedJSObject.performEarlyHintRequest();
  });
  await onNetworkEvents;
}
