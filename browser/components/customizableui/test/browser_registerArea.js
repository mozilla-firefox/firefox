/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Tests that a toolbar area can be registered with overflowable: false
 * as one of its properties, and this results in a non-overflowable
 * toolbar.
 */
add_task(async function test_overflowable_false() {
  registerCleanupFunction(removeCustomToolbars);

  const kToolbarId = "no-overflow-toolbar";
  let node = createToolbarWithPlacements(kToolbarId, ["spring"], {
    overflowable: false,
  });

  Assert.ok(
    !node.hasAttribute("overflowable"),
    "Toolbar should not be overflowable"
  );
  Assert.ok(
    !node.overflowable,
    "OverflowableToolbar instance should not have been created."
  );
});
