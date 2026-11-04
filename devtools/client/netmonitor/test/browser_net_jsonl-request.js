/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Tests if JSON Lines (JSONL/NDJSON) request payloads are displayed as a tree,
 * with one entry per non-blank line.
 */

add_task(async function () {
  const { tab, monitor } = await initNetMonitor(JSONL_POST_URL, {
    requestCount: 1,
  });

  const { document, store, windowRequire } = monitor.panelWin;
  const Actions = windowRequire("devtools/client/netmonitor/src/actions/index");
  store.dispatch(Actions.batchEnable(false));

  await performRequests(monitor, tab, 1);

  const onDomUpdated = waitForDOM(
    document,
    "#request-panel .properties-view",
    1
  );
  store.dispatch(Actions.toggleNetworkDetails());
  clickOnSidebarTab(document, "request");
  await onDomUpdated;

  const tabpanel = document.querySelector("#request-panel");

  is(
    tabpanel.querySelector(".data-label").textContent,
    "JSON Lines",
    "The request payload should be labelled as JSON Lines."
  );
  // The request panel auto-expands the tree, so only look at the top level.
  const entries = tabpanel.querySelectorAll(
    'tr .treeLabelCell .treeLabel[data-level="0"]'
  );
  is(entries.length, 3, "There should be one entry per line of the payload.");
  Assert.deepEqual(
    Array.from(entries, element => element.textContent),
    ["0", "1", "2"],
    "Entries should be labelled with their index, in document order."
  );

  await teardown(monitor);
});
