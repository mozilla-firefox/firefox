/* Any copyright is dedicated to the Public Domain.
 http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Test that the stylesheet links in the rule view are correct when source maps
// are involved.

const TESTCASE_URI = URL_ROOT + "doc_sourcemaps2.html";
const PREF = "devtools.source-map.client-service.enabled";
const SCSS_LOC = "doc_sourcemaps.scss:4";
const CSS_LOC = "doc_sourcemaps2.css:1";

add_task(async function () {
  info("Setting the " + PREF + " pref to true");
  Services.prefs.setBoolPref(PREF, true);

  await addTab(TESTCASE_URI);
  const { toolbox, inspector, view } = await openRuleView();

  info("Selecting the test node");
  await selectNode("div", inspector);

  await verifyLinkText(SCSS_LOC, view);

  info("Setting the " + PREF + " pref to false");
  Services.prefs.setBoolPref(PREF, false);
  await verifyLinkText(CSS_LOC, view);

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

function verifyLinkText(text, view) {
  info("Verifying that the rule-view stylesheet link is " + text);
  const label = getRuleViewLinkByIndex(view, 1);
  return waitForSuccess(function () {
    return label.textContent == text;
  }, "Link text changed to display correct location: " + text);
}
