/* Any copyright is dedicated to the Public Domain.
 http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Tests that the contextual menu items shown when clicking on linked attributes
// for <script> and <link> tags actually open the right tools.

const TEST_URL = URL_ROOT_SSL + "doc_markup_links.html";

Services.scriptloader.loadSubScript(
  "chrome://mochitests/content/browser/devtools/client/debugger/test/mochitest/shared-head.js",
  this
);

add_task(async function () {
  const { toolbox, inspector } = await openInspectorForURL(TEST_URL);

  info("Select a node with a cssresource attribute");
  await selectNode("link", inspector);

  info("Set the popupNode to the node that contains the uri");
  let { editor } = await getContainerForSelector("link", inspector);
  let contextMenuItem = openContextMenuAndGetLinkFollowItem(
    inspector,
    editor.attrElements.get("href").querySelector(".link")
  );

  info("Follow the link and wait for the style-editor to open");
  const onStyleEditorReady = toolbox.once("styleeditor-ready");
  contextMenuItem.click();
  await onStyleEditorReady;

  // No real need to test that the editor opened on the right file here as this
  // is already tested in /framework/test/browser_toolbox_view_source_*
  ok(true, "The style-editor was open");

  info("Switch back to the inspector");
  await toolbox.selectTool("inspector");

  info("Select a node with a jsresource attribute");
  await selectNode("script", inspector);

  info("Set the popupNode to the node that contains the uri");
  ({ editor } = await getContainerForSelector("script", inspector));
  contextMenuItem = openContextMenuAndGetLinkFollowItem(
    inspector,
    editor.attrElements.get("src").querySelector(".link")
  );

  info("Follow the link and wait for the debugger to open");
  contextMenuItem.click();

  // Wait for the debugger to have fully processed the opened source
  await toolbox.getPanelWhenReady("jsdebugger");
  const dbg = createDebuggerContext(toolbox);
  await waitForSelectedSource(dbg, URL_ROOT_SSL + "events/lib_jquery_1.0.js");

  // No real need to test that the debugger opened on the right file here as
  // this is already tested in /framework/test/browser_toolbox_view_source_*
  ok(true, "The debugger was open");
});

function openContextMenuAndGetLinkFollowItem(inspector, target) {
  const items = openContextMenuAndGetAllItems(inspector, {
    target,
  });
  return items.find(({ id }) => id == "node-menu-link-follow");
}
