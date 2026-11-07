/* Any copyright is dedicated to the Public Domain.
 http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Test that the stylesheet links in the rule view are correct when source maps
// are involved.

const TESTCASE_URI = URL_ROOT_SSL + "doc_sourcemaps.html";
const PREF = "devtools.source-map.client-service.enabled";
const SCSS_FILENAME = "doc_sourcemaps.scss";
const SCSS_LOC_LINE = 4;
const CSS_FILENAME = "doc_sourcemaps.css";
const CSS_LOC_LINE = 1;

add_task(async function () {
  info("Setting the " + PREF + " pref to true");
  Services.prefs.setBoolPref(PREF, true);

  await addTab(TESTCASE_URI);
  const { toolbox, inspector, view } = await openRuleView();

  info("Selecting the test node");
  await selectNode("div", inspector);

  await verifyStyleSheetLink(view, SCSS_FILENAME, SCSS_LOC_LINE);

  info("Setting the " + PREF + " pref to false");
  Services.prefs.setBoolPref(PREF, false);
  await verifyStyleSheetLink(view, CSS_FILENAME, CSS_LOC_LINE);

  info("Setting the " + PREF + " pref to true again");
  Services.prefs.setBoolPref(PREF, true);

  await testClickingLink(toolbox, view);
  const selectedEditor =
    await waitForOriginalStyleSheetEditorSelection(toolbox);

  const href = selectedEditor.styleSheet.href;
  ok(
    href.endsWith("doc_sourcemaps.scss"),
    "selected stylesheet is correct one"
  );

  await selectedEditor.getSourceEditor();
  const { line } = selectedEditor.sourceEditor.getCursor();
  is(line, 3, "cursor is at correct line number in original source");

  info("Clearing the " + PREF + " pref");
  Services.prefs.clearUserPref(PREF);
});

async function testClickingLink(toolbox, view) {
  info("Listening for switch to the style editor");
  const onStyleEditorReady = toolbox.once("styleeditor-selected");

  info("Finding the stylesheet link and clicking it");
  const link = getRuleViewLinkByIndex(view, 1);
  link.scrollIntoView();
  link.click();
  await onStyleEditorReady;
}

function waitForOriginalStyleSheetEditorSelection(toolbox) {
  const panel = toolbox.getCurrentPanel();
  return new Promise(resolve => {
    const maybeContinue = editor => {
      // The style editor selects the first sheet at first load before
      // selecting the desired sheet.
      if (editor.styleSheet.href.endsWith("scss")) {
        info("Original source editor selected");
        off();
        resolve(editor);
      }
    };
    const off = panel.UI.on("editor-selected", maybeContinue);
    if (panel.UI.selectedEditor) {
      maybeContinue(panel.UI.selectedEditor);
    }
  });
}

async function verifyStyleSheetLink(view, fileName, lineNumber) {
  const expectedLocation = `${fileName}:${lineNumber}`;
  const expectedUrl = URL_ROOT_SSL + fileName;
  const expectedTitle = `View source in Style Editor → ${URL_ROOT_SSL}${expectedLocation}`;

  info("Verifying that the rule-view stylesheet link is " + expectedLocation);
  const label = getRuleViewLinkByIndex(view, 1);
  await waitForSuccess(function () {
    return (
      label.textContent == expectedLocation &&
      label.getAttribute("title") === expectedTitle
    );
  }, "Link text changed to display correct location: " + expectedLocation);

  const copyLocationMenuItem = openStyleContextMenuAndGetAllItems(
    view,
    label
  ).find(
    item =>
      item.label ===
      STYLE_INSPECTOR_L10N.getStr("styleinspector.contextmenu.copyLocation")
  );

  try {
    await waitForClipboardPromise(
      () => copyLocationMenuItem.click(),
      () => SpecialPowers.getClipboardData("text/plain") === expectedUrl
    );
    ok(true, "Expected URL was copied to clipboard");
  } catch (e) {
    ok(false, `Clipboard text does not match expected "${expectedUrl}" url`);
  }
}
