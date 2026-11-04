/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Test that when the multiprocess browser toolbox is used, console messages
// from newly opened content processes appear.

"use strict";

requestLongerTimeout(4);

const TEST_URI = `data:text/html,<!DOCTYPE html><meta charset=utf8>console API calls<script>
  console.log("Data Message");
</script>`;

const EXAMPLE_URI =
  "http://example.com/browser/devtools/client/webconsole/" +
  "test/browser/test-console.html";

/* global gToolbox */
Services.scriptloader.loadSubScript(
  "chrome://mochitests/content/browser/devtools/client/framework/browser-toolbox/test/helpers-browser-toolbox.js",
  this
);

add_task(async function () {
  // Needed for the invokeInTab() function below
  await pushPref("security.allow_parent_unrestricted_js_loads", true);
  await pushPref("devtools.browsertoolbox.scope", "everything");

  await addTab(TEST_URI);
  const ToolboxTask = await initBrowserToolboxTask();
  await ToolboxTask.importFunctions({
    findMessagesVirtualized,
    findMessageVirtualizedByType,
    findMessagesVirtualizedByType,
    gInFindMessagesVirtualized: false,
    gFindMessagesVirtualizedStack: null,
    waitUntil,
  });

  // Make sure the data: URL message appears in the OBT.
  await ToolboxTask.spawn(null, async () => {
    await gToolbox.selectTool("webconsole");
    const hud = gToolbox.getCurrentPanel().hud;
    await waitUntil(() =>
      findMessageVirtualizedByType({
        hud,
        text: "Data Message",
        typeSelector: ".console-api",
      })
    );
  });
  ok(true, "First message appeared in toolbox");

  await addTab(EXAMPLE_URI);
  invokeInTab("stringLog");

  // Make sure the example.com message appears in the OBT.
  await ToolboxTask.spawn(null, async () => {
    const hud = gToolbox.getCurrentPanel().hud;
    await waitUntil(() =>
      findMessageVirtualizedByType({
        hud,
        text: "stringLog",
        typeSelector: ".console-api",
      })
    );
  });
  ok(true, "New message appeared in toolbox");

  await ToolboxTask.destroy();
});
