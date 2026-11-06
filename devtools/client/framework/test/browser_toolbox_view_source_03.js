/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

/**
 * Tests that Toolbox#viewSourceInStyleEditor works when style editor is not
 * yet opened.
 */

var URL = `${URL_ROOT_SSL}doc_viewsource.html`;
var CSS_URL = `${URL_ROOT_SSL}doc_theme.css`;

async function viewSource(styleSheetInDebuggerEnabled) {
  await pushPref(
    "devtools.debugger.features.stylesheets-in-debugger",
    styleSheetInDebuggerEnabled
  );

  const toolbox = await openNewTabAndToolbox(URL);

  const fileFound = await toolbox.viewStyleSourceByURL(CSS_URL, 2);
  ok(fileFound, "viewStyleSourceByURL should resolve to true if source found.");

  if (!styleSheetInDebuggerEnabled) {
    const panel = toolbox.getPanel("styleeditor");
    ok(panel, "The style editor panel was opened.");
    is(
      toolbox.currentToolId,
      "styleeditor",
      "The style editor panel was selected."
    );
    const { UI } = panel;

    is(
      UI.selectedEditor.styleSheet.href,
      CSS_URL,
      "The correct source is shown in the style editor."
    );
    is(
      UI.selectedEditor.sourceEditor.getCursor().line + 1,
      2,
      "The correct line is highlighted in the style editor's source editor."
    );
  } else {
    const debuggerPanel = toolbox.getPanel("jsdebugger");
    ok(debuggerPanel, "The debugger panel was opened.");
    is(toolbox.currentToolId, "jsdebugger", "The debugger panel was selected.");

    assertSelectedLocationInDebugger(debuggerPanel, 2, undefined, CSS_URL);
  }

  await closeToolboxAndTab(toolbox);
}

add_task(async function () {
  // Open styles in the debugger
  try {
    await viewSource(true);
  } catch (error) {
    ok(false, "Got an error: " + error.message + "\n" + error.stack);
  }

  // Open styles in the style editor
  try {
    await viewSource(false);
  } catch (error) {
    ok(false, "Got an error: " + error.message + "\n" + error.stack);
  }
  finish();
});
