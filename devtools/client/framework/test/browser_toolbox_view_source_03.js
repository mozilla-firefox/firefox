/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

/**
 * Tests that Toolbox#viewSourceInStyleEditor works when style editor is not
 * yet opened.
 */

var URL = `${URL_ROOT_SSL}doc_viewsource.html`;
var CSS_URL = `${URL_ROOT_SSL}doc_theme.css`;

async function viewSource() {
  const toolbox = await openNewTabAndToolbox(URL);

  const fileFound = await toolbox.viewStyleSourceByURL(CSS_URL, 2);
  ok(fileFound, "viewStyleSourceByURL should resolve to true if source found.");

  const stylePanel = toolbox.getPanel("styleeditor");
  ok(stylePanel, "The style editor panel was opened.");
  is(
    toolbox.currentToolId,
    "styleeditor",
    "The style editor panel was selected."
  );

  const { UI } = stylePanel;

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

  await closeToolboxAndTab(toolbox);
  finish();
}

function test() {
  viewSource().then(finish, error => {
    ok(false, "Got an error: " + error.message + "\n" + error.stack);
    finish();
  });
}
