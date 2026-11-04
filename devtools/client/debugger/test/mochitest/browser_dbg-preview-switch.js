/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at <http://mozilla.org/MPL/2.0/>. */

// Check that switching between different previews doesn't release the underlying object actors
// Bug 1944408 - Can not expand info about variable

"use strict";

add_task(async function () {
  // Make the editor tall enough for the previews to open below the hovered
  // line. Otherwise a preview opens on the right and covers the other token.
  await pushPref("devtools.toolbox.footer.height", 600);
  const dbg = await initDebugger("doc-preview.html", "preview.js");

  info(
    "Check that switching between different previews doesn't release the underlying object actors"
  );
  invokeInTab("classPreview");
  await waitForPaused(dbg);

  const fooTokenEl = await getTokenFromPosition(dbg, { line: 50, column: 44 });
  const privateStaticTokenEl = await getTokenFromPosition(dbg, {
    line: 50,
    column: 48,
  });

  hoverToken(fooTokenEl);
  await waitForPreviewWithResult(dbg, "class Foo");

  // We want the ObjectInspector component to be updated with new roots rather than destroyed and recreated.
  // This is hard to trigger reliably so we switch between different previews multiple times with different delays.
  info("Switch between different previews multiple times");
  for (let i = 0; i < 5; i++) {
    await waitForTime(i * 5);
    hoverToken(privateStaticTokenEl);
    await waitForPreviewWithResult(dbg, "Object");

    hoverToken(fooTokenEl);
    await waitForPreviewWithResult(dbg, "class Foo");
  }

  info("Try to expand a property");
  await toggleExpanded(dbg, 3);
  await waitForTime(1000);
  ok(true, "The property was expanded");
});
