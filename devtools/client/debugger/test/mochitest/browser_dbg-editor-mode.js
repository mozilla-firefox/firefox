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

add_task(async function testVueSingleFileComponent() {
  const dbg = await initDebugger(
    "doc-sourcemaps-vue.html",
    "App.vue",
    "Virtual.vue?12ab"
  );

  await selectSource(dbg, "App.vue");
  is(
    getEditorFileMode(dbg),
    "vue",
    "Vue single-file components are highlighted with the Vue mode"
  );

  await selectSource(dbg, "Virtual.vue?12ab");
  is(
    getEditorFileMode(dbg),
    "javascript",
    "Generated JavaScript modules with a .vue extension use the JavaScript mode"
  );

  await selectSource(dbg, "App.vue");
  await selectSource(dbg, "vue-sfc.js");
  is(
    getEditorFileMode(dbg),
    "javascript",
    "Mode is reset to JavaScript when switching from a Vue source"
  );
});
