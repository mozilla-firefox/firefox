/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at <http://mozilla.org/MPL/2.0/>. */

"use strict";

// Tests that `debugger` statements are hit before the debugger even
// initializes and it properly highlights the right location in the
// debugger.

add_task(async function () {
  const url = `${EXAMPLE_URL}doc-script-switching.html`;
  const toolbox = await openNewTabAndToolbox(url, "webconsole");

  // Type "debugger" into console
  const wrapper = toolbox.getPanel("webconsole").hud.ui.wrapper;
  const onSelected = toolbox.once("jsdebugger-selected");
  wrapper.dispatchEvaluateExpression("debugger");

  // Wait for the debugger to be selected and make sure it's paused
  await onSelected;
  is(toolbox.threadFront.state, "paused");

  // Create a dbg context
  const dbg = createDebuggerContext(toolbox);

  // Make sure the thread is paused in the right source and location
  await waitForPaused(dbg);
  const selectedSource = dbg.selectors.getSelectedSource();
  ok(
    !selectedSource.url,
    "The selected source is the console evaluation and doesn't have a URL"
  );
  is(getEditorContent(dbg), "debugger");
  await assertPausedAtSourceAndLine(dbg, selectedSource.id, 1);

  await resume(dbg);
});

// Test that you can also hit breakpoints from another target (worker)
// without opening the debugger first
add_task(async function () {
  // Enable Worker targets listening from TargetCommand
  await pushPref("dom.worker.console.dispatch_events_to_main_thread", false);

  const workerUrl = "data:application/javascript,debugger";
  const url = `data:text/html,<script>new Worker("${workerUrl}");</script>`;
  const toolbox = await openNewTabAndToolbox(url, "webconsole");

  info("Reload to hit the worker breakpoint");
  const onSelected = toolbox.once("jsdebugger-selected");
  await reloadSelectedTab();
  // Wait for the debugger to be selected and make sure it's paused
  await onSelected;

  // Create a dbg context
  const dbg = createDebuggerContext(toolbox);

  // Make sure the thread is paused in the right source and location
  await waitForPaused(dbg);
  const selectedSource = dbg.selectors.getSelectedSource();
  is(selectedSource.url, workerUrl, "The selected source is the worker URL");
  is(getEditorContent(dbg), "debugger");
  await assertPausedAtSourceAndLine(dbg, selectedSource.id, 1);

  await resume(dbg);
});
