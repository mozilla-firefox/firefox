/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at <http://mozilla.org/MPL/2.0/>. */

// Tests that the editor sets the correct mode for different file
// types

"use strict";

add_task(async function () {
  const dbg = await initDebugger("doc-scripts.html", "simple1.js");

  info("Selecting a javascript source should set the correct mode");
  await selectSource(dbg, "simple1.js");
  is(getEditorFileMode(dbg), "javascript", "Mode is correct");

  info("Selecting a html source should set the correct mode");
  await selectSource(dbg, "doc-scripts.html");
  is(getEditorFileMode(dbg), "html", "Mode is correct");

  info("Switch back to a javascript source should set the correct mode");
  await selectSource(dbg, "simple1.js");
  is(getEditorFileMode(dbg), "javascript", "Mode is correct");
});
