/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const TEST_URI =
  "http://example.com/browser/devtools/client/webconsole/" +
  "test/browser/" +
  "test-location-styleeditor-link.html";

add_task(async function () {
  await pushPref("devtools.webconsole.filter.css", true);
  const hud = await openNewTabAndConsole(TEST_URI);
  const toolbox = gDevTools.getToolboxForTab(gBrowser.selectedTab);

  await testViewSource(hud, toolbox, "\u2018font-weight");

  info("Selecting the console again");
  await toolbox.selectTool("webconsole");
  await testViewSource(hud, toolbox, "\u2018color");

  info("Selecting the console again");
  await toolbox.selectTool("webconsole");
  await testViewSource(hud, toolbox, "\u2018display");
});

async function testViewSource(hud, toolbox, text) {
  info(`Testing message with text "${text}"`);
  const messageNode = await waitFor(
    () => findWarningMessage(hud, text),
    `couldn't find message containing "${text}"`
  );
  const messageLocationNode = messageNode.querySelector(".message-location");
  ok(messageLocationNode, "The message does have a location link");

  const onStyleEditorSelected = toolbox.once("styleeditor-selected");

  EventUtils.sendMouseEvent(
    { type: "click" },
    messageNode.querySelector(".frame-link-filename")
  );

  const panel = await onStyleEditorSelected;
  ok(
    true,
    "The style editor is selected when clicking on the location element"
  );

  const win = panel.panelWindow;
  ok(win, "Style Editor Window is defined");
  is(
    win.location.toString(),
    "chrome://devtools/content/styleeditor/index.xhtml",
    "This is the expected styleEditor document"
  );

  info("Waiting the style editor to be focused");
  await new Promise(resolve => waitForFocus(resolve, win));

  info("style editor window focused");
  const href = messageLocationNode.getAttribute("data-url");
  const line = messageLocationNode.getAttribute("data-line");
  // The displayed column is 1-based, while editor cursor is 0-based.
  const column = messageLocationNode.getAttribute("data-column") - 1;
  ok(line, "found source line");

  const editor = panel.UI.editors.find(e => e.styleSheet.href == href);
  ok(editor, "found style editor for " + href);
  await waitFor(
    () => panel.UI.selectedStyleSheetIndex == editor.styleSheet.styleSheetIndex
  );
  ok(true, "correct stylesheet is selected in the editor");

  info("wait for source editor to load and to move the cursor");
  await editor.getSourceEditor();
  await waitFor(() => editor.sourceEditor.getCursor().line !== 0);

  // Get the updated line and column position if the CSS source was prettified.
  const position = editor.translateCursorPosition(line - 1, column - 1);
  const cursor = editor.sourceEditor.getCursor();
  is(cursor.line, position.line, "correct line is selected");
  is(cursor.ch, position.column, "correct column is selected");
}
