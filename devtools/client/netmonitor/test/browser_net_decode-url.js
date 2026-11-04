/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Tests if "Request URL" containing "#" in its query is correctly decoded.
 */
add_task(async function () {
  const { tab, monitor } = await initNetMonitor(POST_RAW_URL_WITH_HASH, {
    requestCount: 1,
  });
  info("Starting test... ");

  const { document, store, windowRequire } = monitor.panelWin;
  const Actions = windowRequire("devtools/client/netmonitor/src/actions/index");
  store.dispatch(Actions.batchEnable(false));

  // Execute request.
  await performRequests(monitor, tab, 1);

  // Wait until the url preview ihas loaded
  const onUrlPreview = waitUntil(() =>
    document.querySelector("#headers-panel .url-preview")
  );

  clickOnRequestRow(document.querySelectorAll(".request-list-item")[0]);

  await onUrlPreview;

  const requestURL = document.querySelector("#headers-panel .url-preview .url");
  is(
    requestURL.textContent.endsWith("foo # bar"),
    true,
    "\"Request URL\" containing '#' is correctly decoded."
  );

  return teardown(monitor);
});
