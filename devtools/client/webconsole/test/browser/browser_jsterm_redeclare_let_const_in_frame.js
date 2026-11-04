/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Check that `let`/`const` declarations in the console while paused in a
// debugger frame shadow the outer bindings rather than mutating them.

const TEST_URI = `data:text/html;charset=utf-8,<!DOCTYPE html>
<script>
let outerX = 2;
const outerY = 2;
function readOuterX() { return outerX; }
function readOuterY() { return outerY; }
function inner() { debugger; }
</script>`;

add_task(async function () {
  const hud = await openNewTabAndConsole(TEST_URI);
  const toolbox = gDevTools.getToolboxForTab(gBrowser.selectedTab);

  info("Open the debugger and pause inside inner()");
  await openDebugger();
  const dbg = createDebuggerContext(toolbox);
  const onPaused = waitForPaused(dbg);

  SpecialPowers.spawn(gBrowser.selectedBrowser, [], () => {
    content.wrappedJSObject.inner();
  });
  await onPaused;

  info("Back to the console, still paused in inner()");
  await toolbox.selectTool("webconsole");

  // Wait for the console to reflect the paused frame before evaluating.
  await waitFor(() => hud.getSelectedFrameActorID() != null);

  info(
    "A `let` declaration in the frame shadows the outer binding, so " +
      "readOuterX() still sees the outer value"
  );
  await executeAndWaitForResultMessage(hud, "let outerX = 3; outerX;", "3");
  await executeAndWaitForResultMessage(hud, "readOuterX();", "2");

  info(
    "A `const` declaration in the frame shadows the outer binding too, " +
      "so readOuterY() still sees the outer value"
  );
  await executeAndWaitForResultMessage(hud, "const outerY = 4; outerY;", "4");
  await executeAndWaitForResultMessage(hud, "readOuterY();", "2");

  info("The outer binding is unchanged after resuming");
  await resume(dbg);
  await executeAndWaitForResultMessage(hud, "readOuterX();", "2");
  await executeAndWaitForResultMessage(hud, "readOuterY();", "2");
});
