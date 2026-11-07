/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/* import-globals-from head_smartwindow_first_turn_telemetry.js */
Services.scriptloader.loadSubScript(
  getRootDirectory(gTestPath) + "head_smartwindow_first_turn_telemetry.js",
  this
);

add_task(async function test_first_turn_completion() {
  await testTurnCompletion("home");
});
