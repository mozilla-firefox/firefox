/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Tests that the evaluation warning notification shows in the console when the debugger
// is paused in an non-pretty printed original file with original variable mapping disabled.

"use strict";

const l10n = require("resource://devtools/shared/webconsole/l10n.js");
const TEST_URI =
  "https://example.com/browser/devtools/client/webconsole/test/browser/test-autocomplete-mapped.html";

add_task(async function () {
  const hud = await openNewTabAndConsole(TEST_URI);
  const toolbox = gDevTools.getToolboxForTab(gBrowser.selectedTab);

  info("Opening Debugger with original variable mapping disabled");
  await openDebugger();
  const dbg = createDebuggerContext(toolbox);

  info("Waiting for pause");
  // This calls firstCall() on the content page and waits for pause(without waiting for loaded scopes). (firstCall
  // has a debugger statement)
  await pauseDebugger(dbg, { shouldWaitForLoadScopes: false });

  info("Assert the warning notification in the split console");
  await toolbox.openSplitConsole();

  // Wait for the notification to be displayed in the console
  await waitUntil(() => findEvaluationNotificationMessage(hud));

  const notificationMessage = l10n.getStr(
    "webconsole-evaluation-notification-no-original-variable-mapping"
  );

  is(
    findEvaluationNotificationMessage(hud),
    notificationMessage,
    "The Original variable mapping warning is displayed"
  );

  info("Assert the warning notification in the full console panel");
  await toolbox.selectTool("webconsole");
  is(
    findEvaluationNotificationMessage(hud),
    notificationMessage,
    "The Original variable mapping warning is displayed"
  );

  await toolbox.selectTool("jsdebugger");

  const loadedScopes = waitForLoadedScopes(dbg);
  dbg.actions.toggleMapScopes();
  await loadedScopes;

  await toolbox.selectTool("webconsole");

  ok(
    !findEvaluationNotificationMessage(hud),
    "The Original variable mapping warning is no longer displayed"
  );

  await resume(dbg);
});

function findEvaluationNotificationMessage(hud) {
  return hud.ui.outputNode.querySelector(".evaluation-notification__text")
    ?.innerText;
}
