/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Tests if JSON responses and requests with XSSI protection sequences
 * are handled correctly.
 */

add_task(async function () {
  const { tab, monitor } = await initNetMonitor(JSON_XSSI_PROTECTION_URL, {
    requestCount: 1,
  });
  info("Starting test... ");

  const { document, store, windowRequire } = monitor.panelWin;
  const Actions = windowRequire("devtools/client/netmonitor/src/actions/index");

  store.dispatch(Actions.batchEnable(false));

  // Execute requests.
  await performRequests(monitor, tab, 1);

  let onDomUpdated = Promise.all([
    waitForDOM(document, "#response-panel .data-header"),
    waitForDOM(document, "#response-panel .cm-content"),
    waitForDOM(document, "#response-panel #raw-response-checkbox:checked"),
  ]);

  store.dispatch(Actions.toggleNetworkDetails());
  info("Opening response panel");
  clickOnSidebarTab(document, "response");

  await onDomUpdated;

  info(
    "making sure response panel defaults to raw view and correctly displays payload"
  );
  const codeLines = document.querySelector("#response-panel .cm-content");
  const firstLine = codeLines.firstChild;
  is(
    firstLine.textContent,
    ")]}'",
    "XSSI protection sequence should be visibly in raw view"
  );

  info("making sure XSSI notification box is not present in raw view");
  let notification = document.querySelector(
    '.network-monitor #response-panel .notification[data-key="xssi-string-removed-info-box"]'
  );
  ok(!notification, "notification should not be present in raw view");

  info("switching to props view");
  onDomUpdated = waitForDOM(document, "#response-panel .properties-view");
  const tabpanel = document.querySelector("#response-panel");
  clickElement(tabpanel.querySelector("#raw-response-checkbox"), monitor);
  await onDomUpdated;

  is(
    tabpanel.querySelectorAll(".treeRow").length,
    1,
    "There should be 1 json property displayed in the response."
  );

  const labels = tabpanel.querySelectorAll("tr .treeLabelCell .treeLabel");
  const values = tabpanel.querySelectorAll("tr .treeValueCell .objectBox");
  info("Checking content of displayed json response");
  is(labels[0].textContent, "greeting", "The first key should be correct");
  is(
    values[0].textContent,
    `"Hello good XSSI protection"`,
    "The first property should be correct"
  );

  info("making sure notification box is present and correct in props view");

  notification = document.querySelector(
    '.network-monitor #response-panel .notification[data-key="xssi-string-removed-info-box"] .notificationInner .messageText'
  );

  is(
    notification.textContent,
    "The string “)]}'\n” was removed from the beginning of the JSON shown below",
    "The notification message is correct"
  );

  await teardown(monitor);
});
