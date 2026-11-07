/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Test the 'Open in debugger' feature
 */

add_task(async function () {
  const { tab, monitor, toolbox } = await initNetMonitor(
    CONTENT_TYPE_WITHOUT_CACHE_URL,
    { requestCount: 1 }
  );
  info("Starting test... ");

  const { document, store, windowRequire } = monitor.panelWin;
  // Avoid async processing
  const Actions = windowRequire("devtools/client/netmonitor/src/actions/index");
  store.dispatch(Actions.batchEnable(false));

  // Execute requests.
  await performRequests(monitor, tab, CONTENT_TYPE_WITHOUT_CACHE_REQUESTS);

  clickOnRequestRow(document.querySelectorAll(".request-list-item")[1]);
  EventUtils.sendMouseEvent(
    { type: "contextmenu" },
    document.querySelectorAll(".request-list-item")[1]
  );

  const onStyleEditorReady = toolbox.once("styleeditor-ready");
  await selectContextMenuItem(
    monitor,
    "request-list-context-open-in-style-editor"
  );
  await onStyleEditorReady;

  ok(true, "Style Editor has been open");

  await teardown(monitor);
});
