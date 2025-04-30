/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at <http://mozilla.org/MPL/2.0/>. */

// Tests pretty-printing a source that is currently paused.

"use strict";

add_task(async function () {
  const dbg = await initDebugger("doc-minified.html", "math.min.js");

  await selectSource(dbg, "math.min.js");
  await addBreakpoint(dbg, "math.min.js", 3);

  invokeInTab("arithmetic");
  await waitForPaused(dbg, "math.min.js");
  await assertPausedAtSourceAndLine(dbg, findSource(dbg, "math.min.js").id, 3);

  clickElement(dbg, "prettyPrintButton");
  await waitForSelectedSource(dbg, "math.min.js:formatted");
  await waitForState(
    dbg,
    () => dbg.selectors.getSelectedFrame().location.line == 18
  );
  await assertPausedAtSourceAndLine(
    dbg,
    findSource(dbg, "math.min.js:formatted").id,
    18
  );
  await waitForBreakpoint(dbg, "math.min.js:formatted", 18);
  await assertBreakpoint(dbg, 18);

  await resume(dbg);
});
