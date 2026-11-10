/* Any copyright is dedicated to the Public Domain.
 http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Test that the stylesheet links in the rule view are correct when source maps
// are defined using the X-SourceMap (non-standard) response headers.

const TESTCASE_URI = URL_ROOT + "doc_sourcemaps2.html";
const PREF = "devtools.source-map.client-service.enabled";
const SCSS_LOC = "doc_sourcemaps.scss:4";
const CSS_LOC = "doc_sourcemaps2.css:1";

add_task(async function () {
  // Open styles in the debugger
  await testLinksInRuleView(true);
  // Open styles in the style editor
  await testLinksInRuleView(false);
});

async function testLinksInRuleView(styleSheetInDebuggerEnabled) {
  await pushPref(
    "devtools.debugger.features.stylesheets-in-debugger",
    styleSheetInDebuggerEnabled
  );

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

  await testClickStylesheetLink(toolbox, view, 1);

  if (!styleSheetInDebuggerEnabled) {
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
  } else {
    const debuggerPanel = toolbox.getPanel("jsdebugger");
    // Wait until the source location is selected
    const location = await waitFor(() => {
      return debuggerPanel._selectors.getSelectedLocation(
        debuggerPanel._getState()
      );
    });
    ok(
      location.source.url.endsWith("doc_sourcemaps.scss"),
      "selected stylesheet is correct one"
    );
    is(location.line, 4, "cursor is at correct line number in original source");
  }

  info("Clearing the " + PREF + " pref");
  Services.prefs.clearUserPref(PREF);
}

function verifyLinkText(text, view) {
  info("Verifying that the rule-view stylesheet link is " + text);
  const label = getRuleViewLinkByIndex(view, 1);
  return waitForSuccess(function () {
    return label.textContent == text;
  }, "Link text changed to display correct location: " + text);
}
