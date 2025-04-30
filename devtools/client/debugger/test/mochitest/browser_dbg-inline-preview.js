/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at <http://mozilla.org/MPL/2.0/>. */

// Test checking inline preview feature

"use strict";

add_task(async function testInlinePreviews() {
  await pushPref("devtools.debugger.features.inline-preview", true);
  await pushPref("javascript.options.experimental.import_attributes", true);

  const dbg = await initDebugger(
    "doc-inline-preview.html",
    "inline-preview.js"
  );
  await selectSource(dbg, "inline-preview.js");

  await checkInlinePreview(dbg, "checkValues", [
    { identifier: "a:", value: '""' },
    { identifier: "b:", value: "false" },
    { identifier: "c:", value: "undefined" },
    { identifier: "d:", value: "null" },
    { identifier: "e:", value: "Array []" },
    { identifier: "f:", value: "Object { }" },
    { identifier: "reg:", value: "/^\\p{RGI_Emoji}$/v" },
    { identifier: "obj:", value: "Object { foo: 1 }" },
    {
      identifier: "bs:",
      value: "Array(101) [ {…}, {…}, {…}, … ]",
    },
  ]);

  await checkInlinePreview(dbg, "columnWise", [
    { identifier: "c:", value: '"c"' },
    { identifier: "a:", value: '"a"' },
    { identifier: "b:", value: '"b"' },
  ]);

  // Check that referencing an object property previews the property, not the
  // object (bug 1599917)
  await checkInlinePreview(dbg, "objectProperties", [
    { identifier: "obj:", value: 'Object { hello: "world", a: {…} }' },
    { identifier: "obj.hello:", value: '"world"' },
    { identifier: "obj.a.b:", value: '"c"' },
  ]);

  await checkInlinePreview(dbg, "classProperties", [
    { identifier: "i:", value: "2" },
    { identifier: "self:", value: `Object { x: 1, #privateVar: 2 }` },
  ]);

  // Check inline previews for values within a module script
  await checkInlinePreview(dbg, "runInModule", [
    { identifier: "val:", value: "4" },
    { identifier: "ids:", value: "Array [ 1, 2 ]" },
  ]);

  // Checks that open in inspector button works in inline preview
  invokeInTab("btnClick");
  await checkInspectorIcon(dbg);

  const { toolbox } = dbg;
  await toolbox.selectTool("jsdebugger");

  await waitForSelectedSource(dbg);

  // Check preview of event ( event.target should be clickable )
  // onBtnClick function in inline-preview.js
  await checkInspectorIcon(dbg);

  await dbg.toolbox.closeToolbox();
});

add_task(async function testInlinePreviewsWithExplicitResourceManagement() {
  await pushPref("devtools.debugger.features.inline-preview", true);
  // javascript.options.experimental.explicit_resource_management is set to true, but it's
  // only supported on Nightly at the moment, so only check for SuppressedError if
  // they're supported.
  if (!AppConstants.ENABLE_EXPLICIT_RESOURCE_MANAGEMENT) {
    return;
  }
  const dbg = await initDebugger("doc-inline-preview.html");

  const onPaused = waitForPaused(dbg);
  dbg.commands.scriptCommand.execute(
    `
    function explicitResourceManagement() {
      using erm = {
        [Symbol.dispose]() {},
        foo: 42
      };
      console.log(erm.foo);
      debugger;
    }; explicitResourceManagement();`,
    {}
  );
  await onPaused;

  await checkInlinePreview(dbg, "explicitResourceManagement", [
    {
      identifier: "erm:",
      value: `Object { foo: 42, Symbol("Symbol.dispose"): Symbol.dispose() }`,
    },
  ]);

  await dbg.toolbox.closeToolbox();
});

async function checkInlinePreview(dbg, fnName, inlinePreviews) {
  invokeInTab(fnName);

  await waitForAllElements(dbg, "inlinePreviewLabels", inlinePreviews.length);

  const labels = findAllElements(dbg, "inlinePreviewLabels");
  const values = findAllElements(dbg, "inlinePreviewValues");

  inlinePreviews.forEach((inlinePreview, index) => {
    const { identifier, value } = inlinePreview;
    is(
      labels[index].innerText,
      identifier,
      `${identifier} in ${fnName} has correct inline preview label`
    );
    is(
      values[index].innerText,
      value,
      `${identifier} in ${fnName} has correct inline preview value`
    );
  });

  await resume(dbg);
}

async function checkInspectorIcon(dbg) {
  const node = await waitForElement(dbg, "inlinePreviewOpenInspector");

  // Ensure hovering over button highlights the node in content pane
  const view = node.ownerDocument.defaultView;
  const { toolbox } = dbg;
  const onNodeHighlight = toolbox.getHighlighter().waitForHighlighterShown();

  EventUtils.synthesizeMouseAtCenter(node, { type: "mousemove" }, view);

  info("Wait for node to be highlighted");
  const { nodeFront } = await onNodeHighlight;
  is(nodeFront.displayName, "button", "The correct node was highlighted");

  // Ensure panel changes when button is clicked
  const onInspectorPanelLoad = waitForInspectorPanelChange(dbg);
  node.click();
  await onInspectorPanelLoad;

  await resume(dbg);
}
