/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Verifies that truncated response bodies still have the correct reported size.
 */
add_task(async function () {
  const limit = Services.prefs.getIntPref("devtools.netmonitor.bodyLimit");
  const URL = EXAMPLE_URL + "sjs_truncate-test-server.sjs?limit=" + limit;
  const { monitor } = await initNetMonitor(URL, { requestCount: 1 });

  info("Starting test... ");

  const {
    L10N,
  } = require("resource://devtools/client/netmonitor/src/utils/l10n.js");

  const { document, store, windowRequire } = monitor.panelWin;
  const Actions = windowRequire("devtools/client/netmonitor/src/actions/index");

  let onNetworkEvents = waitForNetworkEvents(monitor, 1);
  await reloadSelectedTab();
  await onNetworkEvents;

  // Response content will be updated asynchronously, we should make sure data is updated
  // on DOM before asserting.
  await waitUntil(() => document.querySelector(".request-list-item"));
  const item = document.querySelectorAll(".request-list-item")[0];
  await waitUntil(() => item.querySelector(".requests-list-type").title);

  const type = item.querySelector(".requests-list-type").textContent;
  const fullMimeType = item.querySelector(".requests-list-type").title;
  const transferred = item.querySelector(
    ".requests-list-transferred"
  ).textContent;
  const size = item.querySelector(".requests-list-size").textContent;

  is(type, "plain", "Type should be rendered correctly.");
  is(
    fullMimeType,
    "text/plain; charset=utf-8",
    "Mimetype should be rendered correctly."
  );
  is(
    transferred,
    L10N.getFormatStrWithNumbers("networkMenu.sizeMB", 2.1),
    "Transferred size should be rendered correctly."
  );
  is(
    size,
    L10N.getFormatStrWithNumbers("networkMenu.sizeMB", 2.1),
    "Size should be rendered correctly."
  );

  let onDomUpdated = waitForDOM(document, "#response-panel .cm-content");
  store.dispatch(Actions.toggleNetworkDetails());
  clickOnSidebarTab(document, "response");
  await onDomUpdated;

  let tabpanel = document.querySelector("#response-panel");
  ok(
    tabpanel.querySelector(".response-error-header"),
    "The response error header is shown because of the truncated body"
  );

  info("Reduce the body limit to have an emty body");
  await pushPref("devtools.netmonitor.bodyLimit", 1024);

  onNetworkEvents = waitForNetworkEvents(monitor, 1);
  await reloadSelectedTab();
  await onNetworkEvents;

  info("Wait for the truncated HTML page request");
  await waitUntil(() => document.querySelector(".request-list-item"));

  onDomUpdated = waitForDOM(document, "#response-panel .panel-container");
  store.dispatch(Actions.toggleNetworkDetails());
  clickOnSidebarTab(document, "response");
  await onDomUpdated;
  tabpanel = document.querySelector("#response-panel");
  ok(
    tabpanel.querySelector(".response-error-header"),
    "The response error header should still be shown, even with an empty body"
  );
  ok(
    tabpanel.querySelector(".empty-notice"),
    "The response body should be reported as empty"
  );

  return teardown(monitor);
});
